#include "reaktio/rhythm/TimingJudgement.hpp"

#include <algorithm>
#include <cmath>

namespace reaktio::rhythm {

namespace {

bool set_validation_error(std::string* error_message, std::string_view message) {
    if (error_message != nullptr) {
        *error_message = std::string(message);
    }

    return false;
}

} // namespace

TimingWindowSet make_default_timing_window_set() {
    return TimingWindowSet{
        .ordered_windows = {
            TimingWindow{
                .judgement = TimingJudgement::Perfect,
                .early_window_microseconds = 18000,
                .late_window_microseconds = 18000,
                .scoreable_hit = true,
                .advances_combo = true,
            },
            TimingWindow{
                .judgement = TimingJudgement::Great,
                .early_window_microseconds = 45000,
                .late_window_microseconds = 45000,
                .scoreable_hit = true,
                .advances_combo = true,
            },
            TimingWindow{
                .judgement = TimingJudgement::Good,
                .early_window_microseconds = 90000,
                .late_window_microseconds = 90000,
                .scoreable_hit = true,
                .advances_combo = true,
            },
            TimingWindow{
                .judgement = TimingJudgement::Miss,
                .early_window_microseconds = 130000,
                .late_window_microseconds = 130000,
                .scoreable_hit = false,
                .advances_combo = false,
            },
        },
    };
}

bool validate_timing_window_set(const TimingWindowSet& window_set, std::string* error_message) {
    if (window_set.ordered_windows.empty()) {
        return set_validation_error(error_message, "Timing window set must contain at least one window.");
    }

    TimelineMicroseconds previous_early = 0;
    TimelineMicroseconds previous_late = 0;
    for (const TimingWindow& window : window_set.ordered_windows) {
        if (window.judgement == TimingJudgement::None) {
            return set_validation_error(error_message, "Timing windows must not use the 'none' judgement.");
        }

        if (window.early_window_microseconds < 0 || window.late_window_microseconds < 0) {
            return set_validation_error(error_message, "Timing windows must use non-negative early and late bounds.");
        }

        if (window.early_window_microseconds < previous_early ||
            window.late_window_microseconds < previous_late) {
            return set_validation_error(error_message, "Timing windows must be ordered from tightest to loosest bounds.");
        }

        previous_early = window.early_window_microseconds;
        previous_late = window.late_window_microseconds;
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

TimelineMicroseconds total_timing_offset(const TimingOffsetProfile& offset_profile) noexcept {
    return offset_profile.chart_sync_offset_microseconds +
           offset_profile.audio_output_offset_microseconds +
           offset_profile.input_response_offset_microseconds +
           offset_profile.manual_global_offset_microseconds;
}

TimingJudgementResult evaluate_timing_judgement(
    const TimingWindowSet& window_set,
    TimelineMicroseconds cue_time_microseconds,
    TimelineMicroseconds input_time_microseconds,
    const TimingOffsetProfile& offset_profile) noexcept {
    TimingJudgementResult result{
        .judgement = TimingJudgement::None,
        .cue_time_microseconds = cue_time_microseconds,
        .input_time_microseconds = input_time_microseconds,
        .applied_offset_microseconds = total_timing_offset(offset_profile),
        .raw_error_microseconds = input_time_microseconds - cue_time_microseconds,
        .corrected_error_microseconds = input_time_microseconds -
            (cue_time_microseconds + total_timing_offset(offset_profile)),
    };
    result.early = result.corrected_error_microseconds < 0;
    result.late = result.corrected_error_microseconds > 0;

    for (const TimingWindow& window : window_set.ordered_windows) {
        if (result.corrected_error_microseconds < -window.early_window_microseconds ||
            result.corrected_error_microseconds > window.late_window_microseconds) {
            continue;
        }

        result.judgement = window.judgement;
        result.matched_early_window_microseconds = window.early_window_microseconds;
        result.matched_late_window_microseconds = window.late_window_microseconds;
        result.matched_window = true;
        result.scoreable_hit = window.scoreable_hit;
        result.advances_combo = window.advances_combo;
        return result;
    }

    return result;
}

TimingJudgementResult evaluate_timing_judgement(
    const TempoMap& tempo_map,
    const TimingWindowSet& window_set,
    ChartTick cue_hit_tick,
    TimelineMicroseconds input_time_microseconds,
    const TimingOffsetProfile& offset_profile) noexcept {
    return evaluate_timing_judgement(
        window_set,
        tempo_map.microseconds_from_tick(cue_hit_tick),
        input_time_microseconds,
        offset_profile);
}

} // namespace reaktio::rhythm