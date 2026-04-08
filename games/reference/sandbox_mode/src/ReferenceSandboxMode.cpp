#include "reaktio/games/reference/ReferenceSandboxMode.hpp"

#include "reaktio/foundation/DeterministicRandom.hpp"
#include "reaktio/foundation/Telemetry.hpp"
#include "reaktio/gameplay/EventBus.hpp"
#include "reaktio/gameplay/IModeHost.hpp"
#include "reaktio/gameplay/ReplayRecorder.hpp"
#include "reaktio/gameplay/Transforms.hpp"
#include "reaktio/gameplay/Transport.hpp"
#include "reaktio/gameplay/WorldModel.hpp"
#include "reaktio/platform/FrameClock.hpp"
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
    std::uint32_t visual_roll) noexcept {
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
    return hash;
}

struct SandboxMotionCue2D {
    float velocity_x{};
};

struct SandboxPulseCue {
    float phase{};
    float phase_velocity{};
};

struct SandboxLaneCue {
    std::uint32_t lane_index{};
};

struct SandboxRigSpin3D {
    float yaw_radians{};
    float yaw_velocity{};
};

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

void seed_world(gameplay::IModeHost& host) {
    gameplay::WorldModel& world = host.world_model();

    constexpr std::array<float, 3> k_spawn_x{-240.0f, 0.0f, 240.0f};
    constexpr std::array<float, 3> k_velocity_x{12.0f, -6.0f, 9.0f};
    constexpr std::array<float, 3> k_phase_velocity{0.07f, 0.11f, 0.05f};

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

        const gameplay::WorldEntity cue = world.create_entity("reference.sandbox.cue");
        world.emplace<gameplay::TransformParent>(cue, gameplay::TransformParent{.parent = lane_root});
        world.emplace<gameplay::LocalTransform2D>(cue, gameplay::LocalTransform2D{});
        world.emplace<SandboxMotionCue2D>(cue, SandboxMotionCue2D{.velocity_x = k_velocity_x[index]});
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
    world.emplace<SandboxRigSpin3D>(rig_root, SandboxRigSpin3D{.yaw_radians = 0.2f, .yaw_velocity = 0.09f});

    const gameplay::WorldEntity rig_arm = world.create_entity("reference.sandbox.rig-arm");
    world.emplace<gameplay::TransformParent>(rig_arm, gameplay::TransformParent{.parent = rig_root});
    world.emplace<gameplay::LocalTransform3D>(
        rig_arm,
        gameplay::LocalTransform3D{
            .translation = {.x = 0.75f, .y = 0.0f, .z = 0.9f},
            .rotation = gameplay::make_axis_angle_rotation({.x = 0.0f, .y = 1.0f, .z = 0.0f}, -0.1f),
        });
    world.emplace<SandboxRigSpin3D>(rig_arm, SandboxRigSpin3D{.yaw_radians = -0.1f, .yaw_velocity = -0.05f});

    const gameplay::WorldEntity rig_tip = world.create_entity("reference.sandbox.rig-tip");
    world.emplace<gameplay::TransformParent>(rig_tip, gameplay::TransformParent{.parent = rig_arm});
    world.emplace<gameplay::LocalTransform3D>(
        rig_tip,
        gameplay::LocalTransform3D{
            .translation = {.x = 0.5f, .y = 0.0f, .z = 1.25f},
        });
}

void update_world(gameplay::WorldModel& world) {
    world.for_each<gameplay::LocalTransform2D, SandboxMotionCue2D, SandboxPulseCue>(
        [](gameplay::WorldEntity, gameplay::LocalTransform2D& transform, SandboxMotionCue2D& motion, SandboxPulseCue& pulse) {
            transform.translation.x += motion.velocity_x;
            if (transform.translation.x > 120.0f) {
                transform.translation.x = -120.0f;
            } else if (transform.translation.x < -120.0f) {
                transform.translation.x = 120.0f;
            }

            pulse.phase += pulse.phase_velocity;
            if (pulse.phase >= 1.0f) {
                pulse.phase -= 1.0f;
            }

            transform.translation.y = std::sin(pulse.phase * 6.28318531f) * 18.0f;
            transform.rotation_radians = pulse.phase * 0.35f;
        });

    world.for_each<gameplay::LocalTransform3D, SandboxRigSpin3D>(
        [](gameplay::WorldEntity, gameplay::LocalTransform3D& transform, SandboxRigSpin3D& spin) {
            spin.yaw_radians += spin.yaw_velocity;
            if (spin.yaw_radians > 3.14159265f) {
                spin.yaw_radians -= 6.28318531f;
            } else if (spin.yaw_radians < -3.14159265f) {
                spin.yaw_radians += 6.28318531f;
            }

            transform.rotation = gameplay::make_axis_angle_rotation({.x = 0.0f, .y = 1.0f, .z = 0.0f}, spin.yaw_radians);
            transform.translation.y = std::sin(spin.yaw_radians) * 0.2f;
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
    std::size_t& world_entity_count,
    float& average_phase,
    float& sample_cue_world_x,
    gameplay::Vector3& sample_tip_world,
    std::uint64_t fixed_steps,
    gameplay::IModeHost& host) {
    propagation_report = gameplay::propagate_transforms(host.world_model());
    refresh_transform_summary(world_entity_count, average_phase, sample_cue_world_x, sample_tip_world, host.world_model());
    publish_transform_diagnostic(host, fixed_steps, propagation_report);
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
    average_phase_ = 0.0f;
    world_entity_count_ = 0;
    sample_cue_world_x_ = 0.0f;
    sample_tip_world_ = {};
    propagation_report_ = {};

    seed_world(host);
    propagate_and_refresh(
        propagation_report_,
        world_entity_count_,
        average_phase_,
        sample_cue_world_x_,
        sample_tip_world_,
        fixed_steps_,
        host);

    gameplay::ITransportControl& transport = host.transport();
    transport.stop();
    transport.set_loop_region(0.75, 1.25);
    transport.play();

    host.random_service().reset_streams();
    const gameplay::TransportSnapshot& transport_snapshot = transport.snapshot();
    host.replay().record_checkpoint(gameplay::ReplayCheckpoint{
        .frame_index = host.frame_timing().frame_index,
        .simulation_step = fixed_steps_,
        .transport_state = transport_snapshot.playback_state,
        .transport_position_seconds = transport_snapshot.position_seconds,
        .root_random_seed = host.random_service().root_seed(),
        .authoritative_state_hash = make_state_hash(fixed_steps_, transport_snapshot, transport_roll_, visual_roll_),
        .label = "enter",
        .summary = "reference sandbox entered and initialized transport loop region",
    });
    publish_transport_event(host, "enter-play", fixed_steps_, transport_snapshot);
    publish_replay_checkpoint_event(
        host,
        "enter",
        fixed_steps_,
        make_state_hash(fixed_steps_, transport_snapshot, transport_roll_, visual_roll_));
    host.event_bus().publish(
        "mode.reference.sandbox",
        host.frame_timing().frame_index,
        fixed_steps_,
        gameplay::DiagnosticEvent{
            .message = "world seeded entities=" + std::to_string(world_entity_count_),
        });
}

void ReferenceSandboxMode::on_fixed_step(gameplay::IModeHost& host, double) {
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

    const gameplay::TransportSnapshot& transport_snapshot = transport.snapshot();
    const std::uint64_t checkpoint_hash =
        make_state_hash(fixed_steps_, transport_snapshot, transport_roll_, visual_roll_);
    update_world(host.world_model());
    propagate_and_refresh(
        propagation_report_,
        world_entity_count_,
        average_phase_,
        sample_cue_world_x_,
        sample_tip_world_,
        fixed_steps_,
        host);
    host.replay().record_checkpoint(gameplay::ReplayCheckpoint{
        .frame_index = host.frame_timing().frame_index,
        .simulation_step = fixed_steps_,
        .transport_state = transport_snapshot.playback_state,
        .transport_position_seconds = transport_snapshot.position_seconds,
        .root_random_seed = host.random_service().root_seed(),
        .authoritative_state_hash = checkpoint_hash,
        .label = "fixed-step",
        .summary = "sandbox transport/RNG state checkpoint",
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
}

void ReferenceSandboxMode::on_exit(gameplay::IModeHost& host) {
    host.transport().stop();
    const gameplay::TransportSnapshot& transport_snapshot = host.transport().snapshot();
    const std::uint64_t checkpoint_hash =
        make_state_hash(fixed_steps_, transport_snapshot, transport_roll_, visual_roll_);
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