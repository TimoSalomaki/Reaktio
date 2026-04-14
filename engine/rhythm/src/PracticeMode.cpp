#include "reaktio/rhythm/PracticeMode.hpp"

#include <algorithm>
#include <cmath>

namespace reaktio::rhythm {

namespace {

constexpr double k_min_scroll_speed_multiplier = 0.5;
constexpr double k_max_scroll_speed_multiplier = 2.5;

float scale_delta(float endpoint, float origin, double multiplier) noexcept {
    return origin + static_cast<float>((endpoint - origin) * multiplier);
}

} // namespace

double clamp_scroll_speed_multiplier(double scroll_speed_multiplier) noexcept {
    if (!std::isfinite(scroll_speed_multiplier)) {
        return 1.0;
    }

    return std::clamp(scroll_speed_multiplier, k_min_scroll_speed_multiplier, k_max_scroll_speed_multiplier);
}

PracticeLoopSegment make_practice_loop_segment(
    double first_boundary_seconds,
    double second_boundary_seconds,
    double minimum_length_seconds) noexcept {
    if (!std::isfinite(first_boundary_seconds) || !std::isfinite(second_boundary_seconds) ||
        !std::isfinite(minimum_length_seconds) || minimum_length_seconds <= 0.0) {
        return PracticeLoopSegment{};
    }

    const double start_seconds = std::min(first_boundary_seconds, second_boundary_seconds);
    const double end_seconds = std::max(first_boundary_seconds, second_boundary_seconds);
    if (end_seconds - start_seconds < minimum_length_seconds) {
        return PracticeLoopSegment{};
    }

    return PracticeLoopSegment{
        .start_seconds = std::max(0.0, start_seconds),
        .end_seconds = std::max(0.0, end_seconds),
        .enabled = true,
    };
}

PracticeOffsetSummary summarize_practice_offsets(const TimingOffsetProfile& offset_profile) noexcept {
    return PracticeOffsetSummary{
        .chart_sync_offset_microseconds = offset_profile.chart_sync_offset_microseconds,
        .audio_output_offset_microseconds = offset_profile.audio_output_offset_microseconds,
        .input_response_offset_microseconds = offset_profile.input_response_offset_microseconds,
        .manual_global_offset_microseconds = offset_profile.manual_global_offset_microseconds,
        .total_offset_microseconds = total_timing_offset(offset_profile),
    };
}

LinearCueTravelPath scale_linear_cue_travel_path(
    const LinearCueTravelPath& path,
    double scroll_speed_multiplier) noexcept {
    const double clamped_multiplier = clamp_scroll_speed_multiplier(scroll_speed_multiplier);
    return LinearCueTravelPath{
        .spawn_x = scale_delta(path.spawn_x, path.hit_x, clamped_multiplier),
        .hit_x = path.hit_x,
        .release_x = scale_delta(path.release_x, path.hit_x, clamped_multiplier),
    };
}

} // namespace reaktio::rhythm