#include "reaktio/games/reference/ReferenceSandboxMode.hpp"

#include "reaktio/foundation/DeterministicRandom.hpp"
#include "reaktio/foundation/Telemetry.hpp"
#include "reaktio/gameplay/EventBus.hpp"
#include "reaktio/gameplay/IModeHost.hpp"
#include "reaktio/gameplay/ModeConfiguration.hpp"
#include "reaktio/gameplay/MotionCollision.hpp"
#include "reaktio/gameplay/ReplayRecorder.hpp"
#include "reaktio/gameplay/Transforms.hpp"
#include "reaktio/gameplay/Transport.hpp"
#include "reaktio/gameplay/WorldModel.hpp"
#include "reaktio/platform/FrameClock.hpp"
#include "reaktio/platform/InputBindings.hpp"
#include "reaktio/platform/InputSnapshot.hpp"
#include "reaktio/render/RenderCamera.hpp"
#include "reaktio/render/RenderExtraction.hpp"

#include <array>
#include <cmath>
#include <sstream>
#include <string>
#include <string_view>

namespace reaktio::games::reference {

namespace {

const gameplay::ModeDescriptor k_descriptor{
    .id = "mode.reference.sandbox",
    .display_name = "Reference Sandbox",
    .description = "Reference mode that exercises lifecycle, input, transport stubs, and render extraction.",
};

std::uint32_t state_color(gameplay::TransportPlaybackState playback_state) noexcept {
    switch (playback_state) {
    case gameplay::TransportPlaybackState::Playing:
        return 0x1f4d2cff;
    case gameplay::TransportPlaybackState::Paused:
        return 0x7f5a1fff;
    case gameplay::TransportPlaybackState::Stopped:
        return 0x4a2430ff;
    }

    return 0x16324cff;
}

std::uint32_t mix_visual_color(gameplay::TransportPlaybackState playback_state, std::uint32_t visual_roll) noexcept {
    const std::uint32_t base = state_color(playback_state);
    const std::uint32_t red = (base >> 24u) & 0xffu;
    const std::uint32_t green = (base >> 16u) & 0xffu;
    const std::uint32_t blue = 0x20u + (visual_roll % 0xa0u);
    return (red << 24u) | (green << 16u) | (blue << 8u) | 0xffu;
}

std::uint64_t make_state_hash(
    std::uint64_t fixed_steps,
    const gameplay::TransportSnapshot& transport_snapshot,
    std::uint32_t transport_roll,
    std::uint32_t visual_roll,
    float average_phase,
    float cue_world_x,
    gameplay::Vector3 tip_world,
    std::uint64_t collision_signature) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    hash ^= fixed_steps;
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(transport_snapshot.playback_state);
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(transport_snapshot.position_seconds * 1000000.0);
    hash *= 1099511628211ull;
    hash ^= transport_roll;
    hash *= 1099511628211ull;
    hash ^= visual_roll;
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(static_cast<std::int64_t>(std::llround(average_phase * 1000000.0f)));
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(static_cast<std::int64_t>(std::llround(cue_world_x * 1000.0f)));
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(static_cast<std::int64_t>(std::llround(tip_world.x * 1000.0f)));
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(static_cast<std::int64_t>(std::llround(tip_world.y * 1000.0f)));
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(static_cast<std::int64_t>(std::llround(tip_world.z * 1000.0f)));
    hash *= 1099511628211ull;
    hash ^= collision_signature;
    return hash;
}

std::uint64_t hash_collision_report(const gameplay::CollisionDetectionReport& collision_report) noexcept {
    const auto hash_float = [](std::uint64_t& hash, float value, float scale) noexcept {
        hash ^= static_cast<std::uint64_t>(static_cast<std::int64_t>(std::llround(value * scale)));
        hash *= 1099511628211ull;
    };

    std::uint64_t hash = 14695981039346656037ull;
    hash ^= collision_report.circle_circle_contacts;
    hash *= 1099511628211ull;
    hash ^= collision_report.box_box_contacts;
    hash *= 1099511628211ull;
    hash ^= collision_report.circle_box_contacts;
    hash *= 1099511628211ull;
    hash ^= collision_report.skipped_missing_transforms;
    hash *= 1099511628211ull;

    for (const gameplay::CollisionContact2D& contact : collision_report.contacts) {
        hash ^= contact.first.value();
        hash *= 1099511628211ull;
        hash ^= contact.second.value();
        hash *= 1099511628211ull;
        hash ^= static_cast<std::uint32_t>(contact.shape_pair);
        hash *= 1099511628211ull;
        hash_float(hash, contact.normal.x, 1000000.0f);
        hash_float(hash, contact.normal.y, 1000000.0f);
        hash_float(hash, contact.penetration, 1000.0f);
    }

    return hash;
}

std::uint64_t hash_collision_topology(const gameplay::CollisionDetectionReport& collision_report) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    hash ^= collision_report.circle_circle_contacts;
    hash *= 1099511628211ull;
    hash ^= collision_report.box_box_contacts;
    hash *= 1099511628211ull;
    hash ^= collision_report.circle_box_contacts;
    hash *= 1099511628211ull;
    hash ^= collision_report.skipped_missing_transforms;
    hash *= 1099511628211ull;

    for (const gameplay::CollisionContact2D& contact : collision_report.contacts) {
        hash ^= contact.first.value();
        hash *= 1099511628211ull;
        hash ^= contact.second.value();
        hash *= 1099511628211ull;
        hash ^= static_cast<std::uint32_t>(contact.shape_pair);
        hash *= 1099511628211ull;
    }

    return hash;
}

struct SandboxPulseCue {
    float phase{};
    float phase_velocity{};
};

struct SandboxLaneCue {
    std::uint32_t lane_index{};
};

struct SandboxModeConfig {
    float velocity_scale{1.0f};
    float hit_window_half_width{32.0f};
    float hit_window_half_height{24.0f};
    std::string cue_material_authoring_id{"reference.sandbox.material.cue"};
    std::string debug_font_authoring_id{"reference.sandbox.font.debug"};
};

SandboxModeConfig load_sandbox_mode_config(const gameplay::ModeConfigurationStore& store) {
    const gameplay::ModeConfigurationView view = store.view(k_descriptor.id);
    SandboxModeConfig config{};
    config.velocity_scale = static_cast<float>(view.get_double("velocity_scale", config.velocity_scale));
    config.hit_window_half_width = static_cast<float>(view.get_double("hit_window_half_width", config.hit_window_half_width));
    config.hit_window_half_height = static_cast<float>(view.get_double("hit_window_half_height", config.hit_window_half_height));
    config.cue_material_authoring_id = std::string(
        view.get_string("cue_material_authoring_id", config.cue_material_authoring_id));
    config.debug_font_authoring_id = std::string(
        view.get_string("debug_font_authoring_id", config.debug_font_authoring_id));
    return config;
}

std::string describe_binding(
    const platform::InputBindingsConfig& input_bindings,
    std::string_view action_id,
    std::string_view fallback) {
    if (const platform::InputActionBinding* binding = input_bindings.find_action(action_id)) {
        if (!binding->primary.empty() && !binding->secondary.empty()) {
            return binding->primary + "/" + binding->secondary;
        }
        if (!binding->primary.empty()) {
            return binding->primary;
        }
        if (!binding->secondary.empty()) {
            return binding->secondary;
        }
    }

    return std::string(fallback);
}

float sample_average_phase(const gameplay::WorldModel& world) {
    float total_phase = 0.0f;
    std::size_t count = 0;
    world.for_each<SandboxPulseCue>([&](gameplay::WorldEntity, const SandboxPulseCue& pulse) {
        total_phase += pulse.phase;
        ++count;
    });
    return count > 0 ? total_phase / static_cast<float>(count) : 0.0f;
}

std::string sample_first_label(const gameplay::WorldModel& world) {
    std::string label{"none"};
    bool assigned = false;
    world.for_each<gameplay::EntityName, SandboxLaneCue>(
        [&](gameplay::WorldEntity, const gameplay::EntityName& entity_name, const SandboxLaneCue& lane) {
            if (assigned) {
                return;
            }

            std::ostringstream stream;
            stream << entity_name.value << ":lane" << lane.lane_index;
            label = stream.str();
            assigned = true;
        });
    return label;
}

void publish_transport_event(
    gameplay::IModeHost& host,
    std::string_view action,
    std::uint64_t fixed_steps,
    const gameplay::TransportSnapshot& transport_snapshot) {
    host.event_bus().publish(
        "mode.reference.sandbox",
        host.frame_timing().frame_index,
        fixed_steps,
        gameplay::TransportEvent{
            .action = std::string(action),
            .playback_state = transport_snapshot.playback_state,
            .position_seconds = transport_snapshot.position_seconds,
            .loop_enabled = transport_snapshot.loop_region.enabled,
        });
}

void publish_replay_checkpoint_event(
    gameplay::IModeHost& host,
    std::string_view label,
    std::uint64_t fixed_steps,
    std::uint64_t authoritative_state_hash) {
    host.event_bus().publish(
        "mode.reference.sandbox",
        host.frame_timing().frame_index,
        fixed_steps,
        gameplay::ReplayCheckpointEvent{
            .label = std::string(label),
            .simulation_step = fixed_steps,
            .authoritative_state_hash = authoritative_state_hash,
            .checkpoint_count = host.replay().checkpoint_count(),
        });
}

void seed_resources(
    foundation::ResourceRegistry& resource_registry,
    foundation::ResourceRegistrySummary& resource_summary,
    foundation::ResourceHandle& cue_material_handle,
    foundation::ResourceHandle& debug_font_handle,
    bool& stale_debug_font_handle_valid,
    std::string_view cue_material_authoring_id,
    std::string_view debug_font_authoring_id) {
    (void)resource_registry.register_resource(
        foundation::ResourceKind::Texture,
        "reference.sandbox.texture.cue",
        "runtime.texture.cue");
    (void)resource_registry.register_resource(
        foundation::ResourceKind::Material,
        "reference.sandbox.material.cue",
        "runtime.material.cue");
    (void)resource_registry.register_resource(
        foundation::ResourceKind::Material,
        "reference.sandbox.material.hit-window",
        "runtime.material.hit-window");
    (void)resource_registry.register_resource(
        foundation::ResourceKind::Mesh,
        "reference.sandbox.mesh.rig",
        "runtime.mesh.rig");
    (void)resource_registry.register_resource(
        foundation::ResourceKind::ShaderProgram,
        "reference.sandbox.shader.basic",
        "runtime.shader.basic");

    const foundation::ResourceHandle stale_font_handle = resource_registry.register_resource(
        foundation::ResourceKind::Font,
        "reference.sandbox.font.debug",
        "runtime.font.debug.v1");
    (void)resource_registry.register_resource(
        foundation::ResourceKind::Font,
        "reference.sandbox.font.debug",
        "runtime.font.debug.v2");

    cue_material_handle = {};
    if (const foundation::ResourceRecord* resource = resource_registry.find(
            foundation::ResourceKind::Material,
            cue_material_authoring_id);
        resource != nullptr) {
        cue_material_handle = resource->handle;
    }

    debug_font_handle = resource_registry.resolve(
        foundation::ResourceKind::Font,
        debug_font_authoring_id);
    stale_debug_font_handle_valid = resource_registry.contains(stale_font_handle);
    resource_summary = resource_registry.summary();
}

void publish_mode_config_diagnostic(
    gameplay::IModeHost& host,
    std::uint64_t fixed_steps,
    const SandboxModeConfig& config) {
    std::ostringstream stream;
    stream << "mode-config speed=" << config.velocity_scale << " hit=" << config.hit_window_half_width << 'x'
           << config.hit_window_half_height << " cue-id=" << config.cue_material_authoring_id
           << " font-id=" << config.debug_font_authoring_id;
    host.event_bus().publish(
        "mode.reference.sandbox",
        host.frame_timing().frame_index,
        fixed_steps,
        gameplay::DiagnosticEvent{
            .message = stream.str(),
        });
}

void publish_input_binding_diagnostic(
    gameplay::IModeHost& host,
    std::uint64_t fixed_steps,
    std::string_view pause_binding,
    std::string_view restart_binding) {
    std::ostringstream stream;
    stream << "input-bindings pause=" << pause_binding << " restart=" << restart_binding;
    host.event_bus().publish(
        "mode.reference.sandbox",
        host.frame_timing().frame_index,
        fixed_steps,
        gameplay::DiagnosticEvent{
            .message = stream.str(),
        });
}

void publish_resource_diagnostic(
    gameplay::IModeHost& host,
    const foundation::ResourceRegistrySummary& resource_summary,
    foundation::ResourceHandle cue_material_handle,
    foundation::ResourceHandle debug_font_handle,
    bool stale_debug_font_handle_valid) {
    std::ostringstream stream;
    stream << "resources count=" << resource_summary.resource_count << " rev=" << resource_summary.revision
           << " textures="
           << resource_summary.counts_by_kind[foundation::to_index(foundation::ResourceKind::Texture)]
           << " materials="
           << resource_summary.counts_by_kind[foundation::to_index(foundation::ResourceKind::Material)]
            << " shaders="
            << resource_summary.counts_by_kind[foundation::to_index(foundation::ResourceKind::ShaderProgram)]
           << " meshes=" << resource_summary.counts_by_kind[foundation::to_index(foundation::ResourceKind::Mesh)]
           << " fonts=" << resource_summary.counts_by_kind[foundation::to_index(foundation::ResourceKind::Font)]
           << " cue-material=" << cue_material_handle.value() << " debug-font=" << debug_font_handle.value()
           << " stale-valid=" << stale_debug_font_handle_valid;
    host.event_bus().publish(
        "mode.reference.sandbox",
        host.frame_timing().frame_index,
        0,
        gameplay::DiagnosticEvent{
            .message = stream.str(),
        });
}

void seed_world(
    gameplay::IModeHost& host,
    float velocity_scale,
    float hit_window_half_width,
    float hit_window_half_height) {
    gameplay::WorldModel& world = host.world_model();

    constexpr std::array<float, 3> k_spawn_x{-240.0f, 0.0f, 240.0f};
    constexpr std::array<float, 3> k_velocity_x{72.0f, -48.0f, 60.0f};
    constexpr std::array<float, 3> k_phase_velocity{0.07f, 0.11f, 0.05f};
    constexpr std::array<float, 3> k_angular_velocity{0.35f, -0.28f, 0.22f};

    for (std::size_t index = 0; index < k_spawn_x.size(); ++index) {
        const gameplay::WorldEntity lane_root = world.create_entity("reference.sandbox.lane-root");
        world.emplace<gameplay::LocalTransform2D>(
            lane_root,
            gameplay::LocalTransform2D{
                .translation = {
                    .x = k_spawn_x[index],
                    .y = -120.0f + static_cast<float>(index) * 120.0f,
                },
            });

        const gameplay::WorldEntity hit_window = world.create_entity("reference.sandbox.hit-window");
        world.emplace<gameplay::TransformParent>(hit_window, gameplay::TransformParent{.parent = lane_root});
        world.emplace<gameplay::LocalTransform2D>(
            hit_window,
            gameplay::LocalTransform2D{
                .translation = {.x = 0.0f, .y = 0.0f},
            });
        world.emplace<gameplay::AxisAlignedBoxCollider2D>(
            hit_window,
            gameplay::AxisAlignedBoxCollider2D{
                .half_extents = {.x = hit_window_half_width, .y = hit_window_half_height},
            });
        world.emplace<gameplay::CollisionFilter2D>(
            hit_window,
            gameplay::CollisionFilter2D{.layer_bits = 0x2u, .collides_with_bits = 0x1u});

        const gameplay::WorldEntity cue = world.create_entity("reference.sandbox.cue");
        world.emplace<gameplay::TransformParent>(cue, gameplay::TransformParent{.parent = lane_root});
        world.emplace<gameplay::LocalTransform2D>(cue, gameplay::LocalTransform2D{});
        world.emplace<gameplay::LinearVelocity2D>(
            cue,
            gameplay::LinearVelocity2D{.units_per_second = {.x = k_velocity_x[index] * velocity_scale, .y = 0.0f}});
        world.emplace<gameplay::AngularVelocity2D>(
            cue,
            gameplay::AngularVelocity2D{.radians_per_second = k_angular_velocity[index]});
        world.emplace<gameplay::CircleCollider2D>(cue, gameplay::CircleCollider2D{.radius = 18.0f});
        world.emplace<gameplay::CollisionFilter2D>(
            cue,
            gameplay::CollisionFilter2D{.layer_bits = 0x1u, .collides_with_bits = 0x2u});
        world.emplace<SandboxPulseCue>(
            cue,
            SandboxPulseCue{
                .phase = static_cast<float>(index) * 0.25f,
                .phase_velocity = k_phase_velocity[index],
            });
        world.emplace<SandboxLaneCue>(cue, SandboxLaneCue{.lane_index = static_cast<std::uint32_t>(index)});
    }

    const gameplay::WorldEntity rig_root = world.create_entity("reference.sandbox.rig-root");
    world.emplace<gameplay::LocalTransform3D>(
        rig_root,
        gameplay::LocalTransform3D{
            .translation = {.x = 0.0f, .y = 0.25f, .z = 6.0f},
            .rotation = gameplay::make_axis_angle_rotation({.x = 0.0f, .y = 1.0f, .z = 0.0f}, 0.2f),
        });
    world.emplace<gameplay::AngularVelocity3D>(
        rig_root,
        gameplay::AngularVelocity3D{.axis = {.x = 0.0f, .y = 1.0f, .z = 0.0f}, .radians_per_second = 0.09f});

    const gameplay::WorldEntity rig_arm = world.create_entity("reference.sandbox.rig-arm");
    world.emplace<gameplay::TransformParent>(rig_arm, gameplay::TransformParent{.parent = rig_root});
    world.emplace<gameplay::LocalTransform3D>(
        rig_arm,
        gameplay::LocalTransform3D{
            .translation = {.x = 0.75f, .y = 0.0f, .z = 0.9f},
            .rotation = gameplay::make_axis_angle_rotation({.x = 0.0f, .y = 1.0f, .z = 0.0f}, -0.1f),
        });
    world.emplace<gameplay::AngularVelocity3D>(
        rig_arm,
        gameplay::AngularVelocity3D{.axis = {.x = 0.0f, .y = 1.0f, .z = 0.0f}, .radians_per_second = -0.05f});

    const gameplay::WorldEntity rig_tip = world.create_entity("reference.sandbox.rig-tip");
    world.emplace<gameplay::TransformParent>(rig_tip, gameplay::TransformParent{.parent = rig_arm});
    world.emplace<gameplay::LocalTransform3D>(
        rig_tip,
        gameplay::LocalTransform3D{
            .translation = {.x = 0.5f, .y = 0.0f, .z = 1.25f},
        });
}

void update_world(gameplay::WorldModel& world) {
    world.for_each<gameplay::LocalTransform2D, SandboxPulseCue>(
        [](gameplay::WorldEntity, gameplay::LocalTransform2D& transform, SandboxPulseCue& pulse) {
            pulse.phase += pulse.phase_velocity;
            if (pulse.phase >= 1.0f) {
                pulse.phase -= 1.0f;
            }

            transform.translation.y = std::sin(pulse.phase * 6.28318531f) * 18.0f;
        });
}

float sample_first_world_x(const gameplay::WorldModel& world) {
    float world_x = 0.0f;
    bool assigned = false;
    world.for_each<gameplay::WorldTransform2D, SandboxLaneCue>(
        [&](gameplay::WorldEntity, const gameplay::WorldTransform2D& transform, const SandboxLaneCue&) {
            if (assigned) {
                return;
            }

            world_x = transform.translation.x;
            assigned = true;
        });
    return world_x;
}

gameplay::Vector3 sample_tip_world_position(const gameplay::WorldModel& world) {
    gameplay::Vector3 tip_position{};
    bool assigned = false;
    world.for_each<gameplay::EntityName, gameplay::WorldTransform3D>(
        [&](gameplay::WorldEntity, const gameplay::EntityName& name, const gameplay::WorldTransform3D& transform) {
            if (assigned || name.value != "reference.sandbox.rig-tip") {
                return;
            }

            tip_position = transform.translation;
            assigned = true;
        });
    return tip_position;
}

void publish_transform_diagnostic(
    gameplay::IModeHost& host,
    std::uint64_t fixed_steps,
    const gameplay::TransformPropagationReport& report) {
    if ((report.detached_2d + report.detached_3d + report.stale_world_transforms_2d +
         report.stale_world_transforms_3d + report.cycle_breaks_2d + report.cycle_breaks_3d) == 0u) {
        return;
    }

    std::ostringstream stream;
    stream << "transforms 2d=" << report.propagated_2d << " 3d=" << report.propagated_3d
           << " detached=" << (report.detached_2d + report.detached_3d)
           << " cycles=" << (report.cycle_breaks_2d + report.cycle_breaks_3d);
    host.event_bus().publish(
        "mode.reference.sandbox",
        host.frame_timing().frame_index,
        fixed_steps,
        gameplay::DiagnosticEvent{
            .message = stream.str(),
        });
}

void publish_motion_collision_diagnostic(
    gameplay::IModeHost& host,
    std::uint64_t fixed_steps,
    std::uint64_t collision_topology,
    std::uint64_t& last_published_collision_topology,
    const gameplay::MotionIntegrationReport& motion_report,
    const gameplay::CollisionDetectionReport& collision_report) {
    if (collision_report.skipped_missing_transforms == 0u &&
        collision_topology == last_published_collision_topology) {
        return;
    }

    std::ostringstream stream;
    stream << "motion l2=" << motion_report.linear_2d << " a2=" << motion_report.angular_2d
           << " l3=" << motion_report.linear_3d << " a3=" << motion_report.angular_3d
            << " contacts=" << collision_report.contacts.size() << " skipped="
           << collision_report.skipped_missing_transforms;
    if (!collision_report.contacts.empty()) {
        stream << " first=" << gameplay::to_string(collision_report.contacts.front().shape_pair)
               << " pen=" << collision_report.contacts.front().penetration;
    }

    host.event_bus().publish(
        "mode.reference.sandbox",
        host.frame_timing().frame_index,
        fixed_steps,
        gameplay::DiagnosticEvent{
            .message = stream.str(),
        });

    last_published_collision_topology = collision_topology;
}

void refresh_transform_summary(
    std::size_t& world_entity_count,
    float& average_phase,
    float& sample_cue_world_x,
    gameplay::Vector3& sample_tip_world,
    const gameplay::WorldModel& world) {
    world_entity_count = world.entity_count();
    average_phase = sample_average_phase(world);
    sample_cue_world_x = sample_first_world_x(world);
    sample_tip_world = sample_tip_world_position(world);
}

void propagate_and_refresh(
    gameplay::TransformPropagationReport& propagation_report,
    gameplay::CollisionDetectionReport& collision_report,
    std::uint64_t& collision_signature,
    std::uint64_t& collision_topology,
    std::size_t& world_entity_count,
    float& average_phase,
    float& sample_cue_world_x,
    gameplay::Vector3& sample_tip_world,
    std::uint64_t& last_published_collision_topology,
    const gameplay::MotionIntegrationReport& motion_report,
    std::uint64_t fixed_steps,
    gameplay::IModeHost& host) {
    propagation_report = gameplay::propagate_transforms(host.world_model());
    refresh_transform_summary(world_entity_count, average_phase, sample_cue_world_x, sample_tip_world, host.world_model());
    collision_report = gameplay::detect_collisions_2d(host.world_model());
    collision_signature = hash_collision_report(collision_report);
    collision_topology = hash_collision_topology(collision_report);
    publish_transform_diagnostic(host, fixed_steps, propagation_report);
    publish_motion_collision_diagnostic(
        host,
        fixed_steps,
        collision_topology,
        last_published_collision_topology,
        motion_report,
        collision_report);
}

} // namespace

const gameplay::ModeDescriptor& ReferenceSandboxMode::mode_descriptor() noexcept {
    return k_descriptor;
}

const gameplay::ModeDescriptor& ReferenceSandboxMode::descriptor() const noexcept {
    return k_descriptor;
}

void ReferenceSandboxMode::on_enter(gameplay::IModeHost& host) {
    fixed_steps_ = 0;
    transport_roll_ = 0;
    visual_roll_ = 0;
    configured_transport_pause_binding_ = "keyboard:Space";
    configured_transport_restart_binding_ = "keyboard:R";
    configured_velocity_scale_ = 1.0f;
    configured_hit_window_half_width_ = 32.0f;
    configured_hit_window_half_height_ = 24.0f;
    configured_cue_material_authoring_id_ = "reference.sandbox.material.cue";
    configured_debug_font_authoring_id_ = "reference.sandbox.font.debug";
    resource_summary_ = {};
    cue_material_handle_ = {};
    debug_font_handle_ = {};
    stale_debug_font_handle_valid_ = false;
    world_entity_count_ = 0;
    average_phase_ = 0.0f;
    sample_cue_world_x_ = 0.0f;
    sample_tip_world_ = {};
    collision_signature_ = 0;
    collision_topology_ = 0;
    last_published_collision_topology_ = static_cast<std::uint64_t>(-1);
    motion_report_ = {};
    collision_report_ = {};
    propagation_report_ = {};

    const SandboxModeConfig mode_config = load_sandbox_mode_config(host.mode_configuration());
    configured_velocity_scale_ = mode_config.velocity_scale;
    configured_hit_window_half_width_ = mode_config.hit_window_half_width;
    configured_hit_window_half_height_ = mode_config.hit_window_half_height;
    configured_cue_material_authoring_id_ = mode_config.cue_material_authoring_id;
    configured_debug_font_authoring_id_ = mode_config.debug_font_authoring_id;
    configured_transport_pause_binding_ = describe_binding(
        host.input_bindings(),
        "transport_pause",
        configured_transport_pause_binding_);
    configured_transport_restart_binding_ = describe_binding(
        host.input_bindings(),
        "transport_restart",
        configured_transport_restart_binding_);

    seed_resources(
        host.resource_registry(),
        resource_summary_,
        cue_material_handle_,
        debug_font_handle_,
        stale_debug_font_handle_valid_,
        configured_cue_material_authoring_id_,
        configured_debug_font_authoring_id_);
    seed_world(
        host,
        configured_velocity_scale_,
        configured_hit_window_half_width_,
        configured_hit_window_half_height_);
    propagate_and_refresh(
        propagation_report_,
        collision_report_,
        collision_signature_,
        collision_topology_,
        world_entity_count_,
        average_phase_,
        sample_cue_world_x_,
        sample_tip_world_,
        last_published_collision_topology_,
        motion_report_,
        fixed_steps_,
        host);
    publish_resource_diagnostic(
        host,
        resource_summary_,
        cue_material_handle_,
        debug_font_handle_,
        stale_debug_font_handle_valid_);
    publish_mode_config_diagnostic(host, fixed_steps_, mode_config);
    publish_input_binding_diagnostic(
        host,
        fixed_steps_,
        configured_transport_pause_binding_,
        configured_transport_restart_binding_);

    gameplay::ITransportControl& transport = host.transport();
    transport.stop();
    transport.set_loop_region(0.75, 1.25);
    transport.play();

    host.random_service().reset_streams();
    const gameplay::TransportSnapshot& transport_snapshot = transport.snapshot();
    const std::uint64_t checkpoint_hash = make_state_hash(
        fixed_steps_,
        transport_snapshot,
        transport_roll_,
        visual_roll_,
        average_phase_,
        sample_cue_world_x_,
        sample_tip_world_,
        collision_signature_);
    host.replay().record_checkpoint(gameplay::ReplayCheckpoint{
        .frame_index = host.frame_timing().frame_index,
        .simulation_step = fixed_steps_,
        .transport_state = transport_snapshot.playback_state,
        .transport_position_seconds = transport_snapshot.position_seconds,
        .root_random_seed = host.random_service().root_seed(),
        .authoritative_state_hash = checkpoint_hash,
        .label = "enter",
        .summary = "reference sandbox entered and initialized transport loop region",
    });
    publish_transport_event(host, "enter-play", fixed_steps_, transport_snapshot);
    publish_replay_checkpoint_event(host, "enter", fixed_steps_, checkpoint_hash);
    host.event_bus().publish(
        "mode.reference.sandbox",
        host.frame_timing().frame_index,
        fixed_steps_,
        gameplay::DiagnosticEvent{
            .message = "world seeded entities=" + std::to_string(world_entity_count_),
        });
}

void ReferenceSandboxMode::on_fixed_step(gameplay::IModeHost& host, double fixed_delta_seconds) {
    ++fixed_steps_;

    foundation::DeterministicRng& transport_rng =
        host.random_service().stream("reference-sandbox.transport");
    foundation::DeterministicRng& visual_rng =
        host.random_service().stream("reference-sandbox.visual");
    transport_roll_ = transport_rng.next_u32(0u, 9999u);
    visual_roll_ = visual_rng.next_u32(0u, 255u);

    gameplay::ITransportControl& transport = host.transport();
    std::string_view action = "tick";
    if (fixed_steps_ == 2) {
        transport.pause();
        action = "pause";
    } else if (fixed_steps_ == 3) {
        transport.seek(0.50);
        action = "seek";
    } else if (fixed_steps_ == 4) {
        transport.play();
        action = "play";
    } else if (fixed_steps_ == 5) {
        transport.clear_loop_region();
        action = "clear-loop";
    } else if (fixed_steps_ == 6) {
        transport.set_loop_region(1.0, 1.4);
        action = "set-loop";
    } else if (fixed_steps_ == 7) {
        transport.restart();
        action = "restart";
    }

    update_world(host.world_model());
    motion_report_ = gameplay::integrate_motion(host.world_model(), fixed_delta_seconds);

    const gameplay::TransportSnapshot& transport_snapshot = transport.snapshot();
    propagate_and_refresh(
        propagation_report_,
        collision_report_,
        collision_signature_,
        collision_topology_,
        world_entity_count_,
        average_phase_,
        sample_cue_world_x_,
        sample_tip_world_,
        last_published_collision_topology_,
        motion_report_,
        fixed_steps_,
        host);

    const std::uint64_t checkpoint_hash = make_state_hash(
        fixed_steps_,
        transport_snapshot,
        transport_roll_,
        visual_roll_,
        average_phase_,
        sample_cue_world_x_,
        sample_tip_world_,
        collision_signature_);
    host.replay().record_checkpoint(gameplay::ReplayCheckpoint{
        .frame_index = host.frame_timing().frame_index,
        .simulation_step = fixed_steps_,
        .transport_state = transport_snapshot.playback_state,
        .transport_position_seconds = transport_snapshot.position_seconds,
        .root_random_seed = host.random_service().root_seed(),
        .authoritative_state_hash = checkpoint_hash,
        .label = "fixed-step",
        .summary = "sandbox transport/RNG/world state checkpoint",
    });
    publish_transport_event(host, action, fixed_steps_, transport_snapshot);
    publish_replay_checkpoint_event(host, "fixed-step", fixed_steps_, checkpoint_hash);

    foundation::TelemetrySnapshot snapshot{};
    snapshot.audio_drift_ms = 0.00;
    snapshot.visible_cues = 3;
    snapshot.draw_calls = 0;
    host.telemetry().record(snapshot);
}

void ReferenceSandboxMode::on_render_extract(gameplay::IModeHost& host, double interpolation_alpha) {
    const gameplay::TransportSnapshot& transport_snapshot = host.transport().snapshot();
    const platform::InputSnapshot& input_snapshot = host.input_snapshot();
    const foundation::DeterministicRandomService& random_service = host.random_service();
    const foundation::DeterministicRng* transport_rng = random_service.find_stream("reference-sandbox.transport");
    const foundation::DeterministicRng* visual_rng = random_service.find_stream("reference-sandbox.visual");
    const gameplay::EventBus& event_bus = host.event_bus();
    const gameplay::EventRecord* last_event = event_bus.last();
    const gameplay::ReplayRecorder& replay_recorder = host.replay();
    const gameplay::ReplayCheckpoint* last_checkpoint = replay_recorder.last_checkpoint();

    host.render_extraction().set_view_camera(
        reaktio::render::RenderView::MainScene,
        reaktio::render::OrthographicCamera2D{
            .center = {0.0f, 0.0f},
            .virtual_height = 720.0f,
            .view_distance = 10.0f,
            .near_plane = 0.0f,
            .far_plane = 100.0f,
        });
    host.render_extraction().set_main_scene_clear(mix_visual_color(transport_snapshot.playback_state, visual_roll_));

    std::ostringstream state_stream;
    state_stream << "transport=" << gameplay::to_string(transport_snapshot.playback_state) << " pos="
                 << transport_snapshot.position_seconds << '/' << transport_snapshot.duration_seconds << "s alpha="
                 << interpolation_alpha;
    host.render_extraction().add_debug_text(0, 8, 0x0f, state_stream.str());

    std::ostringstream loop_stream;
    loop_stream << "loop=" << transport_snapshot.loop_region.enabled << " ["
                << transport_snapshot.loop_region.start_seconds << ", "
                << transport_snapshot.loop_region.end_seconds << "] loops="
                << transport_snapshot.completed_loops << " fixed-steps="
                << transport_snapshot.advanced_fixed_steps;
    host.render_extraction().add_debug_text(0, 9, 0x0e, loop_stream.str());

    std::ostringstream input_stream;
    input_stream << "keys=" << input_snapshot.keyboard_events().size() << " mouse="
                 << input_snapshot.mouse_button_events().size() << " text="
                 << input_snapshot.text_input_events().size() << " gamepads="
                 << input_snapshot.connected_gamepads().size();
    host.render_extraction().add_debug_text(0, 10, 0x0a, input_stream.str());

    std::ostringstream rng_stream;
    rng_stream << "rng root=0x" << std::hex << random_service.root_seed() << std::dec
               << " streams=" << random_service.stream_count() << " transport-roll="
               << transport_roll_ << " visual-roll=" << visual_roll_ << " draws="
               << (transport_rng != nullptr ? transport_rng->generated_values() : 0) << '/'
               << (visual_rng != nullptr ? visual_rng->generated_values() : 0);
    host.render_extraction().add_debug_text(0, 11, 0x0d, rng_stream.str());

    std::ostringstream replay_stream;
    replay_stream << "replay inputs=" << replay_recorder.input_frame_count() << " checkpoints="
                  << replay_recorder.checkpoint_count() << " last="
                  << (last_checkpoint != nullptr ? last_checkpoint->label : std::string_view("none"));
    host.render_extraction().add_debug_text(0, 12, 0x0c, replay_stream.str());

    std::ostringstream event_stream;
    event_stream << "events=" << event_bus.published_count() << '/' << event_bus.count() << " last="
                 << (last_event != nullptr ? gameplay::describe_event(*last_event) : std::string("none"));
    host.render_extraction().add_debug_text(0, 13, 0x0b, event_stream.str());

    std::ostringstream world_stream;
    world_stream << "world entities=" << world_entity_count_ << " phase=" << average_phase_
                 << " sample=" << sample_first_label(host.world_model()) << " x=" << sample_cue_world_x_
                 << " tip-z=" << sample_tip_world_.z;
    host.render_extraction().add_debug_text(0, 14, 0x0f, world_stream.str());

    std::ostringstream transform_stream;
    transform_stream << "propagate 2d=" << propagation_report_.propagated_2d << " 3d="
                     << propagation_report_.propagated_3d << " detached="
                     << (propagation_report_.detached_2d + propagation_report_.detached_3d) << " stale="
                     << (propagation_report_.stale_world_transforms_2d + propagation_report_.stale_world_transforms_3d)
                     << " cycles=" << (propagation_report_.cycle_breaks_2d + propagation_report_.cycle_breaks_3d);
    host.render_extraction().add_debug_text(0, 15, 0x0e, transform_stream.str());

    std::ostringstream motion_stream;
    motion_stream << "motion l2=" << motion_report_.linear_2d << " a2=" << motion_report_.angular_2d
                  << " l3=" << motion_report_.linear_3d << " a3=" << motion_report_.angular_3d
                  << " contacts=" << collision_report_.contacts.size();
    if (!collision_report_.contacts.empty()) {
        motion_stream << " first=" << gameplay::to_string(collision_report_.contacts.front().shape_pair)
                      << " pen=" << collision_report_.contacts.front().penetration;
    }
    host.render_extraction().add_debug_text(0, 16, 0x0d, motion_stream.str());

    std::ostringstream resource_stream;
    resource_stream << "resources count=" << resource_summary_.resource_count << " rev=" << resource_summary_.revision
                    << " tex="
                    << resource_summary_.counts_by_kind[foundation::to_index(foundation::ResourceKind::Texture)]
                    << " mat="
                    << resource_summary_.counts_by_kind[foundation::to_index(foundation::ResourceKind::Material)]
                    << " shader="
                    << resource_summary_.counts_by_kind[foundation::to_index(foundation::ResourceKind::ShaderProgram)]
                    << " mesh="
                    << resource_summary_.counts_by_kind[foundation::to_index(foundation::ResourceKind::Mesh)]
                    << " font="
                    << resource_summary_.counts_by_kind[foundation::to_index(foundation::ResourceKind::Font)]
                    << " cue=" << cue_material_handle_.value() << " debug=" << debug_font_handle_.value()
                    << " stale=" << stale_debug_font_handle_valid_;
    host.render_extraction().add_debug_text(0, 17, 0x0c, resource_stream.str());

    std::ostringstream config_stream;
    config_stream << "cfg speed=" << configured_velocity_scale_ << " hit=" << configured_hit_window_half_width_
                  << 'x' << configured_hit_window_half_height_ << " cue-id="
                  << configured_cue_material_authoring_id_ << " font-id="
                  << configured_debug_font_authoring_id_;
    host.render_extraction().add_debug_text(0, 18, 0x0b, config_stream.str());

    std::ostringstream binding_stream;
    binding_stream << "bindings pause=" << configured_transport_pause_binding_
                   << " restart=" << configured_transport_restart_binding_;
    host.render_extraction().add_debug_text(0, 19, 0x0a, binding_stream.str());
}

void ReferenceSandboxMode::on_exit(gameplay::IModeHost& host) {
    host.transport().stop();
    const gameplay::TransportSnapshot& transport_snapshot = host.transport().snapshot();
    const std::uint64_t checkpoint_hash = make_state_hash(
        fixed_steps_,
        transport_snapshot,
        transport_roll_,
        visual_roll_,
        average_phase_,
        sample_cue_world_x_,
        sample_tip_world_,
        collision_signature_);
    host.replay().record_checkpoint(gameplay::ReplayCheckpoint{
        .frame_index = host.frame_timing().frame_index,
        .simulation_step = fixed_steps_,
        .transport_state = transport_snapshot.playback_state,
        .transport_position_seconds = transport_snapshot.position_seconds,
        .root_random_seed = host.random_service().root_seed(),
        .authoritative_state_hash = checkpoint_hash,
        .label = "exit",
        .summary = "reference sandbox exit state after transport stop",
    });
    publish_transport_event(host, "exit-stop", fixed_steps_, transport_snapshot);
    publish_replay_checkpoint_event(host, "exit", fixed_steps_, checkpoint_hash);
    host.event_bus().publish(
        "mode.reference.sandbox",
        host.frame_timing().frame_index,
        fixed_steps_,
        gameplay::DiagnosticEvent{
            .message = "world exiting entities=" + std::to_string(host.world_model().entity_count()),
        });
}

} // namespace reaktio::games::reference