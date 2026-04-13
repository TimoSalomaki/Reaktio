#pragma once

#include "reaktio/rhythm/TempoMap.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reaktio::rhythm {

enum class TimingJudgement : std::uint8_t {
    None,
    Miss,
    Good,
    Great,
    Perfect,
};

struct TimingWindow {
    TimingJudgement judgement{TimingJudgement::Perfect};
    TimelineMicroseconds early_window_microseconds{};
    TimelineMicroseconds late_window_microseconds{};
    bool scoreable_hit{true};
    bool advances_combo{true};
};

struct TimingWindowSet {
    std::vector<TimingWindow> ordered_windows;
};

struct TimingOffsetProfile {
    TimelineMicroseconds chart_sync_offset_microseconds{};
    TimelineMicroseconds audio_output_offset_microseconds{};
    TimelineMicroseconds input_response_offset_microseconds{};
    TimelineMicroseconds manual_global_offset_microseconds{};
};

struct TimingJudgementResult {
    TimingJudgement judgement{TimingJudgement::None};
    TimelineMicroseconds cue_time_microseconds{};
    TimelineMicroseconds input_time_microseconds{};
    TimelineMicroseconds applied_offset_microseconds{};
    TimelineMicroseconds raw_error_microseconds{};
    TimelineMicroseconds corrected_error_microseconds{};
    TimelineMicroseconds matched_early_window_microseconds{};
    TimelineMicroseconds matched_late_window_microseconds{};
    bool matched_window{};
    bool early{};
    bool late{};
    bool scoreable_hit{};
    bool advances_combo{};
};

[[nodiscard]] TimingWindowSet make_default_timing_window_set();
[[nodiscard]] bool validate_timing_window_set(const TimingWindowSet& window_set, std::string* error_message = nullptr);
[[nodiscard]] TimelineMicroseconds total_timing_offset(const TimingOffsetProfile& offset_profile) noexcept;
[[nodiscard]] TimingJudgementResult evaluate_timing_judgement(
    const TimingWindowSet& window_set,
    TimelineMicroseconds cue_time_microseconds,
    TimelineMicroseconds input_time_microseconds,
    const TimingOffsetProfile& offset_profile) noexcept;
[[nodiscard]] TimingJudgementResult evaluate_timing_judgement(
    const TempoMap& tempo_map,
    const TimingWindowSet& window_set,
    ChartTick cue_hit_tick,
    TimelineMicroseconds input_time_microseconds,
    const TimingOffsetProfile& offset_profile) noexcept;

[[nodiscard]] inline constexpr std::string_view to_string(TimingJudgement judgement) noexcept {
    switch (judgement) {
    case TimingJudgement::None:
        return "none";
    case TimingJudgement::Miss:
        return "miss";
    case TimingJudgement::Good:
        return "good";
    case TimingJudgement::Great:
        return "great";
    case TimingJudgement::Perfect:
        return "perfect";
    }

    return "unknown";
}

} // namespace reaktio::rhythm