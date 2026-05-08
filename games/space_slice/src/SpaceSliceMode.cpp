#include "reaktio/games/space_slice/SpaceSliceMode.hpp"

#include "reaktio/foundation/CrashSafeLog.hpp"
#include "reaktio/foundation/DeterministicRandom.hpp"
#include "reaktio/gameplay/GameplayInput.hpp"
#include "reaktio/gameplay/IModeHost.hpp"
#include "reaktio/gameplay/ModeFlow.hpp"
#include "reaktio/gameplay/PresentationEvents.hpp"
#include "reaktio/gameplay/ReplayRecorder.hpp"
#include "reaktio/gameplay/SaveData.hpp"
#include "reaktio/platform/FrameClock.hpp"
#include "reaktio/render/RenderCamera.hpp"
#include "reaktio/render/RenderExtraction.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

namespace reaktio::games::space_slice {

namespace {

constexpr float k_two_pi = 6.28318530717958647692f;
constexpr float k_pi = 3.14159265358979323846f;

const gameplay::ModeDescriptor k_descriptor = []() {
    gameplay::ModeDescriptor descriptor{};
    descriptor.id = "mode.space.slice";
    descriptor.display_name = "Space Slice";
    descriptor.family = "space";
    descriptor.description =
        "Vertical-slice spatial / obstacle-heavy mode validating full 3D camera, "
        "deterministic kinematic primitives, seeded pattern generation, and "
        "music-reactive presentation on the shared engine stack.";
    descriptor.capabilities =
        gameplay::ModeCapabilities::UsesActionInput |
        gameplay::ModeCapabilities::UsesTransport |
        gameplay::ModeCapabilities::EmitsRenderPackets |
        gameplay::ModeCapabilities::RecordsReplay |
        gameplay::ModeCapabilities::SupportsPractice;
    return descriptor;
}();

[[nodiscard]] rhythm::TempoMap make_default_tempo_map() {
    rhythm::TempoMap map{};
    rhythm::TempoMapDefinition definition{};
    definition.config.ticks_per_quarter_note = 480;
    definition.config.sample_rate_hz = 48000;
    definition.tempo_changes.push_back(
        rhythm::TempoChange{.start_tick = 0, .microseconds_per_quarter_note = 500000});
    definition.time_signature_changes.push_back(
        rhythm::TimeSignatureChange{.start_tick = 0, .numerator = 4, .denominator = 4});
    map.rebuild(std::move(definition));
    return map;
}

[[nodiscard]] std::vector<rhythm::ScheduledCue> make_default_schedule() {
    std::vector<rhythm::ScheduledCue> cues;
    cues.reserve(32);
    for (std::uint32_t i = 0; i < 32; ++i) {
        rhythm::ScheduledCue cue{};
        cue.hit_tick = static_cast<rhythm::ChartTick>(960 + i * 240);
        cue.channel_index = i % 4u;
        cues.push_back(cue);
    }
    return cues;
}

[[nodiscard]] inline float wrap_angle(float angle_radians) noexcept {
    float wrapped = std::fmod(angle_radians + k_pi, k_two_pi);
    if (wrapped < 0.0f) {
        wrapped += k_two_pi;
    }
    return wrapped - k_pi;
}

// Small helper translating a player heading + orbit radius into a world
// position relative to the tunnel center using the rig's basis. Mirrors
// the math inside SpatialKinematics so collision tests can reuse the
// SpatialCollision primitives directly.
[[nodiscard]] gameplay::Vector3 sample_world_position(
    const SpaceSliceConfig& config,
    float heading_radians,
    float radius,
    float axis_offset_along_forward) noexcept {
    const float c = std::cos(heading_radians);
    const float s = std::sin(heading_radians);

    // axis_right = cross(up, forward). Compute inline to keep this header-free.
    const gameplay::Vector3 up = config.tunnel_axis_up;
    const gameplay::Vector3 fwd = config.tunnel_axis_forward;
    const gameplay::Vector3 right{
        up.y * fwd.z - up.z * fwd.y,
        up.z * fwd.x - up.x * fwd.z,
        up.x * fwd.y - up.y * fwd.x,
    };
    return gameplay::Vector3{
        config.tunnel_center.x + right.x * c * radius + up.x * s * radius +
            fwd.x * axis_offset_along_forward,
        config.tunnel_center.y + right.y * c * radius + up.y * s * radius +
            fwd.y * axis_offset_along_forward,
        config.tunnel_center.z + right.z * c * radius + up.z * s * radius +
            fwd.z * axis_offset_along_forward,
    };
}

} // namespace

const gameplay::ModeDescriptor& SpaceSliceMode::mode_descriptor() noexcept { return k_descriptor; }

SpaceSliceMode::SpaceSliceMode() : SpaceSliceMode(SpaceSliceConfig{}) {}

SpaceSliceMode::SpaceSliceMode(SpaceSliceConfig config) : config_(std::move(config)) {}

const gameplay::ModeDescriptor& SpaceSliceMode::descriptor() const noexcept { return k_descriptor; }

void SpaceSliceMode::ensure_initialized(gameplay::IModeHost& host) {
    if (initialized_) {
        return;
    }

    tempo_map_ = make_default_tempo_map();
    timing_windows_ = rhythm::make_default_timing_window_set();
    scheduled_cues_ = make_default_schedule();
    cue_scheduler_.reset();
    scoring_.reset();
    player_ = SpaceSlicePlayerState{};
    player_.orbit_radius = config_.player_orbit_radius;

    // Author hazards: deterministic per (root_seed, pattern_index). Each
    // pattern is allocated a stride of the hazard-id space so multiple
    // patterns can share a stream without their hazards colliding in the
    // ID range. By default the slice pulls a stream from the host's
    // DeterministicRandomService so the runtime root seed propagates
    // (and replays remain stable). use_host_random_service=false swaps
    // in a private RNG for off-band tooling that wants reproducible
    // output independent of the runtime seed.
    foundation::DeterministicRng private_pattern_rng{config_.pattern_seed};
    foundation::DeterministicRng* pattern_rng_ptr =
        config_.use_host_random_service
            ? &host.random_service().stream(config_.pattern_rng_stream_name)
            : &private_pattern_rng;
    foundation::DeterministicRng& pattern_rng = *pattern_rng_ptr;
    hazards_.clear();
    hazards_authored_ = 0;
    patterns_authored_ = 0;
    for (std::uint32_t i = 0; i < config_.pattern_request_count; ++i) {
        gameplay::PatternRequest request{};
        request.kind = static_cast<gameplay::PatternKind>(
            static_cast<std::uint8_t>(i % 4u));
        request.slice_count = (i % 2u == 0u) ? 12u : 8u;
        request.gap_count = 1u + (i % 3u);
        request.spawn_radius = 28.0f + static_cast<float>(i) * 0.5f;
        request.radial_velocity_per_second = -5.0f - static_cast<float>(i) * 0.10f;
        request.slice_arc_radians = 0.40f;
        request.pattern_parameter = (i % 5u) * 0.12f;
        request.hazard_id_offset = static_cast<std::uint64_t>(i) * 1024ull;

        std::vector<gameplay::RingSliceHazard> emitted;
        const std::size_t added = gameplay::generate_ring_slice_pattern(
            pattern_rng, request, emitted);
        for (gameplay::RingSliceHazard& hazard : emitted) {
            SpaceSliceHazardInstance instance{};
            instance.runtime = gameplay::make_ring_slice_runtime(hazard);
            instance.pattern_index = i;
            instance.active = true;
            hazards_.push_back(instance);
        }
        if (added > 0u) {
            ++patterns_authored_;
            hazards_authored_ += static_cast<std::uint32_t>(added);
        }
    }

    // Beat-synced presentation envelopes. These are the slice's
    // music-reactive surface; modes pull the envelope value each frame
    // and forward it to debug-text intensity, screen effects, etc.
    beat_pulse_config_ = gameplay::PulseEnvelopeConfig{};
    beat_pulse_config_.unit = gameplay::PulseEnvelopeUnit::Beat;
    beat_pulse_config_.stride = 1.0;
    beat_pulse_config_.attack_seconds = 0.04;
    beat_pulse_config_.decay_seconds = 0.28;
    bar_pulse_config_ = gameplay::PulseEnvelopeConfig{};
    bar_pulse_config_.unit = gameplay::PulseEnvelopeUnit::Bar;
    bar_pulse_config_.stride = 1.0;
    bar_pulse_config_.attack_seconds = 0.05;
    bar_pulse_config_.decay_seconds = 0.55;
    last_beat_pulse_ = 0.0;
    last_bar_pulse_ = 0.0;
    last_beat_pulse_emitted_ = false;
    last_bar_pulse_emitted_ = false;

    hazards_resolved_ = 0;
    hazards_avoided_ = 0;
    hazards_hit_ = 0;
    presentation_events_emitted_ = 0;

    initialized_ = true;
    (void)host;
}

void SpaceSliceMode::on_enter(
    gameplay::IModeHost& host, const gameplay::ModeEnterContext& context) {
    ensure_initialized(host);
    host.flow().begin(gameplay::ModeFlowReason::EnterMode);

    if (config_.record_save_data) {
        gameplay::SaveSetting setting{};
        setting.key = "last_played_mode";
        setting.value.kind = gameplay::SaveSettingValueKind::Text;
        setting.value.text_value = std::string(descriptor().id);
        host.save_data().upsert_setting(
            "profile", std::move(setting), host.frame_timing().wall_clock_ns);
        (void)host.save_data().grant_unlock(
            "reaktio.space.first_run",
            std::string(descriptor().id),
            host.frame_timing().wall_clock_ns);
    }

    ++lifecycle_event_count_;
    (void)context;
}

void SpaceSliceMode::apply_input(gameplay::IModeHost& host, double fixed_delta_seconds) {
    const gameplay::ActionInputSurface& actions = host.input().actions();
    const bool orbit_left = actions.is_down(
        k_space_action_context, k_space_action_orbit_left);
    const bool orbit_right = actions.is_down(
        k_space_action_context, k_space_action_orbit_right);
    const bool dive = actions.is_down(k_space_action_context, k_space_action_dive);
    const bool climb = actions.is_down(k_space_action_context, k_space_action_climb);

    const float dt = static_cast<float>(fixed_delta_seconds);
    if (orbit_left) {
        player_.orbit_heading_radians -= config_.player_orbit_speed_radians_per_second * dt;
    }
    if (orbit_right) {
        player_.orbit_heading_radians += config_.player_orbit_speed_radians_per_second * dt;
    }
    player_.orbit_heading_radians = wrap_angle(player_.orbit_heading_radians);

    if (dive) {
        player_.orbit_radius -= config_.player_radius_speed_per_second * dt;
    }
    if (climb) {
        player_.orbit_radius += config_.player_radius_speed_per_second * dt;
    }
    player_.orbit_radius = std::clamp(
        player_.orbit_radius, config_.player_radius_min, config_.player_radius_max);
}

void SpaceSliceMode::advance_world(
    gameplay::IModeHost& host, const gameplay::ModeFixedStepContext& context) {
    const double dt = context.fixed_delta_seconds;

    // Advance every hazard's radial sweep. Bulk advance keeps the inner
    // loop tight and lets the compiler vectorize the linear update.
    {
        // Build a transient span by reinterpreting the underlying sweep
        // state of each instance. We can't span<RadialSweepState> directly
        // because the instances embed runtime + bookkeeping; advance each
        // via the per-element function instead.
        for (SpaceSliceHazardInstance& instance : hazards_) {
            if (!instance.active) {
                continue;
            }
            gameplay::advance_radial_sweep(instance.runtime.sweep, dt);
        }
    }

    // Player world position (rides on the orbit ring at the entry plane
    // of the tunnel, axis_offset = 0). Build a sphere collider once per
    // step; hazards are tested as oriented boxes spanning the slice arc
    // and band height.
    const gameplay::Vector3 player_world = sample_world_position(
        config_, player_.orbit_heading_radians, player_.orbit_radius, 0.0f);
    const gameplay::SphereVolume player_volume{
        .center = player_world,
        .radius = config_.player_collision_radius,
    };

    for (SpaceSliceHazardInstance& instance : hazards_) {
        if (!instance.active || instance.resolved) {
            continue;
        }
        const gameplay::RadialSweepState& sweep = instance.runtime.sweep;

        // Build the hazard's oriented box at its current radius. The box
        // spans the slice arc tangentially, the band height vertically,
        // and a thin radial depth. axis_right points along the orbit
        // tangent at the hazard's heading; axis_up is the orbit-plane up.
        const gameplay::Vector3 hazard_world = sample_world_position(
            config_, sweep.heading_radians, sweep.radius, 0.0f);
        const float c = std::cos(sweep.heading_radians);
        const float s = std::sin(sweep.heading_radians);

        // Tangent direction = derivative of position w.r.t. heading; in
        // our basis = -right * sin + up * cos.
        const gameplay::Vector3 up_basis = config_.tunnel_axis_up;
        const gameplay::Vector3 fwd_basis = config_.tunnel_axis_forward;
        const gameplay::Vector3 right_basis{
            up_basis.y * fwd_basis.z - up_basis.z * fwd_basis.y,
            up_basis.z * fwd_basis.x - up_basis.x * fwd_basis.z,
            up_basis.x * fwd_basis.y - up_basis.y * fwd_basis.x,
        };
        const gameplay::Vector3 tangent{
            -right_basis.x * s + up_basis.x * c,
            -right_basis.y * s + up_basis.y * c,
            -right_basis.z * s + up_basis.z * c,
        };
        const gameplay::Vector3 radial{
            right_basis.x * c + up_basis.x * s,
            right_basis.y * c + up_basis.y * s,
            right_basis.z * c + up_basis.z * s,
        };

        gameplay::OrientedBoxVolume box{};
        box.center = hazard_world;
        box.axis_right = tangent;
        box.axis_up = fwd_basis;            // Band axis lines up with tunnel forward.
        box.axis_forward = radial;
        // World-space tangent half-extent at CURRENT radius. Carrying the
        // angular extent (radians) on the runtime and projecting it into
        // world units here keeps the box's perceived angular size constant
        // as the hazard sweeps inward; pre-projecting at spawn radius (the
        // earlier implementation) would have made hazards visually 3-4x
        // bigger by the time they reached the player.
        const float tangent_half_world =
            std::max(0.05f, instance.runtime.arc_half_extent_radians *
                                std::max(0.1f, sweep.radius));
        box.half_extents.x = tangent_half_world;
        box.half_extents.y = std::max(0.05f, instance.runtime.band_half_height);
        box.half_extents.z = 0.40f;          // Slice radial thickness.

        if (gameplay::sphere_overlaps_obb(player_volume, box)) {
            // Player crashed into this slice. Score Miss, emit a flash,
            // and resolve so a subsequent step doesn't double-count.
            ++hazards_hit_;
            ++hazards_resolved_;
            instance.resolved = true;
            instance.active = false;
            gameplay::ScoreJudgementEvent event{};
            event.cue_id = instance.runtime.hazard_id;
            event.cue_hit_tick = static_cast<rhythm::ChartTick>(context.fixed_step_index);
            event.judgement.judgement = rhythm::TimingJudgement::Miss;
            scoring_.record_judgement(event);
            const bool emitted = host.presentation_events().publish(
                gameplay::ScreenEffectEvent{
                    .kind = gameplay::ScreenEffectKind::Flash,
                    .intensity = 1.0f,
                    .frame_index = context.frame_index,
                });
            if (emitted) {
                ++presentation_events_emitted_;
            }
            continue;
        }

        // Hazard reached the inner radius without contacting the player:
        // score it as a successful avoidance. The threshold is relative
        // to the player's current orbit radius so it stays meaningful as
        // the player climbs/dives.
        if (sweep.radius <= player_.orbit_radius - 0.5f) {
            ++hazards_avoided_;
            ++hazards_resolved_;
            instance.resolved = true;
            instance.active = false;
            gameplay::ScoreJudgementEvent event{};
            event.cue_id = instance.runtime.hazard_id;
            event.cue_hit_tick = static_cast<rhythm::ChartTick>(context.fixed_step_index);
            event.judgement.judgement = rhythm::TimingJudgement::Great;
            scoring_.record_judgement(event);
        }
    }
}

void SpaceSliceMode::evaluate_music_envelopes(
    gameplay::IModeHost& host, const gameplay::ModeFixedStepContext& context) {
    if (!tempo_map_.valid()) {
        return;
    }
    const gameplay::TransportSnapshot snapshot = host.transport().snapshot();
    const rhythm::TimelineMicroseconds position_us =
        static_cast<rhythm::TimelineMicroseconds>(
            snapshot.position_seconds > 0.0 ? snapshot.position_seconds * 1'000'000.0 : 0.0);

    const double new_beat = gameplay::evaluate_pulse_envelope(
        beat_pulse_config_, tempo_map_, position_us);
    const double new_bar = gameplay::evaluate_pulse_envelope(
        bar_pulse_config_, tempo_map_, position_us);

    // Edge-detect the beat envelope so we publish a single ColorPulse per
    // beat, not one per fixed step. The envelope itself is replay-stable
    // (pure function of the position + tempo map); the edge-detection is
    // local state so we don't spam the bus.
    const bool beat_emitted_now = !last_beat_pulse_emitted_ && new_beat > 0.6;
    if (beat_emitted_now) {
        const bool emitted = host.presentation_events().publish(
            gameplay::ScreenEffectEvent{
                .kind = gameplay::ScreenEffectKind::ColorPulse,
                .intensity = static_cast<float>(new_beat),
                .frame_index = context.frame_index,
            });
        if (emitted) {
            ++presentation_events_emitted_;
        }
    }
    last_beat_pulse_emitted_ = new_beat > 0.6;
    last_beat_pulse_ = new_beat;

    const bool bar_emitted_now = !last_bar_pulse_emitted_ && new_bar > 0.6;
    if (bar_emitted_now) {
        const bool emitted = host.presentation_events().publish(
            gameplay::ScreenEffectEvent{
                .kind = gameplay::ScreenEffectKind::Flash,
                .intensity = static_cast<float>(new_bar),
                .frame_index = context.frame_index,
            });
        if (emitted) {
            ++presentation_events_emitted_;
        }
    }
    last_bar_pulse_emitted_ = new_bar > 0.6;
    last_bar_pulse_ = new_bar;
}

void SpaceSliceMode::on_fixed_step(
    gameplay::IModeHost& host, const gameplay::ModeFixedStepContext& context) {
    ensure_initialized(host);

    if (tempo_map_.valid()) {
        gameplay::CueSchedulerUpdateInput input{};
        input.tempo_map = &tempo_map_;
        const gameplay::TransportSnapshot snapshot = host.transport().snapshot();
        input.transport = &snapshot;
        input.schedule = std::span<const rhythm::ScheduledCue>(scheduled_cues_);
        gameplay::CueSchedulerRules rules{};
        rules.schedule_when_stopped = true;
        input.rules = rules;
        cue_scheduler_.update(input);
    }

    apply_input(host, context.fixed_delta_seconds);
    advance_world(host, context);
    evaluate_music_envelopes(host, context);

    // Replay sample submission: each fixed step we record the player's
    // orbit heading + radius as the engagement metric, mirroring the
    // typing/rail slices' replay sample policy.
    if (config_.record_replay_samples) {
        host.replay().record_judgement_sample(gameplay::ReplayJudgementSample{
            .frame_index = context.frame_index,
            .simulation_step = context.fixed_step_index,
            .schedule_index = hazards_resolved_,
            .channel_index = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(player_.orbit_heading_radians * 100.0f) & 0xFF),
            .judgement = rhythm::TimingJudgement::Great,
            .raw_error_microseconds = 0,
            .corrected_error_microseconds = 0,
            .applied_offset_microseconds = 0,
        });
    }

    // Flow transitions: when every authored hazard is resolved, mark
    // cleared and request the success transition. This proves the same
    // ModeFlow controller drives the spatial slice's lifecycle.
    if (hazards_resolved_ >= hazards_authored_ && hazards_authored_ > 0u) {
        scoring_.mark_cleared();
        if (host.flow().can_succeed()) {
            host.flow().succeed(gameplay::ModeFlowReason::SongEnded);
        }
    }
}

void SpaceSliceMode::on_render_extract(
    gameplay::IModeHost& host, const gameplay::ModeRenderContext& context) {
    if (!initialized_) {
        return;
    }

    // FreeCamera3D path. Closes Phase 10's "full 3D camera/extraction"
    // bullet: the rail slice already exercised PerspectiveCamera25D, the
    // typing slice exercised the orthographic 2D path, and this slice
    // routes a FreeCamera3D through the shared render extraction.
    gameplay::TunnelCameraRig rig{};
    rig.tunnel_center = config_.tunnel_center;
    rig.tunnel_axis_forward = config_.tunnel_axis_forward;
    rig.tunnel_axis_up = config_.tunnel_axis_up;
    rig.player_heading_radians = player_.orbit_heading_radians;
    rig.player_orbit_radius = player_.orbit_radius;
    rig.follow_back_distance = 6.0f;
    rig.follow_height_offset = 1.0f;
    const render::FreeCamera3D camera = gameplay::sample_tunnel_camera(rig);
    host.render_extraction().set_view_camera(render::RenderView::MainScene, camera);

    std::ostringstream player_line;
    player_line << "Space slice: heading="
                << player_.orbit_heading_radians
                << " radius=" << player_.orbit_radius
                << " status=" << player_.status_flags;
    host.render_extraction().add_debug_text(0, 8, 0x0a, player_line.str());

    std::ostringstream hazard_line;
    hazard_line << "patterns=" << patterns_authored_
                << " hazards=" << hazards_authored_
                << " resolved=" << hazards_resolved_
                << " avoided=" << hazards_avoided_
                << " hit=" << hazards_hit_
                << " beat-pulse=" << last_beat_pulse_
                << " bar-pulse=" << last_bar_pulse_;
    host.render_extraction().add_debug_text(0, 9, 0x0e, hazard_line.str());

    (void)context;
}

void SpaceSliceMode::on_exit(
    gameplay::IModeHost& host, const gameplay::ModeExitContext& context) {
    if (!initialized_) {
        return;
    }

    const gameplay::ScoreSummary summary = scoring_.summary();
    if (host.flow().can_present_results()) {
        host.flow().present_results(summary, "mode.space.slice.exit");
    }

    if (config_.record_save_data) {
        gameplay::SaveModeStatsResult result{};
        result.mode_id = std::string(descriptor().id);
        result.song_id = "reaktio.space.intro";
        result.cleared = summary.run_state == gameplay::ScoreRunState::Cleared;
        result.failed = summary.run_state == gameplay::ScoreRunState::Failed;
        result.score = summary.score;
        result.combo = summary.max_combo;
        result.accuracy_ratio = summary.accuracy_ratio;
        result.grade = std::string(gameplay::to_string(summary.grade));
        result.play_time_ms = static_cast<std::uint64_t>(
            host.transport().snapshot().position_seconds > 0.0
                ? host.transport().snapshot().position_seconds * 1000.0
                : 0.0);
        result.wall_clock_ns = host.frame_timing().wall_clock_ns;
        host.save_data().record_mode_session(result);
    }

    ++lifecycle_event_count_;
    (void)context;
}

std::uint32_t SpaceSliceMode::hazards_active() const noexcept {
    std::uint32_t count = 0;
    for (const SpaceSliceHazardInstance& instance : hazards_) {
        if (instance.active && !instance.resolved) {
            ++count;
        }
    }
    return count;
}

} // namespace reaktio::games::space_slice
