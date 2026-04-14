#pragma once

#include "reaktio/rhythm/CueTravelModel.hpp"
#include "reaktio/rhythm/TimingJudgement.hpp"

namespace reaktio::rhythm {

struct PracticeLoopSegment {
    double start_seconds{};
    double end_seconds{};
    bool enabled{};
};

struct PracticeOffsetSummary {
    TimelineMicroseconds chart_sync_offset_microseconds{};
    TimelineMicroseconds audio_output_offset_microseconds{};
    TimelineMicroseconds input_response_offset_microseconds{};
    TimelineMicroseconds manual_global_offset_microseconds{};
    TimelineMicroseconds total_offset_microseconds{};
};

[[nodiscard]] double clamp_scroll_speed_multiplier(double scroll_speed_multiplier) noexcept;
[[nodiscard]] PracticeLoopSegment make_practice_loop_segment(
    double first_boundary_seconds,
    double second_boundary_seconds,
    double minimum_length_seconds = 0.05) noexcept;
[[nodiscard]] PracticeOffsetSummary summarize_practice_offsets(const TimingOffsetProfile& offset_profile) noexcept;
[[nodiscard]] LinearCueTravelPath scale_linear_cue_travel_path(
    const LinearCueTravelPath& path,
    double scroll_speed_multiplier) noexcept;

} // namespace reaktio::rhythm