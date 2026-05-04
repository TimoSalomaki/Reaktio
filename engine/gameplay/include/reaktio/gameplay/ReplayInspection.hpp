#pragma once

#include "reaktio/gameplay/ReplayRecorder.hpp"
#include "reaktio/rhythm/TimingJudgement.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace reaktio::gameplay {

struct ReplayInspectionOptions {
    static constexpr std::size_t k_default_recent_input_frames = 8;
    static constexpr std::size_t k_default_recent_judgements = 8;
    static constexpr std::size_t k_default_recent_checkpoints = 4;
    static constexpr std::size_t k_default_histogram_bucket_count = 11;
    static constexpr std::int64_t k_default_histogram_bucket_microseconds = 20000;

    std::size_t recent_input_frame_count{k_default_recent_input_frames};
    std::size_t recent_judgement_count{k_default_recent_judgements};
    std::size_t recent_checkpoint_count{k_default_recent_checkpoints};
    std::size_t histogram_bucket_count{k_default_histogram_bucket_count};
    std::int64_t histogram_bucket_microseconds{k_default_histogram_bucket_microseconds};
};

struct ReplayInputTimelineRow {
    std::uint64_t frame_index{};
    std::uint64_t wall_clock_ns{};
    double frame_delta_seconds{};
    double interpolation_alpha{};
    std::uint32_t keyboard_event_count{};
    std::uint32_t text_input_event_count{};
    std::uint32_t text_editing_event_count{};
    std::uint32_t mouse_event_count{};
    std::uint32_t gamepad_event_count{};
    std::uint32_t total_event_count{};
};

struct ReplayJudgementSummary {
    std::size_t sample_count{};
    std::array<std::uint32_t, k_timing_judgement_count> judgement_counts{};
    std::uint32_t scoreable_hit_count{};
    std::uint32_t miss_count{};
    std::uint32_t early_count{};
    std::uint32_t late_count{};
    std::int64_t mean_corrected_error_microseconds{};
    std::int64_t mean_absolute_corrected_error_microseconds{};
    std::int64_t min_corrected_error_microseconds{};
    std::int64_t max_corrected_error_microseconds{};
    double accuracy_ratio{};
};

struct ReplayJudgementOffsetHistogram {
    std::int64_t bucket_microseconds{};
    std::int64_t first_bucket_min_microseconds{};
    std::int64_t last_bucket_max_microseconds{};
    std::vector<std::uint32_t> bucket_counts;
    std::uint32_t below_range_count{};
    std::uint32_t above_range_count{};
    std::uint32_t total_samples{};
};

struct ReplayCheckpointSummary {
    std::size_t total_in_session{};
    std::uint64_t first_simulation_step{};
    std::uint64_t last_simulation_step{};
    std::string last_label;
    std::uint64_t last_authoritative_state_hash{};
    std::vector<ReplayCheckpoint> recent;
};

struct ReplayInspectionView {
    ReplaySessionMetadata metadata;
    std::size_t total_input_frames{};
    std::size_t total_checkpoints{};
    std::size_t total_judgement_samples{};
    std::uint64_t total_input_frames_recorded{};
    std::uint64_t total_checkpoints_recorded{};
    std::uint64_t total_judgement_samples_recorded{};
    bool input_frames_truncated{};
    bool checkpoints_truncated{};
    bool judgement_samples_truncated{};

    std::vector<ReplayInputTimelineRow> recent_input_timeline;
    ReplayJudgementSummary judgement_summary{};
    ReplayJudgementOffsetHistogram judgement_offset_histogram{};
    std::vector<ReplayJudgementSample> recent_judgements;
    ReplayCheckpointSummary checkpoint_summary{};
};

[[nodiscard]] ReplayInspectionView build_replay_inspection_view(
    const ReplaySession& session,
    ReplayInspectionOptions options = {});

[[nodiscard]] std::string format_replay_inspection_view(const ReplayInspectionView& view);

} // namespace reaktio::gameplay
