#include "reaktio/games/reference/ReferenceSandboxMode.hpp"

#include "reaktio/foundation/DeterministicRandom.hpp"
#include "reaktio/foundation/Telemetry.hpp"
#include "reaktio/gameplay/DebugVisualizations.hpp"
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
#include "reaktio/platform/InputBindingQueries.hpp"
#include "reaktio/platform/InputSnapshot.hpp"
#include "reaktio/render/RenderCamera.hpp"
#include "reaktio/render/RenderExtraction.hpp"
#include "reaktio/rhythm/CueTravelModel.hpp"
#include "reaktio/rhythm/LatencyCalibration.hpp"
#include "reaktio/rhythm/PracticeMode.hpp"
#include "reaktio/rhythm/TempoMap.hpp"
#include "reaktio/rhythm/TimingJudgement.hpp"

#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace reaktio::games::reference {

namespace {

constexpr std::array<float, 3> k_lane_root_x{-240.0f, 0.0f, 240.0f};
constexpr std::array<float, 3> k_lane_center_y{-120.0f, 0.0f, 120.0f};
constexpr std::array<float, 3> k_lane_velocity_x{72.0f, -48.0f, 60.0f};
constexpr double k_practice_scroll_speed_step = 0.25;
constexpr rhythm::TimelineMicroseconds k_calibration_adjust_step_microseconds = 1000;
constexpr rhythm::TimelineMicroseconds k_output_calibration_limit_microseconds = 150000;
constexpr rhythm::TimelineMicroseconds k_input_calibration_capture_window_microseconds = 160000;

const gameplay::ModeDescriptor k_descriptor{
    .id = "mode.reference.sandbox",
    .display_name = "Reference Sandbox",
    .description = "Reference mode that exercises lifecycle, input, transport control, and render extraction.",
};

void publish_practice_diagnostic(
    gameplay::IModeHost& host,
    std::uint64_t fixed_steps,
    std::string message);
void sync_practice_loop_markers_from_transport(
    const gameplay::TransportSnapshot& transport_snapshot,
    double& practice_loop_marker_start_seconds,
    double& practice_loop_marker_end_seconds,
    bool& practice_loop_marker_start_set,
    bool& practice_loop_marker_end_set) noexcept;

rhythm::TempoMapDefinition make_demo_tempo_map_definition() {
    return rhythm::TempoMapDefinition{
        .config = rhythm::TempoMapConfig{
            .ticks_per_quarter_note = 480,
            .sample_rate_hz = 48000,
        },
        .tempo_changes = {
            rhythm::TempoChange{
                .start_tick = 0,
                .microseconds_per_quarter_note = rhythm::microseconds_per_quarter_from_milli_bpm(120000),
            },
            rhythm::TempoChange{
                .start_tick = 1920,
                .microseconds_per_quarter_note = rhythm::microseconds_per_quarter_from_milli_bpm(150000),
            },
        },
        .time_signature_changes = {
            rhythm::TimeSignatureChange{
                .start_tick = 0,
                .numerator = 4,
                .denominator = 4,
            },
            rhythm::TimeSignatureChange{
                .start_tick = 3840,
                .numerator = 7,
                .denominator = 8,
            },
        },
        .stops = {
            rhythm::StopSegment{
                .start_tick = 1440,
                .duration_microseconds = 120000,
            },
        },
        .warps = {
            rhythm::WarpSegment{
                .start_tick = 4560,
                .duration_ticks = 480,
            },
        },
    };
}

std::string make_rhythm_status(const rhythm::TempoMap& tempo_map) {
    if (!tempo_map.valid()) {
        return std::string("tempo-map=invalid ") + std::string(tempo_map.last_error());
    }

    const rhythm::RhythmPosition warp_position = tempo_map.position_from_tick(5040);
    const rhythm::RhythmPosition second_position = tempo_map.position_from_seconds(1.0);
    std::ostringstream stream;
    stream << "tempo-map=ok tick@1s=" << second_position.tick << " bar@warp=" << warp_position.bar.bar_index
           << ':' << warp_position.bar.beat_index_in_bar << '+' << warp_position.bar.tick_offset_in_beat;
    return stream.str();
}

std::vector<rhythm::ScheduledCue> make_demo_cue_schedule() {
    return {
        rhythm::ScheduledCue{.hit_tick = 240, .channel_index = 0},
        rhythm::ScheduledCue{.hit_tick = 480, .channel_index = 1},
        rhythm::ScheduledCue{.hit_tick = 720, .channel_index = 2},
        rhythm::ScheduledCue{.hit_tick = 1200, .channel_index = 0},
        rhythm::ScheduledCue{.hit_tick = 1440, .channel_index = 1},
        rhythm::ScheduledCue{.hit_tick = 1920, .channel_index = 2},
        rhythm::ScheduledCue{.hit_tick = 2160, .channel_index = 0},
        rhythm::ScheduledCue{.hit_tick = 2640, .channel_index = 1},
        rhythm::ScheduledCue{.hit_tick = 3360, .channel_index = 2},
        rhythm::ScheduledCue{.hit_tick = 4080, .channel_index = 0},
        rhythm::ScheduledCue{.hit_tick = 4560, .channel_index = 1},
        rhythm::ScheduledCue{.hit_tick = 5040, .channel_index = 2},
        rhythm::ScheduledCue{.hit_tick = 5520, .channel_index = 0},
    };
}

rhythm::TimingOffsetProfile make_demo_timing_offsets() noexcept {
    return rhythm::TimingOffsetProfile{
        .manual_global_offset_microseconds = 8000,
    };
}

std::uint8_t calibration_attribute(CalibrationFlowMode mode) noexcept {
    switch (mode) {
    case CalibrationFlowMode::Output:
        return 0x0a;
    case CalibrationFlowMode::Input:
        return 0x0b;
    case CalibrationFlowMode::None:
        break;
    }

    return 0x08;
}

std::uint8_t judgement_attribute(rhythm::TimingJudgement judgement) noexcept {
    switch (judgement) {
    case rhythm::TimingJudgement::Perfect:
        return 0x0f;
    case rhythm::TimingJudgement::Great:
        return 0x0b;
    case rhythm::TimingJudgement::Good:
        return 0x0e;
    case rhythm::TimingJudgement::Miss:
        return 0x0c;
    case rhythm::TimingJudgement::None:
        break;
    }

    return 0x08;
}

void publish_calibration_diagnostic(
    gameplay::IModeHost& host,
    std::uint64_t fixed_steps,
    std::string message) {
    host.event_bus().publish(
        "mode.reference.sandbox",
        host.frame_timing().frame_index,
        fixed_steps,
        gameplay::DiagnosticEvent{
            .message = std::move(message),
        });
}

void apply_calibration_summary(
    const rhythm::LatencyCalibrationSession& session,
    rhythm::TimingOffsetProfile& offset_profile) noexcept {
    if (session.kind() == rhythm::LatencyCalibrationKind::AudioOutput) {
        offset_profile.audio_output_offset_microseconds = session.summary().recommended_offset_microseconds;
        return;
    }

    offset_profile.input_response_offset_microseconds = session.summary().recommended_offset_microseconds;
}

bool record_output_calibration_sample(
    gameplay::IModeHost& host,
    std::uint64_t fixed_steps,
    rhythm::LatencyCalibrationSession& output_calibration,
    rhythm::TimingOffsetProfile& offset_profile,
    rhythm::TimelineMicroseconds recommended_offset_microseconds) {
    if (!output_calibration.add_observation(
            rhythm::make_audio_output_calibration_observation(recommended_offset_microseconds))) {
        return false;
    }

    apply_calibration_summary(output_calibration, offset_profile);
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1)
           << "cal-output sample=" << output_calibration.summary().sample_count
           << " offset-ms=" << static_cast<double>(recommended_offset_microseconds) / 1000.0
           << " recommended-ms=" << static_cast<double>(output_calibration.summary().recommended_offset_microseconds) / 1000.0;
    publish_calibration_diagnostic(host, fixed_steps, stream.str());
    return true;
}

bool record_input_calibration_sample(
    gameplay::IModeHost& host,
    std::uint64_t fixed_steps,
    rhythm::LatencyCalibrationSession& input_calibration,
    rhythm::TimingOffsetProfile& offset_profile,
    rhythm::ChartTick cue_hit_tick,
    rhythm::TimelineMicroseconds input_time_microseconds,
    const rhythm::TempoMap& tempo_map) {
    if (!input_calibration.add_observation(
            rhythm::make_input_response_calibration_observation(
                tempo_map.microseconds_from_tick(cue_hit_tick),
                input_time_microseconds,
                offset_profile))) {
        return false;
    }

    apply_calibration_summary(input_calibration, offset_profile);
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1)
           << "cal-input sample=" << input_calibration.summary().sample_count
           << " recommended-ms=" << static_cast<double>(input_calibration.summary().recommended_offset_microseconds) / 1000.0
           << " cue-tick=" << cue_hit_tick;
    publish_calibration_diagnostic(host, fixed_steps, stream.str());
    return true;
}

const rhythm::ScheduledCue* nearest_capture_cue(
    std::span<const rhythm::ScheduledCue> scheduled_cues,
    const rhythm::TempoMap& tempo_map,
    rhythm::TimelineMicroseconds input_time_microseconds) noexcept {
    const rhythm::ScheduledCue* nearest_cue =
        rhythm::find_nearest_cue_by_time(scheduled_cues, tempo_map, input_time_microseconds);
    if (nearest_cue == nullptr) {
        return nullptr;
    }

    const rhythm::TimelineMicroseconds distance = std::llabs(
        tempo_map.microseconds_from_tick(nearest_cue->hit_tick) - input_time_microseconds);
    return distance <= k_input_calibration_capture_window_microseconds ? nearest_cue : nullptr;
}

bool input_calibration_ready(const rhythm::LatencyCalibrationSession& output_calibration) noexcept {
    return output_calibration.summary().stable;
}

void process_calibration_input(
    gameplay::IModeHost& host,
    std::uint64_t fixed_steps,
    const gameplay::TransportSnapshot& transport_snapshot,
    const rhythm::TempoMap& tempo_map,
    std::span<const rhythm::ScheduledCue> scheduled_cues,
    const platform::InputSnapshot& input_snapshot,
    const platform::InputBindingsConfig& input_bindings,
    CalibrationFlowMode& calibration_flow_mode,
    rhythm::LatencyCalibrationSession& output_calibration,
    rhythm::LatencyCalibrationSession& input_calibration,
    rhythm::TimingOffsetProfile& offset_profile,
    rhythm::TimelineMicroseconds& pending_output_offset_microseconds) {
    if (platform::was_action_pressed(input_snapshot, input_bindings, "calibration_output_mode")) {
        calibration_flow_mode = calibration_flow_mode == CalibrationFlowMode::Output
            ? CalibrationFlowMode::None
            : CalibrationFlowMode::Output;
        pending_output_offset_microseconds = offset_profile.audio_output_offset_microseconds;
        publish_calibration_diagnostic(
            host,
            fixed_steps,
            std::string("cal-mode=") + std::string(to_string(calibration_flow_mode)));
    }

    if (platform::was_action_pressed(input_snapshot, input_bindings, "calibration_input_mode")) {
        if (calibration_flow_mode != CalibrationFlowMode::Input &&
            !input_calibration_ready(output_calibration)) {
            publish_calibration_diagnostic(
                host,
                fixed_steps,
                "cal-input requires stable output calibration");
        } else {
            calibration_flow_mode = calibration_flow_mode == CalibrationFlowMode::Input
                ? CalibrationFlowMode::None
                : CalibrationFlowMode::Input;
            publish_calibration_diagnostic(
                host,
                fixed_steps,
                std::string("cal-mode=") + std::string(to_string(calibration_flow_mode)));
        }
    }

    if (platform::was_action_pressed(input_snapshot, input_bindings, "calibration_clear")) {
        if (calibration_flow_mode == CalibrationFlowMode::Input) {
            input_calibration.clear();
            offset_profile.input_response_offset_microseconds = 0;
            publish_calibration_diagnostic(host, fixed_steps, "cal-input cleared");
        } else {
            output_calibration.clear();
            offset_profile.audio_output_offset_microseconds = 0;
            pending_output_offset_microseconds = 0;
            publish_calibration_diagnostic(host, fixed_steps, "cal-output cleared");
        }
    }

    if (calibration_flow_mode == CalibrationFlowMode::Output) {
        if (platform::was_action_pressed(input_snapshot, input_bindings, "calibration_adjust_negative")) {
            pending_output_offset_microseconds = std::clamp(
                pending_output_offset_microseconds - k_calibration_adjust_step_microseconds,
                -k_output_calibration_limit_microseconds,
                k_output_calibration_limit_microseconds);
        }
        if (platform::was_action_pressed(input_snapshot, input_bindings, "calibration_adjust_positive")) {
            pending_output_offset_microseconds = std::clamp(
                pending_output_offset_microseconds + k_calibration_adjust_step_microseconds,
                -k_output_calibration_limit_microseconds,
                k_output_calibration_limit_microseconds);
        }
        if (platform::was_action_pressed(input_snapshot, input_bindings, "calibration_commit")) {
            (void)record_output_calibration_sample(
                host,
                fixed_steps,
                output_calibration,
                offset_profile,
                pending_output_offset_microseconds);
        }
        return;
    }

    if (calibration_flow_mode != CalibrationFlowMode::Input ||
        !platform::was_action_pressed(input_snapshot, input_bindings, "calibration_commit") ||
        !tempo_map.valid()) {
        return;
    }

    if (!input_calibration_ready(output_calibration)) {
        publish_calibration_diagnostic(
            host,
            fixed_steps,
            "cal-input requires stable output calibration");
        return;
    }

    const rhythm::TimelineMicroseconds input_time_microseconds =
        static_cast<rhythm::TimelineMicroseconds>(std::llround(transport_snapshot.position_seconds * 1000000.0));
    const rhythm::ScheduledCue* capture_cue = nearest_capture_cue(scheduled_cues, tempo_map, input_time_microseconds);
    if (capture_cue == nullptr) {
        publish_calibration_diagnostic(host, fixed_steps, "cal-input missed capture window");
        return;
    }

    (void)record_input_calibration_sample(
        host,
        fixed_steps,
        input_calibration,
        offset_profile,
        capture_cue->hit_tick,
        input_time_microseconds,
        tempo_map);
}

void run_demo_calibration_samples(
    gameplay::IModeHost& host,
    std::uint64_t fixed_steps,
    const rhythm::TempoMap& tempo_map,
    rhythm::LatencyCalibrationSession& output_calibration,
    rhythm::LatencyCalibrationSession& input_calibration,
    rhythm::TimingOffsetProfile& offset_profile,
    rhythm::TimelineMicroseconds& pending_output_offset_microseconds) {
    switch (fixed_steps) {
    case 5:
        pending_output_offset_microseconds = 9000;
        (void)record_output_calibration_sample(
            host,
            fixed_steps,
            output_calibration,
            offset_profile,
            pending_output_offset_microseconds);
        return;
    case 6:
        pending_output_offset_microseconds = 10000;
        (void)record_output_calibration_sample(
            host,
            fixed_steps,
            output_calibration,
            offset_profile,
            pending_output_offset_microseconds);
        return;
    case 7:
        pending_output_offset_microseconds = 11000;
        (void)record_output_calibration_sample(
            host,
            fixed_steps,
            output_calibration,
            offset_profile,
            pending_output_offset_microseconds);
        return;
    case 8:
        pending_output_offset_microseconds = 10000;
        (void)record_output_calibration_sample(
            host,
            fixed_steps,
            output_calibration,
            offset_profile,
            pending_output_offset_microseconds);
        return;
    case 12:
        (void)record_input_calibration_sample(
            host,
            fixed_steps,
            input_calibration,
            offset_profile,
            240,
            tempo_map.microseconds_from_tick(240) + 14000,
            tempo_map);
        return;
    case 13:
        (void)record_input_calibration_sample(
            host,
            fixed_steps,
            input_calibration,
            offset_profile,
            480,
            tempo_map.microseconds_from_tick(480) + 16000,
            tempo_map);
        return;
    case 14:
        (void)record_input_calibration_sample(
            host,
            fixed_steps,
            input_calibration,
            offset_profile,
            720,
            tempo_map.microseconds_from_tick(720) + 15000,
            tempo_map);
        return;
    case 15:
        (void)record_input_calibration_sample(
            host,
            fixed_steps,
            input_calibration,
            offset_profile,
            1200,
            tempo_map.microseconds_from_tick(1200) + 15000,
            tempo_map);
        return;
    default:
        break;
    }
}

rhythm::CueTravelWindow make_demo_cue_travel_window() noexcept {
    return rhythm::CueTravelWindow{
        .pre_hit_visible_ticks = 960,
        .post_hit_visible_ticks = 240,
    };
}

rhythm::LinearCueTravelPath make_lane_travel_path(std::uint32_t channel_index) noexcept {
    const std::size_t lane = static_cast<std::size_t>(channel_index % k_lane_root_x.size());
    const float hit_x = k_lane_root_x[lane];
    const bool moving_right = k_lane_velocity_x[lane] >= 0.0f;
    return rhythm::LinearCueTravelPath{
        .spawn_x = hit_x + (moving_right ? -180.0f : 180.0f),
        .hit_x = hit_x,
        .release_x = hit_x + (moving_right ? 96.0f : -96.0f),
    };
}

rhythm::LinearCueTravelPath make_practice_lane_travel_path(
    std::uint32_t channel_index,
    double practice_scroll_speed_multiplier) noexcept {
    return rhythm::scale_linear_cue_travel_path(
        make_lane_travel_path(channel_index),
        practice_scroll_speed_multiplier);
}

float sample_offset_visualization_x(
    const rhythm::TempoMap& tempo_map,
    const rhythm::RhythmPosition& current_position,
    rhythm::TimelineMicroseconds offset_microseconds,
    std::uint32_t channel_index,
    double practice_scroll_speed_multiplier) noexcept {
    const rhythm::ScheduledCue offset_cue{
        .hit_tick = tempo_map.tick_from_microseconds(current_position.microseconds + offset_microseconds),
        .channel_index = channel_index,
    };
    const rhythm::CueTravelState travel_state = rhythm::sample_cue_travel(
        tempo_map,
        current_position.tick,
        offset_cue,
        make_demo_cue_travel_window());
    return rhythm::sample_linear_cue_position_x(
        travel_state,
        make_practice_lane_travel_path(channel_index, practice_scroll_speed_multiplier));
}

render::Color4 scheduled_cue_color(std::uint32_t channel_index, rhythm::CueTravelPhase phase) noexcept {
    const float lane_tint = static_cast<float>(channel_index % 3u) * 0.18f;
    switch (phase) {
    case rhythm::CueTravelPhase::Approaching:
        return render::Color4{0.85f - lane_tint * 0.3f, 0.55f + lane_tint, 0.95f, 0.72f};
    case rhythm::CueTravelPhase::Release:
        return render::Color4{1.0f, 0.72f - lane_tint * 0.2f, 0.35f + lane_tint * 0.4f, 0.58f};
    case rhythm::CueTravelPhase::Hidden:
        break;
    }

    return render::Color4{};
}

void process_practice_input(
    gameplay::IModeHost& host,
    std::uint64_t fixed_steps,
    gameplay::ITransportControl& transport,
    const platform::InputSnapshot& input_snapshot,
    const platform::InputBindingsConfig& input_bindings,
    double& practice_scroll_speed_multiplier,
    bool& practice_offset_visualization_enabled,
    double& practice_loop_marker_start_seconds,
    double& practice_loop_marker_end_seconds,
    bool& practice_loop_marker_start_set,
    bool& practice_loop_marker_end_set) {
    if (platform::was_action_pressed(input_snapshot, input_bindings, "practice_speed_decrease")) {
        practice_scroll_speed_multiplier = rhythm::clamp_scroll_speed_multiplier(
            practice_scroll_speed_multiplier - k_practice_scroll_speed_step);
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(2)
               << "practice scroll=" << practice_scroll_speed_multiplier << 'x';
        publish_practice_diagnostic(host, fixed_steps, stream.str());
    }

    if (platform::was_action_pressed(input_snapshot, input_bindings, "practice_speed_increase")) {
        practice_scroll_speed_multiplier = rhythm::clamp_scroll_speed_multiplier(
            practice_scroll_speed_multiplier + k_practice_scroll_speed_step);
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(2)
               << "practice scroll=" << practice_scroll_speed_multiplier << 'x';
        publish_practice_diagnostic(host, fixed_steps, stream.str());
    }

    if (platform::was_action_pressed(input_snapshot, input_bindings, "practice_speed_reset")) {
        practice_scroll_speed_multiplier = 1.0;
        publish_practice_diagnostic(host, fixed_steps, "practice scroll reset=1.00x");
    }

    if (platform::was_action_pressed(input_snapshot, input_bindings, "practice_offset_visualization_toggle")) {
        practice_offset_visualization_enabled = !practice_offset_visualization_enabled;
        publish_practice_diagnostic(
            host,
            fixed_steps,
            std::string("practice offsets=") + (practice_offset_visualization_enabled ? "shown" : "hidden"));
    }

    const gameplay::TransportSnapshot& transport_snapshot = transport.snapshot();
    if (platform::was_action_pressed(input_snapshot, input_bindings, "practice_loop_mark_start")) {
        practice_loop_marker_start_seconds = transport_snapshot.position_seconds;
        practice_loop_marker_start_set = true;
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(3)
               << "practice loop-start=" << practice_loop_marker_start_seconds;
        publish_practice_diagnostic(host, fixed_steps, stream.str());
    }

    if (platform::was_action_pressed(input_snapshot, input_bindings, "practice_loop_mark_end")) {
        practice_loop_marker_end_seconds = transport_snapshot.position_seconds;
        practice_loop_marker_end_set = true;
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(3)
               << "practice loop-end=" << practice_loop_marker_end_seconds;
        publish_practice_diagnostic(host, fixed_steps, stream.str());
    }

    if (platform::was_action_pressed(input_snapshot, input_bindings, "practice_loop_apply")) {
        if (!practice_loop_marker_start_set || !practice_loop_marker_end_set) {
            publish_practice_diagnostic(host, fixed_steps, "practice loop apply ignored: missing marker");
        } else {
            const rhythm::PracticeLoopSegment loop_segment = rhythm::make_practice_loop_segment(
                practice_loop_marker_start_seconds,
                practice_loop_marker_end_seconds);
            if (!loop_segment.enabled) {
                publish_practice_diagnostic(host, fixed_steps, "practice loop apply ignored: invalid range");
            } else {
                transport.set_loop_region(loop_segment.start_seconds, loop_segment.end_seconds);
                sync_practice_loop_markers_from_transport(
                    transport.snapshot(),
                    practice_loop_marker_start_seconds,
                    practice_loop_marker_end_seconds,
                    practice_loop_marker_start_set,
                    practice_loop_marker_end_set);
                std::ostringstream stream;
                stream << std::fixed << std::setprecision(3)
                       << "practice loop=[" << loop_segment.start_seconds << ", "
                       << loop_segment.end_seconds << ']';
                publish_practice_diagnostic(host, fixed_steps, stream.str());
            }
        }
    }

    if (platform::was_action_pressed(input_snapshot, input_bindings, "practice_loop_clear")) {
        transport.clear_loop_region();
        sync_practice_loop_markers_from_transport(
            transport.snapshot(),
            practice_loop_marker_start_seconds,
            practice_loop_marker_end_seconds,
            practice_loop_marker_start_set,
            practice_loop_marker_end_set);
        publish_practice_diagnostic(host, fixed_steps, "practice loop cleared");
    }
}

void refresh_rhythm_debug_state(
    const rhythm::TempoMap& tempo_map,
    std::span<const rhythm::ScheduledCue> scheduled_cues,
    const rhythm::TimingWindowSet& judgement_window_set,
    const rhythm::TimingOffsetProfile& judgement_offset_profile,
    const gameplay::TransportSnapshot& transport_snapshot,
    rhythm::RhythmPosition& rhythm_position,
    rhythm::TimingJudgementResult& nearest_judgement,
    double& nearest_cue_timing_error_ms,
    std::size_t& visible_scheduled_cue_count) {
    rhythm_position = tempo_map.position_from_seconds(transport_snapshot.position_seconds);
    nearest_judgement = {};
    nearest_cue_timing_error_ms = 0.0;
    visible_scheduled_cue_count = 0;

    if (!tempo_map.valid()) {
        return;
    }

    const rhythm::CueTravelWindow travel_window = make_demo_cue_travel_window();
    if (const rhythm::ScheduledCue* nearest_cue =
            rhythm::find_nearest_cue_by_time(scheduled_cues, tempo_map, rhythm_position.microseconds);
        nearest_cue != nullptr) {
        nearest_judgement = rhythm::evaluate_timing_judgement(
            tempo_map,
            judgement_window_set,
            nearest_cue->hit_tick,
            rhythm_position.microseconds,
            judgement_offset_profile);
        nearest_cue_timing_error_ms = static_cast<double>(
            rhythm_position.microseconds - tempo_map.microseconds_from_tick(nearest_cue->hit_tick)) /
            1000.0;
    }

    for (const rhythm::ScheduledCue& scheduled_cue : scheduled_cues) {
        const rhythm::CueTravelState travel_state = rhythm::sample_cue_travel(
            tempo_map,
            rhythm_position.tick,
            scheduled_cue,
            travel_window);
        if (travel_state.visible) {
            ++visible_scheduled_cue_count;
        }
    }
}

std::uint32_t state_color(const gameplay::TransportSnapshot& transport_snapshot) noexcept {
    if (transport_snapshot.playback_mode == gameplay::TransportPlaybackMode::Preview &&
        transport_snapshot.playback_state != gameplay::TransportPlaybackState::Stopped) {
        return 0x1f3b66ff;
    }

    switch (transport_snapshot.playback_state) {
    case gameplay::TransportPlaybackState::Playing:
        return 0x1f4d2cff;
    case gameplay::TransportPlaybackState::Paused:
        return 0x7f5a1fff;
    case gameplay::TransportPlaybackState::Stopped:
        return 0x4a2430ff;
    }

    return 0x16324cff;
}

std::uint32_t mix_visual_color(const gameplay::TransportSnapshot& transport_snapshot, std::uint32_t visual_roll) noexcept {
    const std::uint32_t base = state_color(transport_snapshot);
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
    std::uint64_t collision_signature,
    rhythm::TimelineMicroseconds audio_output_offset_microseconds,
    rhythm::TimelineMicroseconds input_response_offset_microseconds,
    CalibrationFlowMode calibration_flow_mode,
    rhythm::TimelineMicroseconds pending_output_offset_microseconds,
    double practice_scroll_speed_multiplier,
    bool practice_offset_visualization_enabled,
    double practice_loop_marker_start_seconds,
    double practice_loop_marker_end_seconds,
    bool practice_loop_marker_start_set,
    bool practice_loop_marker_end_set) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    hash ^= fixed_steps;
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(transport_snapshot.playback_state);
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(transport_snapshot.playback_mode);
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(transport_snapshot.position_authority);
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(std::llround(transport_snapshot.position_seconds * 1000000.0));
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(std::llround(transport_snapshot.playback_rate * 1000000.0));
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(transport_snapshot.loop_region.enabled);
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(std::llround(transport_snapshot.loop_region.start_seconds * 1000000.0));
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(std::llround(transport_snapshot.loop_region.end_seconds * 1000000.0));
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(transport_snapshot.preview_region.enabled);
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(std::llround(transport_snapshot.preview_region.start_seconds * 1000000.0));
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(std::llround(transport_snapshot.preview_region.end_seconds * 1000000.0));
    hash *= 1099511628211ull;
    hash ^= transport_snapshot.completed_loops;
    hash *= 1099511628211ull;
    hash ^= transport_snapshot.completed_previews;
    hash *= 1099511628211ull;
    hash ^= transport_snapshot.discontinuity.timeline_revision;
    hash *= 1099511628211ull;
    hash ^= transport_snapshot.discontinuity.discontinuity_count;
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(transport_snapshot.discontinuity.last_reason);
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(std::llround(transport_snapshot.discontinuity.last_from_seconds * 1000000.0));
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(std::llround(transport_snapshot.discontinuity.last_to_seconds * 1000000.0));
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
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(audio_output_offset_microseconds);
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(input_response_offset_microseconds);
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(calibration_flow_mode);
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(pending_output_offset_microseconds);
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(static_cast<std::int64_t>(
        std::llround(practice_scroll_speed_multiplier * 1000000.0)));
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(practice_offset_visualization_enabled);
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(practice_loop_marker_start_set);
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(practice_loop_marker_end_set);
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(static_cast<std::int64_t>(
        std::llround(practice_loop_marker_start_seconds * 1000000.0)));
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(static_cast<std::int64_t>(
        std::llround(practice_loop_marker_end_seconds * 1000000.0)));
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
    double practice_scroll_speed_multiplier{1.25};
    bool practice_offset_visualization_enabled{true};
    double practice_loop_start_seconds{0.75};
    double practice_loop_end_seconds{1.25};
    std::string cue_material_authoring_id{"reference.sandbox.material.cue"};
    std::string debug_font_authoring_id{"reference.sandbox.font.debug"};
};

SandboxModeConfig load_sandbox_mode_config(const gameplay::ModeConfigurationStore& store) {
    const gameplay::ModeConfigurationView view = store.view(k_descriptor.id);
    SandboxModeConfig config{};
    config.velocity_scale = static_cast<float>(view.get_double("velocity_scale", config.velocity_scale));
    config.hit_window_half_width = static_cast<float>(view.get_double("hit_window_half_width", config.hit_window_half_width));
    config.hit_window_half_height = static_cast<float>(view.get_double("hit_window_half_height", config.hit_window_half_height));
    config.practice_scroll_speed_multiplier =
        rhythm::clamp_scroll_speed_multiplier(
            view.get_double("practice_scroll_speed_multiplier", config.practice_scroll_speed_multiplier));
    config.practice_offset_visualization_enabled =
        view.get_bool("practice_offset_visualization_enabled", config.practice_offset_visualization_enabled);
    config.practice_loop_start_seconds =
        view.get_double("practice_loop_start_seconds", config.practice_loop_start_seconds);
    config.practice_loop_end_seconds =
        view.get_double("practice_loop_end_seconds", config.practice_loop_end_seconds);
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

void publish_practice_diagnostic(
    gameplay::IModeHost& host,
    std::uint64_t fixed_steps,
    std::string message) {
    host.event_bus().publish(
        "mode.reference.sandbox",
        host.frame_timing().frame_index,
        fixed_steps,
        gameplay::DiagnosticEvent{
            .message = std::move(message),
        });
}

void sync_practice_loop_markers_from_transport(
    const gameplay::TransportSnapshot& transport_snapshot,
    double& practice_loop_marker_start_seconds,
    double& practice_loop_marker_end_seconds,
    bool& practice_loop_marker_start_set,
    bool& practice_loop_marker_end_set) noexcept {
    if (!transport_snapshot.loop_region.enabled) {
        practice_loop_marker_start_seconds = 0.0;
        practice_loop_marker_end_seconds = 0.0;
        practice_loop_marker_start_set = false;
        practice_loop_marker_end_set = false;
        return;
    }

    practice_loop_marker_start_seconds = transport_snapshot.loop_region.start_seconds;
    practice_loop_marker_end_seconds = transport_snapshot.loop_region.end_seconds;
    practice_loop_marker_start_set = true;
    practice_loop_marker_end_set = true;
}

void apply_practice_speed_to_world(
    gameplay::WorldModel& world,
    float configured_velocity_scale,
    double practice_scroll_speed_multiplier) {
    const double clamped_multiplier = rhythm::clamp_scroll_speed_multiplier(practice_scroll_speed_multiplier);
    world.for_each<gameplay::LinearVelocity2D, SandboxLaneCue>(
        [&](gameplay::WorldEntity, gameplay::LinearVelocity2D& velocity, const SandboxLaneCue& lane) {
            const std::size_t lane_index = static_cast<std::size_t>(lane.lane_index % k_lane_velocity_x.size());
            velocity.units_per_second.x =
                k_lane_velocity_x[lane_index] * configured_velocity_scale * static_cast<float>(clamped_multiplier);
            velocity.units_per_second.y = 0.0f;
        });
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
            .playback_mode = transport_snapshot.playback_mode,
            .position_authority = transport_snapshot.position_authority,
            .position_seconds = transport_snapshot.position_seconds,
            .loop_enabled = transport_snapshot.loop_region.enabled,
            .preview_enabled = transport_snapshot.preview_region.enabled,
            .timeline_revision = transport_snapshot.discontinuity.timeline_revision,
            .discontinuity_reason = transport_snapshot.discontinuity.last_reason,
        });
}

void publish_transport_discontinuity_diagnostic(
    gameplay::IModeHost& host,
    std::uint64_t fixed_steps,
    const gameplay::TransportSnapshot& transport_snapshot,
    std::uint64_t& last_published_transport_revision) {
    if (transport_snapshot.discontinuity.timeline_revision == 0u ||
        transport_snapshot.discontinuity.timeline_revision == last_published_transport_revision) {
        return;
    }

    std::ostringstream stream;
    stream << "transport rev=" << transport_snapshot.discontinuity.timeline_revision
           << " count=" << transport_snapshot.discontinuity.discontinuity_count
           << " reason=" << gameplay::to_string(transport_snapshot.discontinuity.last_reason)
           << " from=" << transport_snapshot.discontinuity.last_from_seconds
           << " to=" << transport_snapshot.discontinuity.last_to_seconds;
    host.event_bus().publish(
        "mode.reference.sandbox",
        host.frame_timing().frame_index,
        fixed_steps,
        gameplay::DiagnosticEvent{
            .message = stream.str(),
        });
    last_published_transport_revision = transport_snapshot.discontinuity.timeline_revision;
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
    foundation::ResourceHandle& cue_texture_handle,
    std::string& cue_texture_runtime_label,
    foundation::ResourceHandle& cue_material_handle,
    std::string& cue_material_runtime_label,
    foundation::ResourceHandle& rig_mesh_handle,
    std::string& rig_mesh_runtime_label,
    foundation::ResourceHandle& debug_font_handle,
    std::string& debug_font_runtime_label,
    bool& stale_debug_font_borrow_valid,
    std::string_view cue_material_authoring_id,
    std::string_view debug_font_authoring_id) {
    (void)resource_registry.register_resource(
        foundation::ResourceKind::Material,
        "reference.sandbox.material.cue",
        "runtime.material.cue");
    (void)resource_registry.register_resource(
        foundation::ResourceKind::Material,
        "reference.sandbox.material.hit-window",
        "runtime.material.hit-window");
    (void)resource_registry.register_resource(
        foundation::ResourceKind::ShaderProgram,
        "reference.sandbox.shader.basic",
        "runtime.shader.basic");

    cue_texture_runtime_label.clear();
    const foundation::BorrowedResourceRecord cue_texture = resource_registry.find_borrow(
        foundation::ResourceKind::Texture,
        "reference.sandbox.texture.cue");
    cue_texture_handle = cue_texture.handle();
    if (cue_texture) {
        cue_texture_runtime_label = cue_texture->runtime_label;
    }

    rig_mesh_runtime_label.clear();
    const foundation::BorrowedResourceRecord rig_mesh = resource_registry.find_borrow(
        foundation::ResourceKind::Mesh,
        "reference.sandbox.mesh.rig");
    rig_mesh_handle = rig_mesh.handle();
    if (rig_mesh) {
        rig_mesh_runtime_label = rig_mesh->runtime_label;
    }

    const foundation::ResourceHandle stale_font_handle = resource_registry.register_resource(
        foundation::ResourceKind::Font,
        "reference.sandbox.font.debug.hot-reload",
        "runtime.font.debug.hot-reload.v1");
    const foundation::BorrowedResourceRecord stale_font_borrow = resource_registry.borrow(stale_font_handle);
    (void)resource_registry.register_resource(
        foundation::ResourceKind::Font,
        "reference.sandbox.font.debug.hot-reload",
        "runtime.font.debug.hot-reload.v2");

    cue_material_runtime_label.clear();
    const foundation::BorrowedResourceRecord cue_material = resource_registry.find_borrow(
        foundation::ResourceKind::Material,
        cue_material_authoring_id);
    cue_material_handle = cue_material.handle();
    if (cue_material) {
        cue_material_runtime_label = cue_material->runtime_label;
    }

    debug_font_runtime_label.clear();
    const foundation::BorrowedResourceRecord debug_font = resource_registry.find_borrow(
        foundation::ResourceKind::Font,
        debug_font_authoring_id);
    debug_font_handle = debug_font.handle();
    if (debug_font) {
        debug_font_runtime_label = debug_font->runtime_label;
    }

    stale_debug_font_borrow_valid = static_cast<bool>(stale_font_borrow);
    resource_summary = resource_registry.summary();
}

void publish_mode_config_diagnostic(
    gameplay::IModeHost& host,
    std::uint64_t fixed_steps,
    const SandboxModeConfig& config) {
    std::ostringstream stream;
    stream << "mode-config speed=" << config.velocity_scale << " hit=" << config.hit_window_half_width << 'x'
           << config.hit_window_half_height << " practice-scroll=" << config.practice_scroll_speed_multiplier
           << " practice-loop=[" << config.practice_loop_start_seconds << ", "
           << config.practice_loop_end_seconds << "] offsets="
           << config.practice_offset_visualization_enabled << " cue-id=" << config.cue_material_authoring_id
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
    foundation::ResourceHandle cue_texture_handle,
    std::string_view cue_texture_runtime_label,
    foundation::ResourceHandle cue_material_handle,
    std::string_view cue_material_runtime_label,
    foundation::ResourceHandle rig_mesh_handle,
    std::string_view rig_mesh_runtime_label,
    foundation::ResourceHandle debug_font_handle,
    std::string_view debug_font_runtime_label,
    bool stale_debug_font_borrow_valid) {
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
               << " cue-texture=" << cue_texture_handle.value() << '/' << cue_texture_runtime_label
               << " cue-material=" << cue_material_handle.value() << '/' << cue_material_runtime_label
               << " rig-mesh=" << rig_mesh_handle.value() << '/' << rig_mesh_runtime_label
               << " debug-font=" << debug_font_handle.value() << '/' << debug_font_runtime_label
               << " stale-borrow=" << stale_debug_font_borrow_valid;
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

    constexpr std::array<float, 3> k_phase_velocity{0.07f, 0.11f, 0.05f};
    constexpr std::array<float, 3> k_angular_velocity{0.35f, -0.28f, 0.22f};

    for (std::size_t index = 0; index < k_lane_root_x.size(); ++index) {
        const gameplay::WorldEntity lane_root = world.create_entity("reference.sandbox.lane-root");
        world.emplace<gameplay::LocalTransform2D>(
            lane_root,
            gameplay::LocalTransform2D{
                .translation = {
                    .x = k_lane_root_x[index],
                    .y = k_lane_center_y[index],
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
            gameplay::LinearVelocity2D{.units_per_second = {.x = k_lane_velocity_x[index] * velocity_scale, .y = 0.0f}});
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
    configured_calibration_output_binding_ = "keyboard:O";
    configured_calibration_input_binding_ = "keyboard:I";
    configured_calibration_commit_binding_ = "keyboard:Return";
    configured_calibration_clear_binding_ = "keyboard:Backspace";
    configured_calibration_adjust_negative_binding_ = "keyboard:Left";
    configured_calibration_adjust_positive_binding_ = "keyboard:Right";
    configured_practice_speed_decrease_binding_ = "keyboard:Z";
    configured_practice_speed_increase_binding_ = "keyboard:X";
    configured_practice_speed_reset_binding_ = "keyboard:C";
    configured_practice_loop_mark_start_binding_ = "keyboard:J";
    configured_practice_loop_mark_end_binding_ = "keyboard:K";
    configured_practice_loop_apply_binding_ = "keyboard:L";
    configured_practice_loop_clear_binding_ = "keyboard:U";
    configured_practice_offset_visualization_toggle_binding_ = "keyboard:V";
    configured_velocity_scale_ = 1.0f;
    configured_hit_window_half_width_ = 32.0f;
    configured_hit_window_half_height_ = 24.0f;
    configured_cue_material_authoring_id_ = "reference.sandbox.material.cue";
    configured_debug_font_authoring_id_ = "reference.sandbox.font.debug";
    resource_summary_ = {};
    cue_texture_handle_ = {};
    cue_texture_runtime_label_.clear();
    cue_material_handle_ = {};
    cue_material_runtime_label_.clear();
    rig_mesh_handle_ = {};
    rig_mesh_runtime_label_.clear();
    debug_font_handle_ = {};
    debug_font_runtime_label_.clear();
    stale_debug_font_borrow_valid_ = false;
    world_entity_count_ = 0;
    average_phase_ = 0.0f;
    sample_cue_world_x_ = 0.0f;
    sample_tip_world_ = {};
    collision_signature_ = 0;
    collision_topology_ = 0;
    last_published_collision_topology_ = static_cast<std::uint64_t>(-1);
    last_published_transport_revision_ = 0;
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
    configured_calibration_output_binding_ = describe_binding(
        host.input_bindings(),
        "calibration_output_mode",
        configured_calibration_output_binding_);
    configured_calibration_input_binding_ = describe_binding(
        host.input_bindings(),
        "calibration_input_mode",
        configured_calibration_input_binding_);
    configured_calibration_commit_binding_ = describe_binding(
        host.input_bindings(),
        "calibration_commit",
        configured_calibration_commit_binding_);
    configured_calibration_clear_binding_ = describe_binding(
        host.input_bindings(),
        "calibration_clear",
        configured_calibration_clear_binding_);
    configured_calibration_adjust_negative_binding_ = describe_binding(
        host.input_bindings(),
        "calibration_adjust_negative",
        configured_calibration_adjust_negative_binding_);
    configured_calibration_adjust_positive_binding_ = describe_binding(
        host.input_bindings(),
        "calibration_adjust_positive",
        configured_calibration_adjust_positive_binding_);
    configured_practice_speed_decrease_binding_ = describe_binding(
        host.input_bindings(),
        "practice_speed_decrease",
        configured_practice_speed_decrease_binding_);
    configured_practice_speed_increase_binding_ = describe_binding(
        host.input_bindings(),
        "practice_speed_increase",
        configured_practice_speed_increase_binding_);
    configured_practice_speed_reset_binding_ = describe_binding(
        host.input_bindings(),
        "practice_speed_reset",
        configured_practice_speed_reset_binding_);
    configured_practice_loop_mark_start_binding_ = describe_binding(
        host.input_bindings(),
        "practice_loop_mark_start",
        configured_practice_loop_mark_start_binding_);
    configured_practice_loop_mark_end_binding_ = describe_binding(
        host.input_bindings(),
        "practice_loop_mark_end",
        configured_practice_loop_mark_end_binding_);
    configured_practice_loop_apply_binding_ = describe_binding(
        host.input_bindings(),
        "practice_loop_apply",
        configured_practice_loop_apply_binding_);
    configured_practice_loop_clear_binding_ = describe_binding(
        host.input_bindings(),
        "practice_loop_clear",
        configured_practice_loop_clear_binding_);
    configured_practice_offset_visualization_toggle_binding_ = describe_binding(
        host.input_bindings(),
        "practice_offset_visualization_toggle",
        configured_practice_offset_visualization_toggle_binding_);

    rhythm_tempo_map_.clear();
    (void)rhythm_tempo_map_.rebuild(make_demo_tempo_map_definition());
    rhythm_status_ = make_rhythm_status(rhythm_tempo_map_);
    scheduled_cues_ = make_demo_cue_schedule();
    judgement_window_set_ = rhythm::make_default_timing_window_set();
    judgement_offset_profile_ = make_demo_timing_offsets();
    practice_scroll_speed_multiplier_ = mode_config.practice_scroll_speed_multiplier;
    practice_offset_visualization_enabled_ = mode_config.practice_offset_visualization_enabled;
    practice_loop_marker_start_seconds_ = 0.0;
    practice_loop_marker_end_seconds_ = 0.0;
    practice_loop_marker_start_set_ = false;
    practice_loop_marker_end_set_ = false;
    calibration_flow_mode_ = CalibrationFlowMode::None;
    output_latency_calibration_.clear();
    input_latency_calibration_.clear();
    pending_output_offset_microseconds_ = judgement_offset_profile_.audio_output_offset_microseconds;
    nearest_judgement_ = {};
    nearest_cue_timing_error_ms_ = 0.0;
    visible_scheduled_cue_count_ = 0;

    seed_resources(
        host.resource_registry(),
        resource_summary_,
        cue_texture_handle_,
        cue_texture_runtime_label_,
        cue_material_handle_,
        cue_material_runtime_label_,
        rig_mesh_handle_,
        rig_mesh_runtime_label_,
        debug_font_handle_,
        debug_font_runtime_label_,
        stale_debug_font_borrow_valid_,
        configured_cue_material_authoring_id_,
        configured_debug_font_authoring_id_);
    seed_world(
        host,
        configured_velocity_scale_,
        configured_hit_window_half_width_,
        configured_hit_window_half_height_);
    apply_practice_speed_to_world(
        host.world_model(),
        configured_velocity_scale_,
        practice_scroll_speed_multiplier_);
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
        cue_texture_handle_,
        cue_texture_runtime_label_,
        cue_material_handle_,
        cue_material_runtime_label_,
        rig_mesh_handle_,
        rig_mesh_runtime_label_,
        debug_font_handle_,
        debug_font_runtime_label_,
        stale_debug_font_borrow_valid_);
    publish_mode_config_diagnostic(host, fixed_steps_, mode_config);
    publish_input_binding_diagnostic(
        host,
        fixed_steps_,
        configured_transport_pause_binding_,
        configured_transport_restart_binding_);
    publish_calibration_diagnostic(
        host,
        fixed_steps_,
        "cal-bindings out=" + configured_calibration_output_binding_ +
            " input=" + configured_calibration_input_binding_ +
            " sample=" + configured_calibration_commit_binding_ +
            " clear=" + configured_calibration_clear_binding_ +
            " adjust=" + configured_calibration_adjust_negative_binding_ + "/" +
            configured_calibration_adjust_positive_binding_);
    publish_practice_diagnostic(
        host,
        fixed_steps_,
        "practice-bindings speed=" + configured_practice_speed_decrease_binding_ + "/" +
            configured_practice_speed_increase_binding_ + "/" + configured_practice_speed_reset_binding_ +
            " loop=" + configured_practice_loop_mark_start_binding_ + "/" +
            configured_practice_loop_mark_end_binding_ + "/" + configured_practice_loop_apply_binding_ +
            " clear=" + configured_practice_loop_clear_binding_ +
            " vis=" + configured_practice_offset_visualization_toggle_binding_);

    gameplay::ITransportControl& transport = host.transport();
    transport.stop();
    const rhythm::PracticeLoopSegment practice_loop_segment = rhythm::make_practice_loop_segment(
        mode_config.practice_loop_start_seconds,
        mode_config.practice_loop_end_seconds);
    if (practice_loop_segment.enabled) {
        transport.set_loop_region(practice_loop_segment.start_seconds, practice_loop_segment.end_seconds);
    } else {
        transport.clear_loop_region();
    }
    transport.play();
    sync_practice_loop_markers_from_transport(
        transport.snapshot(),
        practice_loop_marker_start_seconds_,
        practice_loop_marker_end_seconds_,
        practice_loop_marker_start_set_,
        practice_loop_marker_end_set_);

    host.random_service().reset_streams();
    const gameplay::TransportSnapshot& transport_snapshot = transport.snapshot();
    refresh_rhythm_debug_state(
        rhythm_tempo_map_,
        scheduled_cues_,
        judgement_window_set_,
        judgement_offset_profile_,
        transport_snapshot,
        rhythm_position_,
        nearest_judgement_,
        nearest_cue_timing_error_ms_,
        visible_scheduled_cue_count_);
    const std::uint64_t checkpoint_hash = make_state_hash(
        fixed_steps_,
        transport_snapshot,
        transport_roll_,
        visual_roll_,
        average_phase_,
        sample_cue_world_x_,
        sample_tip_world_,
        collision_signature_,
        judgement_offset_profile_.audio_output_offset_microseconds,
        judgement_offset_profile_.input_response_offset_microseconds,
        calibration_flow_mode_,
        pending_output_offset_microseconds_,
        practice_scroll_speed_multiplier_,
        practice_offset_visualization_enabled_,
        practice_loop_marker_start_seconds_,
        practice_loop_marker_end_seconds_,
        practice_loop_marker_start_set_,
        practice_loop_marker_end_set_);
    host.replay().record_checkpoint(gameplay::ReplayCheckpoint{
        .frame_index = host.frame_timing().frame_index,
        .simulation_step = fixed_steps_,
        .transport_state = transport_snapshot.playback_state,
        .transport_position_authority = transport_snapshot.position_authority,
        .transport_position_seconds = transport_snapshot.position_seconds,
        .transport_timeline_revision = transport_snapshot.discontinuity.timeline_revision,
        .transport_discontinuity_reason = transport_snapshot.discontinuity.last_reason,
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
            .message = "world seeded entities=" + std::to_string(world_entity_count_) + " " + rhythm_status_,
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
        transport.preview(0.20, 0.22);
        action = "preview";
    } else if (fixed_steps_ == 3) {
        transport.pause();
        action = "pause";
    } else if (fixed_steps_ == 4) {
        transport.play();
        action = "play";
    } else if (fixed_steps_ == 8) {
        transport.clear_loop_region();
        action = "clear-loop";
    } else if (fixed_steps_ == 9) {
        transport.set_loop_region(1.0, 1.4);
        action = "set-loop";
    } else if (fixed_steps_ == 10) {
        transport.seek(1.10);
        action = "seek";
    } else if (fixed_steps_ == 11) {
        transport.restart();
        action = "restart";
    }

    if (fixed_steps_ == 8 || fixed_steps_ == 9) {
        sync_practice_loop_markers_from_transport(
            transport.snapshot(),
            practice_loop_marker_start_seconds_,
            practice_loop_marker_end_seconds_,
            practice_loop_marker_start_set_,
            practice_loop_marker_end_set_);
    }

    apply_practice_speed_to_world(
        host.world_model(),
        configured_velocity_scale_,
        practice_scroll_speed_multiplier_);
    update_world(host.world_model());
    motion_report_ = gameplay::integrate_motion(host.world_model(), fixed_delta_seconds);

    const gameplay::TransportSnapshot& transport_snapshot = transport.snapshot();
    run_demo_calibration_samples(
        host,
        fixed_steps_,
        rhythm_tempo_map_,
        output_latency_calibration_,
        input_latency_calibration_,
        judgement_offset_profile_,
        pending_output_offset_microseconds_);
    refresh_rhythm_debug_state(
        rhythm_tempo_map_,
        scheduled_cues_,
        judgement_window_set_,
        judgement_offset_profile_,
        transport_snapshot,
        rhythm_position_,
        nearest_judgement_,
        nearest_cue_timing_error_ms_,
        visible_scheduled_cue_count_);
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
    publish_transport_discontinuity_diagnostic(
        host,
        fixed_steps_,
        transport_snapshot,
        last_published_transport_revision_);

    const std::uint64_t checkpoint_hash = make_state_hash(
        fixed_steps_,
        transport_snapshot,
        transport_roll_,
        visual_roll_,
        average_phase_,
        sample_cue_world_x_,
        sample_tip_world_,
        collision_signature_,
        judgement_offset_profile_.audio_output_offset_microseconds,
        judgement_offset_profile_.input_response_offset_microseconds,
        calibration_flow_mode_,
        pending_output_offset_microseconds_,
        practice_scroll_speed_multiplier_,
        practice_offset_visualization_enabled_,
        practice_loop_marker_start_seconds_,
        practice_loop_marker_end_seconds_,
        practice_loop_marker_start_set_,
        practice_loop_marker_end_set_);
    host.replay().record_checkpoint(gameplay::ReplayCheckpoint{
        .frame_index = host.frame_timing().frame_index,
        .simulation_step = fixed_steps_,
        .transport_state = transport_snapshot.playback_state,
        .transport_position_authority = transport_snapshot.position_authority,
        .transport_position_seconds = transport_snapshot.position_seconds,
        .transport_timeline_revision = transport_snapshot.discontinuity.timeline_revision,
        .transport_discontinuity_reason = transport_snapshot.discontinuity.last_reason,
        .root_random_seed = host.random_service().root_seed(),
        .authoritative_state_hash = checkpoint_hash,
        .label = "fixed-step",
        .summary = "sandbox transport/RNG/world state checkpoint",
    });
    publish_transport_event(host, action, fixed_steps_, transport_snapshot);
    publish_replay_checkpoint_event(host, "fixed-step", fixed_steps_, checkpoint_hash);

    foundation::TelemetrySnapshot snapshot{};
    snapshot.audio_drift_ms = host.transport().diagnostics().drift_seconds * 1000.0;
    snapshot.visible_cues = static_cast<std::uint32_t>(std::max<std::size_t>(3u, visible_scheduled_cue_count_));
    snapshot.draw_calls = 0;
    host.telemetry().record(snapshot);
}

void ReferenceSandboxMode::on_render_extract(gameplay::IModeHost& host, double interpolation_alpha) {
    gameplay::ITransportControl& transport = host.transport();
    const gameplay::TransportSnapshot& transport_snapshot = transport.snapshot();
    const platform::InputSnapshot& input_snapshot = host.input_snapshot();
    process_practice_input(
        host,
        fixed_steps_,
        transport,
        input_snapshot,
        host.input_bindings(),
        practice_scroll_speed_multiplier_,
        practice_offset_visualization_enabled_,
        practice_loop_marker_start_seconds_,
        practice_loop_marker_end_seconds_,
        practice_loop_marker_start_set_,
        practice_loop_marker_end_set_);
    process_calibration_input(
        host,
        fixed_steps_,
        transport_snapshot,
        rhythm_tempo_map_,
        scheduled_cues_,
        input_snapshot,
        host.input_bindings(),
        calibration_flow_mode_,
        output_latency_calibration_,
        input_latency_calibration_,
        judgement_offset_profile_,
        pending_output_offset_microseconds_);
    refresh_rhythm_debug_state(
        rhythm_tempo_map_,
        scheduled_cues_,
        judgement_window_set_,
        judgement_offset_profile_,
        transport_snapshot,
        rhythm_position_,
        nearest_judgement_,
        nearest_cue_timing_error_ms_,
        visible_scheduled_cue_count_);
    const foundation::DeterministicRandomService& random_service = host.random_service();
    const foundation::DeterministicRng* transport_rng = random_service.find_stream("reference-sandbox.transport");
    const foundation::DeterministicRng* visual_rng = random_service.find_stream("reference-sandbox.visual");
    const gameplay::EventBus& event_bus = host.event_bus();
    const gameplay::EventRecord* last_event = event_bus.last();
    const gameplay::ReplayRecorder& replay_recorder = host.replay();
    const gameplay::ReplayCheckpoint* last_checkpoint = replay_recorder.last_checkpoint();
    const gameplay::TransportDiagnostics& transport_diagnostics = transport.diagnostics();

    host.render_extraction().set_view_camera(
        reaktio::render::RenderView::MainScene,
        reaktio::render::OrthographicCamera2D{
            .center = {0.0f, 0.0f},
            .virtual_height = 720.0f,
            .view_distance = 10.0f,
            .near_plane = 0.0f,
            .far_plane = 100.0f,
        });
    host.render_extraction().set_main_scene_clear(mix_visual_color(transport_snapshot, visual_roll_));

    std::ostringstream state_stream;
    state_stream << "transport=" << gameplay::to_string(transport_snapshot.playback_state) << '/'
                 << gameplay::to_string(transport_snapshot.playback_mode) << '/'
                 << gameplay::to_string(transport_snapshot.position_authority) << " pos="
                 << transport_snapshot.position_seconds << '/' << transport_snapshot.duration_seconds << "s rev="
                 << transport_snapshot.discontinuity.timeline_revision << " alpha="
                 << interpolation_alpha;
    host.render_extraction().add_debug_text(0, 8, 0x0f, state_stream.str());

    std::ostringstream loop_stream;
    loop_stream << "loop=" << transport_snapshot.loop_region.enabled << " ["
                << transport_snapshot.loop_region.start_seconds << ", "
                << transport_snapshot.loop_region.end_seconds << "] loops="
                << transport_snapshot.completed_loops << " preview="
                << transport_snapshot.preview_region.enabled << " ["
                << transport_snapshot.preview_region.start_seconds << ", "
                << transport_snapshot.preview_region.end_seconds << "] previews="
                << transport_snapshot.completed_previews << " jump="
                << gameplay::to_string(transport_snapshot.discontinuity.last_reason) << '('
                << transport_snapshot.discontinuity.last_from_seconds << "->"
                << transport_snapshot.discontinuity.last_to_seconds << ") fixed-steps="
                << transport_snapshot.advanced_fixed_steps;
    host.render_extraction().add_debug_text(0, 9, 0x0e, loop_stream.str());

    const double drift_milliseconds = transport_diagnostics.drift_seconds * 1000.0;
    const double absolute_drift_milliseconds = std::abs(drift_milliseconds);
    const std::uint8_t inspector_attribute = absolute_drift_milliseconds >=
            transport_diagnostics.correction_policy.hard_snap_threshold_seconds * 1000.0
        ? 0x0c
        : (absolute_drift_milliseconds >= transport_diagnostics.correction_policy.soft_correction_threshold_seconds * 1000.0
            ? 0x0e
            : 0x0f);
    std::ostringstream inspector_stream;
    inspector_stream << std::fixed << std::setprecision(3)
                     << "inspector audio=" << transport_diagnostics.authoritative_position_seconds
                     << "s sim=" << transport_diagnostics.simulation_position_seconds
                     << "s drift-ms=" << drift_milliseconds
                     << " reported=" << transport_diagnostics.reported_output_position_seconds
                     << "s lat-ms=" << (transport_diagnostics.total_output_latency_seconds * 1000.0);
    host.render_extraction().add_debug_text(0, 10, inspector_attribute, inspector_stream.str());

    std::ostringstream correction_stream;
    correction_stream << std::fixed << std::setprecision(3)
                      << "corr count=" << transport_diagnostics.correction_count
                      << " soft-ms=" << (transport_diagnostics.correction_policy.soft_correction_threshold_seconds * 1000.0)
                      << " hard-ms=" << (transport_diagnostics.correction_policy.hard_snap_threshold_seconds * 1000.0)
                      << " step-ms=" << (transport_diagnostics.correction_policy.max_soft_correction_step_seconds * 1000.0);
    if (transport_diagnostics.recent_correction_count > 0u) {
        const gameplay::TransportCorrectionEvent& last_correction = transport_diagnostics.recent_corrections[0];
        correction_stream << " last=" << gameplay::to_string(last_correction.correction_type)
                          << " drift-ms=" << (last_correction.drift_before_seconds * 1000.0)
                          << " apply-ms=" << (last_correction.correction_applied_seconds * 1000.0);
    }
    host.render_extraction().add_debug_text(0, 11, 0x08, correction_stream.str());

    const rhythm::LatencyCalibrationSummary& output_calibration_summary = output_latency_calibration_.summary();
    const rhythm::LatencyCalibrationSummary& input_calibration_summary = input_latency_calibration_.summary();
    const rhythm::PracticeOffsetSummary practice_offset_summary =
        rhythm::summarize_practice_offsets(judgement_offset_profile_);
    std::ostringstream calibration_stream;
    calibration_stream << std::fixed << std::setprecision(1)
                       << "cal mode=" << to_string(calibration_flow_mode_)
                       << " out-ms=" << static_cast<double>(output_calibration_summary.recommended_offset_microseconds) / 1000.0
                       << "(" << output_calibration_summary.sample_count << (output_calibration_summary.stable ? '*' : '-') << ")"
                       << " in-ms=" << static_cast<double>(input_calibration_summary.recommended_offset_microseconds) / 1000.0
                       << "(" << input_calibration_summary.sample_count << (input_calibration_summary.stable ? '*' : '-') << ")"
                       << " pending-ms=" << static_cast<double>(pending_output_offset_microseconds_) / 1000.0;
    host.render_extraction().add_debug_text(0, 12, calibration_attribute(calibration_flow_mode_), calibration_stream.str());

    std::ostringstream calibration_controls_stream;
    calibration_controls_stream << "cal keys out=" << configured_calibration_output_binding_
                                << " in=" << configured_calibration_input_binding_
                                << " sample=" << configured_calibration_commit_binding_
                                << " clear=" << configured_calibration_clear_binding_
                                << " adjust=" << configured_calibration_adjust_negative_binding_ << '/'
                                << configured_calibration_adjust_positive_binding_;
    host.render_extraction().add_debug_text(0, 13, 0x08, calibration_controls_stream.str());

    std::ostringstream practice_stream;
    practice_stream << std::fixed << std::setprecision(2)
                    << "practice scroll=" << practice_scroll_speed_multiplier_ << 'x'
                    << " loop=";
    if (transport_snapshot.loop_region.enabled) {
        practice_stream << '[' << transport_snapshot.loop_region.start_seconds << ", "
                        << transport_snapshot.loop_region.end_seconds << ']';
    } else {
        practice_stream << "none";
    }
    practice_stream << " edit=";
    if (practice_loop_marker_start_set_ || practice_loop_marker_end_set_) {
        practice_stream << '[';
        if (practice_loop_marker_start_set_) {
            practice_stream << practice_loop_marker_start_seconds_;
        } else {
            practice_stream << '?';
        }
        practice_stream << ", ";
        if (practice_loop_marker_end_set_) {
            practice_stream << practice_loop_marker_end_seconds_;
        } else {
            practice_stream << '?';
        }
        practice_stream << ']';
    } else {
        practice_stream << "none";
    }
    practice_stream << " offsets=" << practice_offset_visualization_enabled_
                    << " total-ms=" << static_cast<double>(practice_offset_summary.total_offset_microseconds) / 1000.0;
    host.render_extraction().add_debug_text(0, 14, 0x0b, practice_stream.str());

    std::ostringstream practice_controls_stream;
    practice_controls_stream << "practice keys speed=" << configured_practice_speed_decrease_binding_ << '/'
                             << configured_practice_speed_increase_binding_ << '/'
                             << configured_practice_speed_reset_binding_ << " loop="
                             << configured_practice_loop_mark_start_binding_ << '/'
                             << configured_practice_loop_mark_end_binding_ << '/'
                             << configured_practice_loop_apply_binding_ << " clear="
                             << configured_practice_loop_clear_binding_ << " vis="
                             << configured_practice_offset_visualization_toggle_binding_;
    host.render_extraction().add_debug_text(0, 15, 0x08, practice_controls_stream.str());

    std::ostringstream rhythm_stream;
    if (rhythm_tempo_map_.valid()) {
        rhythm_stream << std::fixed << std::setprecision(1);
        rhythm_stream << "rhythm tick=" << rhythm_position_.tick << " beat=" << rhythm_position_.beat.whole_beats
                      << '+' << rhythm_position_.beat.tick_offset_in_beat << " bar="
                      << rhythm_position_.bar.bar_index << ':' << rhythm_position_.bar.beat_index_in_bar
                      << '+' << rhythm_position_.bar.tick_offset_in_beat << " sample="
                      << rhythm_position_.sample_index << " err-ms=" << nearest_cue_timing_error_ms_
                      << " visible=" << visible_scheduled_cue_count_;
    } else {
        rhythm_stream << rhythm_status_;
    }
    host.render_extraction().add_debug_text(0, 16, 0x0d, rhythm_stream.str());

    std::ostringstream judgement_stream;
    judgement_stream << std::fixed << std::setprecision(1)
                     << "judge=" << rhythm::to_string(nearest_judgement_.judgement)
                     << " corr-ms=" << static_cast<double>(nearest_judgement_.corrected_error_microseconds) / 1000.0
                     << " raw-ms=" << static_cast<double>(nearest_judgement_.raw_error_microseconds) / 1000.0
                     << " offset-ms=" << static_cast<double>(nearest_judgement_.applied_offset_microseconds) / 1000.0
                     << " hit=" << nearest_judgement_.scoreable_hit
                     << " combo=" << nearest_judgement_.advances_combo;
    host.render_extraction().add_debug_text(0, 17, judgement_attribute(nearest_judgement_.judgement), judgement_stream.str());

    std::array<rhythm::ScheduledCue, 4> upcoming_cues{};
    const std::size_t upcoming_count = rhythm::collect_upcoming_cues(
        scheduled_cues_,
        rhythm_position_.tick,
        1920,
        upcoming_cues);
    for (std::size_t index = 0; index < upcoming_cues.size(); ++index) {
        std::ostringstream upcoming_stream;
        if (index < upcoming_count && rhythm_tempo_map_.valid()) {
            const rhythm::ScheduledCue& scheduled_cue = upcoming_cues[index];
            const rhythm::RhythmPosition cue_position = rhythm_tempo_map_.position_from_tick(scheduled_cue.hit_tick);
            const rhythm::CueTravelState travel_state = rhythm::sample_cue_travel(
                rhythm_tempo_map_,
                rhythm_position_.tick,
                scheduled_cue,
                make_demo_cue_travel_window());
            upcoming_stream << std::fixed << std::setprecision(1)
                            << "up[" << index << "] ch=" << scheduled_cue.channel_index
                            << " hit=" << cue_position.bar.bar_index << ':' << cue_position.bar.beat_index_in_bar
                            << '+' << cue_position.bar.tick_offset_in_beat << " dt-ms="
                            << (-travel_state.delta_seconds * 1000.0)
                            << " phase=" << rhythm::to_string(travel_state.phase);
        } else {
            upcoming_stream << "up[" << index << "] none";
        }
        host.render_extraction().add_debug_text(0, static_cast<std::uint16_t>(18 + index), 0x09, upcoming_stream.str());
    }

    std::ostringstream input_stream;
    input_stream << "keys=" << input_snapshot.keyboard_events().size() << " mouse="
                 << input_snapshot.mouse_button_events().size() << " text="
                 << input_snapshot.text_input_events().size() << " gamepads="
                 << input_snapshot.connected_gamepads().size();
    host.render_extraction().add_debug_text(0, 22, 0x0a, input_stream.str());

    std::ostringstream rng_stream;
    rng_stream << "rng root=0x" << std::hex << random_service.root_seed() << std::dec
               << " streams=" << random_service.stream_count() << " transport-roll="
               << transport_roll_ << " visual-roll=" << visual_roll_ << " draws="
               << (transport_rng != nullptr ? transport_rng->generated_values() : 0) << '/'
               << (visual_rng != nullptr ? visual_rng->generated_values() : 0);
    host.render_extraction().add_debug_text(0, 23, 0x0d, rng_stream.str());

    std::ostringstream replay_stream;
    replay_stream << "replay inputs=" << replay_recorder.input_frame_count() << " checkpoints="
                  << replay_recorder.checkpoint_count() << " last="
                  << (last_checkpoint != nullptr ? last_checkpoint->label : std::string_view("none"));
    host.render_extraction().add_debug_text(0, 24, 0x0c, replay_stream.str());

    std::ostringstream event_stream;
    event_stream << "events=" << event_bus.published_count() << '/' << event_bus.count() << " last="
                 << (last_event != nullptr ? gameplay::describe_event(*last_event) : std::string("none"));
    host.render_extraction().add_debug_text(0, 25, 0x0b, event_stream.str());

    std::ostringstream world_stream;
    world_stream << "world entities=" << world_entity_count_ << " phase=" << average_phase_
                 << " sample=" << sample_first_label(host.world_model()) << " x=" << sample_cue_world_x_
                 << " tip-z=" << sample_tip_world_.z;
    host.render_extraction().add_debug_text(0, 26, 0x0f, world_stream.str());

    std::ostringstream transform_stream;
    transform_stream << "propagate 2d=" << propagation_report_.propagated_2d << " 3d="
                     << propagation_report_.propagated_3d << " detached="
                     << (propagation_report_.detached_2d + propagation_report_.detached_3d) << " stale="
                     << (propagation_report_.stale_world_transforms_2d + propagation_report_.stale_world_transforms_3d)
                     << " cycles=" << (propagation_report_.cycle_breaks_2d + propagation_report_.cycle_breaks_3d);
    host.render_extraction().add_debug_text(0, 27, 0x0e, transform_stream.str());

    std::ostringstream motion_stream;
    motion_stream << "motion l2=" << motion_report_.linear_2d << " a2=" << motion_report_.angular_2d
                  << " l3=" << motion_report_.linear_3d << " a3=" << motion_report_.angular_3d
                  << " contacts=" << collision_report_.contacts.size();
    if (!collision_report_.contacts.empty()) {
        motion_stream << " first=" << gameplay::to_string(collision_report_.contacts.front().shape_pair)
                      << " pen=" << collision_report_.contacts.front().penetration;
    }
    host.render_extraction().add_debug_text(0, 28, 0x0d, motion_stream.str());

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
                    << " tex-h=" << cue_texture_handle_.value() << '/' << cue_texture_runtime_label_
                    << " cue=" << cue_material_handle_.value() << '/' << cue_material_runtime_label_
                    << " mesh-h=" << rig_mesh_handle_.value() << '/' << rig_mesh_runtime_label_
                    << " debug=" << debug_font_handle_.value() << '/' << debug_font_runtime_label_
                    << " stale=" << stale_debug_font_borrow_valid_;
    host.render_extraction().add_debug_text(0, 29, 0x0c, resource_stream.str());

    std::ostringstream config_stream;
    config_stream << "cfg speed=" << configured_velocity_scale_ << " hit=" << configured_hit_window_half_width_
                  << 'x' << configured_hit_window_half_height_ << " cue-id="
                  << configured_cue_material_authoring_id_ << " font-id="
                  << configured_debug_font_authoring_id_;
    host.render_extraction().add_debug_text(0, 30, 0x0b, config_stream.str());

    std::ostringstream binding_stream;
    binding_stream << "bindings pause=" << configured_transport_pause_binding_
                   << " restart=" << configured_transport_restart_binding_;
    host.render_extraction().add_debug_text(0, 31, 0x0a, binding_stream.str());

    // Exercise the new render paths: sprites for cue positions, debug shapes for hit windows,
    // and lines for lane baselines.
    host.world_model().for_each<gameplay::WorldTransform2D, SandboxLaneCue>(
        [&](gameplay::WorldEntity, const gameplay::WorldTransform2D& transform, const SandboxLaneCue& lane) {
            const float lane_hue = static_cast<float>(lane.lane_index) * 0.33f;
            host.render_extraction().add_sprite(render::SpriteCommand{
                .view = render::RenderView::MainScene,
                .position = {transform.translation.x, transform.translation.y},
                .size = {36.0f, 36.0f},
                .rotation_radians = transform.rotation_radians,
                .color = {0.4f + lane_hue, 0.8f - lane_hue * 0.5f, 0.9f, 0.8f},
            });
        });

    gameplay::emit_collision_debug_visualizations(
        host.render_extraction(),
        host.world_model(),
        &collision_report_,
        gameplay::CollisionDebugVisualizationStyle{});

    std::array<gameplay::CueLaneDebugVisualization, 3> lane_visualizations{};
    for (std::size_t lane = 0; lane < lane_visualizations.size(); ++lane) {
        const float lane_center_y = k_lane_center_y[lane];
        const float timing_line_x = k_lane_root_x[lane];
        const float lane_velocity_x = k_lane_velocity_x[lane] * configured_velocity_scale_;
        const float spawn_offset = (lane_velocity_x >= 0.0f ? -180.0f : 180.0f) *
            static_cast<float>(rhythm::clamp_scroll_speed_multiplier(practice_scroll_speed_multiplier_));
        lane_visualizations[lane] = gameplay::CueLaneDebugVisualization{
            .lane_start_x = -400.0f,
            .lane_end_x = 400.0f,
            .center_y = lane_center_y,
            .lane_rgba = 0x5050a0ffu,
            .timing_line_x = timing_line_x,
            .timing_line_half_height = configured_hit_window_half_height_ + 18.0f,
            .timing_line_rgba = 0xffc040ffu,
            .spawn_window_center = {.x = timing_line_x + spawn_offset, .y = lane_center_y},
            .spawn_window_half_extents = {.x = 28.0f, .y = configured_hit_window_half_height_ + 8.0f},
            .spawn_window_rgba = 0x4080ffffu,
        };
    }
    gameplay::emit_cue_lane_debug_visualizations(host.render_extraction(), lane_visualizations);

    if (practice_offset_visualization_enabled_ && rhythm_tempo_map_.valid()) {
        const auto add_offset_marker = [&](rhythm::TimelineMicroseconds offset_microseconds, render::Color4 color) {
            if (std::llabs(offset_microseconds) <= 100) {
                return;
            }

            for (std::size_t lane = 0; lane < k_lane_center_y.size(); ++lane) {
                const float marker_x = sample_offset_visualization_x(
                    rhythm_tempo_map_,
                    rhythm_position_,
                    offset_microseconds,
                    static_cast<std::uint32_t>(lane),
                    practice_scroll_speed_multiplier_);
                host.render_extraction().add_line(render::LineCommand{
                    .view = render::RenderView::MainScene,
                    .start = {marker_x, k_lane_center_y[lane] - (configured_hit_window_half_height_ + 24.0f)},
                    .end = {marker_x, k_lane_center_y[lane] + (configured_hit_window_half_height_ + 24.0f)},
                    .color = color,
                });
            }
        };

        add_offset_marker(practice_offset_summary.audio_output_offset_microseconds, {0.30f, 0.85f, 1.00f, 0.70f});
        add_offset_marker(practice_offset_summary.input_response_offset_microseconds, {1.00f, 0.88f, 0.30f, 0.70f});
        add_offset_marker(practice_offset_summary.manual_global_offset_microseconds, {1.00f, 0.45f, 0.85f, 0.70f});
        add_offset_marker(practice_offset_summary.total_offset_microseconds, {1.00f, 1.00f, 1.00f, 0.90f});
    }

    render::InstancedQuadBatchCommand scheduled_cue_batch{
        .view = render::RenderView::MainScene,
    };
    scheduled_cue_batch.quads.reserve(scheduled_cues_.size());
    for (const rhythm::ScheduledCue& scheduled_cue : scheduled_cues_) {
        const rhythm::CueTravelState travel_state = rhythm::sample_cue_travel(
            rhythm_tempo_map_,
            rhythm_position_.tick,
            scheduled_cue,
            make_demo_cue_travel_window());
        if (!travel_state.visible) {
            continue;
        }

        const std::size_t lane = static_cast<std::size_t>(scheduled_cue.channel_index % k_lane_center_y.size());
        scheduled_cue_batch.quads.push_back(render::InstancedQuadInstance{
            .position = {
                rhythm::sample_linear_cue_position_x(
                    travel_state,
                    make_practice_lane_travel_path(
                        scheduled_cue.channel_index,
                        practice_scroll_speed_multiplier_)),
                k_lane_center_y[lane] - 46.0f,
            },
            .size = {20.0f, 20.0f},
            .rotation_radians = travel_state.phase == rhythm::CueTravelPhase::Release ? 0.35f : -0.2f,
            .color = scheduled_cue_color(scheduled_cue.channel_index, travel_state.phase),
        });
    }
    if (!scheduled_cue_batch.quads.empty()) {
        host.render_extraction().add_instanced_quad_batch(scheduled_cue_batch);
    }

    render::QuadBatchCommand note_field_batch{
        .view = render::RenderView::MainScene,
    };
    host.world_model().for_each<gameplay::WorldTransform2D, SandboxPulseCue>(
        [&](gameplay::WorldEntity, const gameplay::WorldTransform2D& transform, const SandboxPulseCue& pulse) {
            note_field_batch.quads.push_back(render::QuadBatchInstance{
                .position = {transform.translation.x, transform.translation.y + 44.0f},
                .size = {18.0f, 10.0f},
                .rotation_radians = pulse.phase * 1.5f,
                .color = {0.9f, 0.75f, 0.25f, 0.7f},
            });
        });
    host.render_extraction().add_quad_batch(note_field_batch);

    render::ParticleBatchCommand particle_batch{
        .view = render::RenderView::MainScene,
    };
    for (int lane = 0; lane < 3; ++lane) {
        const float lane_offset = static_cast<float>(lane) * 2.09439510f;
        const float phase = static_cast<float>(interpolation_alpha) * 6.28318531f +
                            lane_offset + static_cast<float>(visual_roll_) * 0.01f;
        particle_batch.particles.push_back(render::ParticleInstance{
            .position = {
                -240.0f + static_cast<float>(lane) * 240.0f + static_cast<float>(std::cos(phase)) * 26.0f,
                -120.0f + static_cast<float>(lane) * 120.0f + static_cast<float>(std::sin(phase)) * 20.0f,
            },
            .size = {10.0f, 10.0f},
            .rotation_radians = phase,
            .color = {0.35f, 0.8f, 1.0f, 0.55f},
        });
    }
    host.render_extraction().add_particle_batch(particle_batch);

    render::TransientGeometryCommand procedural_geometry{
        .view = render::RenderView::MainScene,
        .primitive = render::BufferPrimitive::Triangles,
        .blend_mode = render::BufferBlendMode::Alpha,
    };
    const std::uint32_t geometry_color = 0x60d0ffffu;
    procedural_geometry.vertices.push_back(render::TransientColorVertex{.x = -340.0f, .y = 220.0f, .z = 0.0f, .abgr = geometry_color});
    procedural_geometry.vertices.push_back(render::TransientColorVertex{.x = -300.0f, .y = 180.0f, .z = 0.0f, .abgr = geometry_color});
    procedural_geometry.vertices.push_back(render::TransientColorVertex{.x = -260.0f, .y = 220.0f, .z = 0.0f, .abgr = geometry_color});
    procedural_geometry.vertices.push_back(render::TransientColorVertex{.x = -300.0f, .y = 180.0f, .z = 0.0f, .abgr = geometry_color});
    procedural_geometry.vertices.push_back(render::TransientColorVertex{.x = -220.0f, .y = 180.0f, .z = 0.0f, .abgr = geometry_color});
    procedural_geometry.vertices.push_back(render::TransientColorVertex{.x = -260.0f, .y = 220.0f, .z = 0.0f, .abgr = geometry_color});
    host.render_extraction().add_transient_geometry(procedural_geometry);

    render::InstancedQuadBatchCommand dense_note_batch{
        .view = render::RenderView::MainScene,
    };
    dense_note_batch.quads.reserve(72u);
    for (int lane = 0; lane < 3; ++lane) {
        const float lane_y = -120.0f + static_cast<float>(lane) * 120.0f;
        for (int note = 0; note < 24; ++note) {
            const float note_phase = static_cast<float>(note) * 0.19f + static_cast<float>(lane) * 0.7f;
            dense_note_batch.quads.push_back(render::InstancedQuadInstance{
                .position = {
                    -340.0f + static_cast<float>(note) * 28.0f,
                    lane_y + std::sin(note_phase + static_cast<float>(interpolation_alpha) * 2.0f) * 10.0f,
                },
                .size = {12.0f, 12.0f},
                .rotation_radians = note_phase * 0.3f,
                .color = {0.85f, 0.35f + static_cast<float>(lane) * 0.15f, 0.95f, 0.65f},
            });
        }
    }
    host.render_extraction().add_instanced_quad_batch(dense_note_batch);

    render::InstancedQuadBatchCommand obstacle_batch{
        .view = render::RenderView::MainScene,
    };
    obstacle_batch.quads.reserve(24u);
    for (int lane = 0; lane < 3; ++lane) {
        const float lane_y = -120.0f + static_cast<float>(lane) * 120.0f;
        for (int obstacle = 0; obstacle < 8; ++obstacle) {
            obstacle_batch.quads.push_back(render::InstancedQuadInstance{
                .position = {
                    -280.0f + static_cast<float>(obstacle) * 80.0f,
                    lane_y - 42.0f,
                },
                .size = {26.0f, 18.0f},
                .rotation_radians = 0.0f,
                .color = {0.25f, 0.9f, 0.45f, 0.55f},
            });
        }
    }
    host.render_extraction().add_instanced_quad_batch(obstacle_batch);
}

void ReferenceSandboxMode::on_exit(gameplay::IModeHost& host) {
    host.transport().stop();
    const gameplay::TransportSnapshot& transport_snapshot = host.transport().snapshot();
    publish_transport_discontinuity_diagnostic(
        host,
        fixed_steps_,
        transport_snapshot,
        last_published_transport_revision_);
    const std::uint64_t checkpoint_hash = make_state_hash(
        fixed_steps_,
        transport_snapshot,
        transport_roll_,
        visual_roll_,
        average_phase_,
        sample_cue_world_x_,
        sample_tip_world_,
        collision_signature_,
        judgement_offset_profile_.audio_output_offset_microseconds,
        judgement_offset_profile_.input_response_offset_microseconds,
        calibration_flow_mode_,
        pending_output_offset_microseconds_,
        practice_scroll_speed_multiplier_,
        practice_offset_visualization_enabled_,
        practice_loop_marker_start_seconds_,
        practice_loop_marker_end_seconds_,
        practice_loop_marker_start_set_,
        practice_loop_marker_end_set_);
    host.replay().record_checkpoint(gameplay::ReplayCheckpoint{
        .frame_index = host.frame_timing().frame_index,
        .simulation_step = fixed_steps_,
        .transport_state = transport_snapshot.playback_state,
        .transport_position_authority = transport_snapshot.position_authority,
        .transport_position_seconds = transport_snapshot.position_seconds,
        .transport_timeline_revision = transport_snapshot.discontinuity.timeline_revision,
        .transport_discontinuity_reason = transport_snapshot.discontinuity.last_reason,
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