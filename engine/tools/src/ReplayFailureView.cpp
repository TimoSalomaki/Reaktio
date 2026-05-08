#include "reaktio/tools/ReplayFailureView.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace reaktio::tools {

namespace {

[[nodiscard]] std::string format_microseconds_signed(std::int64_t us) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << (static_cast<double>(us) / 1000.0) << "ms";
    return stream.str();
}

} // namespace

TimingOffsetOverlay build_timing_offset_overlay(const gameplay::ReplayInspectionView& view) {
    TimingOffsetOverlay overlay{};
    const gameplay::ReplayJudgementOffsetHistogram& histogram = view.judgement_offset_histogram;

    overlay.bucket_microseconds = histogram.bucket_microseconds;
    overlay.below_range_count = histogram.below_range_count;
    overlay.above_range_count = histogram.above_range_count;
    overlay.total_samples = histogram.total_samples;
    overlay.mean_microseconds = view.judgement_summary.mean_corrected_error_microseconds;
    overlay.mean_absolute_microseconds = view.judgement_summary.mean_absolute_corrected_error_microseconds;
    overlay.min_microseconds = view.judgement_summary.min_corrected_error_microseconds;
    overlay.max_microseconds = view.judgement_summary.max_corrected_error_microseconds;

    overlay.buckets.reserve(histogram.bucket_counts.size());
    std::int64_t bucket_min = histogram.first_bucket_min_microseconds;
    std::uint32_t peak_count = 0;
    for (std::size_t i = 0; i < histogram.bucket_counts.size(); ++i) {
        TimingOffsetBucketBounds bounds{};
        bounds.min_microseconds = bucket_min;
        bounds.max_microseconds = bucket_min + histogram.bucket_microseconds;
        bounds.sample_count = histogram.bucket_counts[i];
        overlay.buckets.push_back(bounds);
        if (bounds.sample_count > peak_count) {
            peak_count = bounds.sample_count;
            overlay.peak_bucket_index = i;
        }
        bucket_min += histogram.bucket_microseconds;
    }
    return overlay;
}

InspectorPanel build_timing_offset_inspector(const gameplay::ReplayInspectionView& view) {
    InspectorPanel panel{};
    panel.id = "timing-offset";
    panel.title = "Timing Offset Overlay";

    const TimingOffsetOverlay overlay = build_timing_offset_overlay(view);
    push_row(panel, "samples", std::to_string(overlay.total_samples));
    push_row(panel, "mean", format_microseconds_signed(overlay.mean_microseconds));
    push_row(panel, "mean_abs", format_microseconds_signed(overlay.mean_absolute_microseconds));
    push_row(panel, "min", format_microseconds_signed(overlay.min_microseconds));
    push_row(panel, "max", format_microseconds_signed(overlay.max_microseconds));
    push_row(panel, "below_range", std::to_string(overlay.below_range_count));
    push_row(panel, "above_range", std::to_string(overlay.above_range_count));
    {
        std::ostringstream line;
        if (overlay.peak_bucket_index == TimingOffsetOverlay::k_no_peak) {
            line << "no_samples";
        } else {
            const auto& peak = overlay.buckets[overlay.peak_bucket_index];
            line << "[" << format_microseconds_signed(peak.min_microseconds)
                 << "," << format_microseconds_signed(peak.max_microseconds)
                 << ") count=" << peak.sample_count;
        }
        push_row(panel, "peak_bucket", line.str());
    }

    for (std::size_t i = 0; i < overlay.buckets.size(); ++i) {
        const TimingOffsetBucketBounds& bounds = overlay.buckets[i];
        std::ostringstream line;
        line << "bucket[" << i << "] ["
             << format_microseconds_signed(bounds.min_microseconds) << ","
             << format_microseconds_signed(bounds.max_microseconds) << ") count="
             << bounds.sample_count;
        panel.body_lines.push_back(line.str());
    }
    return panel;
}

ReplayFailureState build_replay_failure_state(
    const gameplay::ReplaySession& session) {
    ReplayFailureState state{};

    // Walk the judgement samples in order. The session's vector is the
    // recorded slice of the deque so iterating left-to-right yields the
    // run's chronological order.
    for (const gameplay::ReplayJudgementSample& sample : session.judgement_samples) {
        if (!state.first_miss.has_value() &&
            sample.judgement == rhythm::TimingJudgement::Miss) {
            state.first_miss = sample;
        }
        if (sample.run_state == gameplay::ScoreRunState::Failed && !state.failure_trigger.has_value()) {
            state.failure_trigger = sample;
            state.ever_failed = true;
        }
    }

    if (state.ever_failed && state.failure_trigger.has_value()) {
        const gameplay::ReplayJudgementSample& trigger = *state.failure_trigger;
        // Find the latest checkpoint that lives at or before the
        // failure-triggering simulation step.
        const gameplay::ReplayCheckpoint* best = nullptr;
        for (const gameplay::ReplayCheckpoint& checkpoint : session.checkpoints) {
            if (checkpoint.simulation_step <= trigger.simulation_step) {
                if (best == nullptr || checkpoint.simulation_step > best->simulation_step) {
                    best = &checkpoint;
                }
            }
        }
        if (best != nullptr) {
            state.nearest_checkpoint_before_failure = *best;
        }
    }

    return state;
}

InspectorPanel build_replay_failure_inspector(const ReplayFailureState& failure_state) {
    InspectorPanel panel{};
    panel.id = "replay-failure";
    panel.title = "Replay Failure";
    push_row(
        panel,
        "ever_failed",
        failure_state.ever_failed ? "1" : "0",
        failure_state.ever_failed ? InspectorRowSeverity::Error : InspectorRowSeverity::Info);
    push_row(panel, "first_miss_present", failure_state.first_miss.has_value() ? "1" : "0");
    if (failure_state.first_miss.has_value()) {
        const gameplay::ReplayJudgementSample& miss = *failure_state.first_miss;
        std::ostringstream line;
        line << "first_miss frame=" << miss.frame_index
             << " step=" << miss.simulation_step
             << " cue=" << miss.cue_id
             << " err=" << format_microseconds_signed(miss.corrected_error_microseconds);
        panel.body_lines.push_back(line.str());
    }
    if (failure_state.failure_trigger.has_value()) {
        const gameplay::ReplayJudgementSample& trigger = *failure_state.failure_trigger;
        std::ostringstream line;
        line << "failure_trigger frame=" << trigger.frame_index
             << " step=" << trigger.simulation_step
             << " cue=" << trigger.cue_id
             << " score=" << trigger.score_after
             << " combo=" << trigger.combo_after
             << " err=" << format_microseconds_signed(trigger.corrected_error_microseconds);
        panel.body_lines.push_back(line.str());
    }
    if (failure_state.nearest_checkpoint_before_failure.has_value()) {
        const gameplay::ReplayCheckpoint& checkpoint = *failure_state.nearest_checkpoint_before_failure;
        std::ostringstream line;
        line << "nearest_checkpoint frame=" << checkpoint.frame_index
             << " step=" << checkpoint.simulation_step
             << " label=" << checkpoint.label
             << " hash=" << checkpoint.authoritative_state_hash;
        panel.body_lines.push_back(line.str());
    }
    return panel;
}

} // namespace reaktio::tools
