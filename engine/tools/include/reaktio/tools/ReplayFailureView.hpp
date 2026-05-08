#pragma once

#include "reaktio/gameplay/ReplayInspection.hpp"
#include "reaktio/gameplay/ReplayRecorder.hpp"
#include "reaktio/tools/InspectorPanel.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace reaktio::tools {

// Phase 11 expansion of the existing ReplayInspectionView. The base
// view (engine/gameplay/ReplayInspection.hpp) already produces input
// timeline rows, judgement summaries, judgement-offset histograms, and
// recent checkpoints. The tooling layer adds:
//
//   - A timing-offset overlay model: a rebuilt histogram with explicit
//     bucket bounds, plus the mean/median/percentile rollups consumers
//     typically display under a histogram graph.
//   - A failure-state view: the first failing judgement (if any), the
//     point where the run transitioned to Failed in the score run
//     state, and the most-recent checkpoint preceding the failure so
//     UI consumers can scrub directly to "right before the failure".
//
// Both keep the engine/gameplay ReplayInspectionView untouched: tools
// take the immutable view by const ref and produce derived diagnostics.

struct TimingOffsetBucketBounds {
    // Microsecond bounds for one histogram bucket.
    // Buckets are inclusive of min, exclusive of max.
    std::int64_t min_microseconds{};
    std::int64_t max_microseconds{};
    std::uint32_t sample_count{};
};

struct TimingOffsetOverlay {
    std::int64_t bucket_microseconds{};
    std::vector<TimingOffsetBucketBounds> buckets;
    std::uint32_t below_range_count{};
    std::uint32_t above_range_count{};
    std::uint32_t total_samples{};
    std::int64_t mean_microseconds{};
    std::int64_t mean_absolute_microseconds{};
    std::int64_t min_microseconds{};
    std::int64_t max_microseconds{};
    // Mode-of-the-bucket (peak): index of the most-populated bucket.
    // Set to npos if no samples were recorded.
    static constexpr std::size_t k_no_peak = static_cast<std::size_t>(-1);
    std::size_t peak_bucket_index{k_no_peak};
};

[[nodiscard]] TimingOffsetOverlay build_timing_offset_overlay(
    const gameplay::ReplayInspectionView& view);

[[nodiscard]] InspectorPanel build_timing_offset_inspector(
    const gameplay::ReplayInspectionView& view);

// Failure-state view. failed_at is set when the score's run state ever
// transitions to Failed in the recorded judgement samples; the helper
// also reports the first-miss judgement (if any) which is often
// distinct from the actual fail-trigger judgement.
struct ReplayFailureState {
    bool ever_failed{false};
    std::optional<gameplay::ReplayJudgementSample> first_miss;
    std::optional<gameplay::ReplayJudgementSample> failure_trigger;
    std::optional<gameplay::ReplayCheckpoint> nearest_checkpoint_before_failure;
};

[[nodiscard]] ReplayFailureState build_replay_failure_state(
    const gameplay::ReplaySession& session);

[[nodiscard]] InspectorPanel build_replay_failure_inspector(
    const ReplayFailureState& failure_state);

} // namespace reaktio::tools
