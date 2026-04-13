#include "reaktio/rhythm/CueTravelModel.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace reaktio::rhythm {

namespace {

AudioSampleIndex microseconds_to_sample_delta(
    TimelineMicroseconds microseconds,
    std::int32_t sample_rate_hz) noexcept {
    if (microseconds == 0 || sample_rate_hz <= 0) {
        return 0;
    }

    return (microseconds * sample_rate_hz) / microseconds_per_second();
}

double microseconds_to_seconds(TimelineMicroseconds microseconds) noexcept {
    return static_cast<double>(microseconds) / static_cast<double>(microseconds_per_second());
}

double inverse_non_zero(ChartTick ticks) noexcept {
    return ticks > 0 ? 1.0 / static_cast<double>(ticks) : 0.0;
}

float lerp(float start, float end, double t) noexcept {
    return static_cast<float>(start + (end - start) * t);
}

} // namespace

CueTravelState sample_cue_travel(
    const TempoMap& tempo_map,
    ChartTick current_tick,
    const ScheduledCue& cue,
    const CueTravelWindow& window) noexcept {
    const ChartTick delta_ticks = current_tick - cue.hit_tick;
    const TimelineMicroseconds current_microseconds = tempo_map.microseconds_from_tick(current_tick);
    const TimelineMicroseconds cue_microseconds = tempo_map.microseconds_from_tick(cue.hit_tick);
    const TimelineMicroseconds delta_microseconds = current_microseconds - cue_microseconds;

    CueTravelState state{
        .cue = cue,
        .phase = CueTravelPhase::Hidden,
        .delta_ticks = delta_ticks,
        .delta_microseconds = delta_microseconds,
        .delta_samples = microseconds_to_sample_delta(delta_microseconds, tempo_map.config().sample_rate_hz),
        .delta_seconds = microseconds_to_seconds(delta_microseconds),
        .normalized_progress = delta_ticks < 0 ? 0.0 : 2.0,
        .signed_normalized_offset = 0.0,
        .visible = false,
    };

    if (delta_ticks < -window.pre_hit_visible_ticks || delta_ticks > window.post_hit_visible_ticks) {
        return state;
    }

    state.visible = true;
    if (delta_ticks <= 0) {
        state.phase = CueTravelPhase::Approaching;
        const double scale = inverse_non_zero(window.pre_hit_visible_ticks);
        state.signed_normalized_offset = static_cast<double>(delta_ticks) * scale;
        state.normalized_progress = 1.0 + state.signed_normalized_offset;
        return state;
    }

    state.phase = CueTravelPhase::Release;
    const double scale = inverse_non_zero(window.post_hit_visible_ticks);
    state.signed_normalized_offset = static_cast<double>(delta_ticks) * scale;
    state.normalized_progress = 1.0 + state.signed_normalized_offset;
    return state;
}

float sample_linear_cue_position_x(
    const CueTravelState& state,
    const LinearCueTravelPath& path) noexcept {
    if (state.phase == CueTravelPhase::Approaching) {
        return lerp(path.spawn_x, path.hit_x, std::clamp(state.normalized_progress, 0.0, 1.0));
    }

    if (state.phase == CueTravelPhase::Release) {
        return lerp(path.hit_x, path.release_x, std::clamp(state.normalized_progress - 1.0, 0.0, 1.0));
    }

    return state.delta_ticks < 0 ? path.spawn_x : path.release_x;
}

std::size_t collect_upcoming_cues(
    std::span<const ScheduledCue> schedule,
    ChartTick current_tick,
    ChartTick lookahead_ticks,
    std::span<ScheduledCue> output) noexcept {
    const ChartTick lookahead_end_tick = lookahead_ticks <= 0
        ? std::numeric_limits<ChartTick>::max()
        : current_tick + lookahead_ticks;

    std::size_t written = 0;
    for (const ScheduledCue& cue : schedule) {
        if (cue.hit_tick < current_tick) {
            continue;
        }

        if (cue.hit_tick > lookahead_end_tick) {
            break;
        }

        if (written >= output.size()) {
            break;
        }

        output[written] = cue;
        ++written;
    }

    return written;
}

const ScheduledCue* find_nearest_cue_by_time(
    std::span<const ScheduledCue> schedule,
    const TempoMap& tempo_map,
    TimelineMicroseconds current_microseconds) noexcept {
    const ScheduledCue* nearest = nullptr;
    TimelineMicroseconds smallest_distance = std::numeric_limits<TimelineMicroseconds>::max();
    for (const ScheduledCue& cue : schedule) {
        const TimelineMicroseconds cue_microseconds = tempo_map.microseconds_from_tick(cue.hit_tick);
        const TimelineMicroseconds distance = std::llabs(cue_microseconds - current_microseconds);
        if (distance < smallest_distance) {
            smallest_distance = distance;
            nearest = &cue;
        }
    }

    return nearest;
}

} // namespace reaktio::rhythm