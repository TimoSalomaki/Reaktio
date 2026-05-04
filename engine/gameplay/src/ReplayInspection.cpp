#include "reaktio/gameplay/ReplayInspection.hpp"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>

namespace reaktio::gameplay {

namespace {

ReplayInputTimelineRow build_timeline_row(const ReplayInputFrame& frame) {
    ReplayInputTimelineRow row{};
    row.frame_index = frame.frame_index;
    row.wall_clock_ns = frame.wall_clock_ns;
    row.frame_delta_seconds = frame.frame_delta_seconds;
    row.interpolation_alpha = frame.interpolation_alpha;
    row.keyboard_event_count = static_cast<std::uint32_t>(frame.keyboard_events.size());
    row.text_input_event_count = static_cast<std::uint32_t>(frame.text_input_events.size());
    row.text_editing_event_count = static_cast<std::uint32_t>(
        frame.text_editing_events.size() + frame.text_editing_candidates_events.size());
    row.mouse_event_count = static_cast<std::uint32_t>(
        frame.mouse_motion_events.size() + frame.mouse_button_events.size() + frame.mouse_wheel_events.size());
    row.gamepad_event_count = static_cast<std::uint32_t>(
        frame.gamepad_connection_events.size() + frame.gamepad_axis_events.size() + frame.gamepad_button_events.size());
    row.total_event_count =
        row.keyboard_event_count + row.text_input_event_count + row.text_editing_event_count +
        row.mouse_event_count + row.gamepad_event_count;
    return row;
}

ReplayJudgementSummary summarize_judgements(const std::vector<ReplayJudgementSample>& samples) {
    ReplayJudgementSummary summary{};
    summary.sample_count = samples.size();
    if (samples.empty()) {
        return summary;
    }

    summary.min_corrected_error_microseconds = std::numeric_limits<std::int64_t>::max();
    summary.max_corrected_error_microseconds = std::numeric_limits<std::int64_t>::min();

    long double error_sum = 0.0L;
    long double absolute_error_sum = 0.0L;
    long double accuracy_points = 0.0L;

    for (const ReplayJudgementSample& sample : samples) {
        const std::size_t judgement_index = static_cast<std::size_t>(sample.judgement);
        if (judgement_index < summary.judgement_counts.size()) {
            ++summary.judgement_counts[judgement_index];
        }
        if (sample.scoreable_hit) {
            ++summary.scoreable_hit_count;
        }
        if (sample.judgement == rhythm::TimingJudgement::Miss) {
            ++summary.miss_count;
        }

        const std::int64_t error = static_cast<std::int64_t>(sample.corrected_error_microseconds);
        if (error < 0) {
            ++summary.early_count;
        } else if (error > 0) {
            ++summary.late_count;
        }
        summary.min_corrected_error_microseconds = std::min(summary.min_corrected_error_microseconds, error);
        summary.max_corrected_error_microseconds = std::max(summary.max_corrected_error_microseconds, error);
        error_sum += static_cast<long double>(error);
        absolute_error_sum += static_cast<long double>(std::llabs(error));

        switch (sample.judgement) {
        case rhythm::TimingJudgement::Perfect:
            accuracy_points += 1.0L;
            break;
        case rhythm::TimingJudgement::Great:
            accuracy_points += 0.85L;
            break;
        case rhythm::TimingJudgement::Good:
            accuracy_points += 0.6L;
            break;
        case rhythm::TimingJudgement::Miss:
        case rhythm::TimingJudgement::None:
        default:
            break;
        }
    }

    const long double sample_count = static_cast<long double>(summary.sample_count);
    summary.mean_corrected_error_microseconds =
        static_cast<std::int64_t>(error_sum / sample_count);
    summary.mean_absolute_corrected_error_microseconds =
        static_cast<std::int64_t>(absolute_error_sum / sample_count);
    summary.accuracy_ratio = static_cast<double>(accuracy_points / sample_count);
    return summary;
}

ReplayJudgementOffsetHistogram build_offset_histogram(
    const std::vector<ReplayJudgementSample>& samples,
    std::size_t bucket_count,
    std::int64_t bucket_microseconds) {
    ReplayJudgementOffsetHistogram histogram{};
    histogram.bucket_microseconds = std::max<std::int64_t>(bucket_microseconds, 1);
    histogram.bucket_counts.assign(std::max<std::size_t>(bucket_count, 1u), 0u);

    const std::int64_t half = static_cast<std::int64_t>(histogram.bucket_counts.size() / 2u);
    histogram.first_bucket_min_microseconds = -half * histogram.bucket_microseconds;
    histogram.last_bucket_max_microseconds = histogram.first_bucket_min_microseconds +
        static_cast<std::int64_t>(histogram.bucket_counts.size()) * histogram.bucket_microseconds;
    histogram.total_samples = static_cast<std::uint32_t>(samples.size());

    for (const ReplayJudgementSample& sample : samples) {
        const std::int64_t error = static_cast<std::int64_t>(sample.corrected_error_microseconds);
        if (error < histogram.first_bucket_min_microseconds) {
            ++histogram.below_range_count;
            continue;
        }
        if (error >= histogram.last_bucket_max_microseconds) {
            ++histogram.above_range_count;
            continue;
        }
        const std::int64_t offset_from_start = error - histogram.first_bucket_min_microseconds;
        std::size_t bucket = static_cast<std::size_t>(offset_from_start / histogram.bucket_microseconds);
        if (bucket >= histogram.bucket_counts.size()) {
            bucket = histogram.bucket_counts.size() - 1u;
        }
        ++histogram.bucket_counts[bucket];
    }

    return histogram;
}

ReplayCheckpointSummary build_checkpoint_summary(
    const std::vector<ReplayCheckpoint>& checkpoints,
    std::size_t recent_count) {
    ReplayCheckpointSummary summary{};
    summary.total_in_session = checkpoints.size();
    if (checkpoints.empty()) {
        return summary;
    }

    summary.first_simulation_step = checkpoints.front().simulation_step;
    summary.last_simulation_step = checkpoints.back().simulation_step;
    summary.last_label = checkpoints.back().label;
    summary.last_authoritative_state_hash = checkpoints.back().authoritative_state_hash;

    const std::size_t take = std::min(recent_count, checkpoints.size());
    summary.recent.reserve(take);
    for (std::size_t index = checkpoints.size() - take; index < checkpoints.size(); ++index) {
        summary.recent.push_back(checkpoints[index]);
    }
    return summary;
}

} // namespace

ReplayInspectionView build_replay_inspection_view(
    const ReplaySession& session,
    ReplayInspectionOptions options) {
    ReplayInspectionView view{};
    view.metadata = session.metadata;
    view.total_input_frames = session.input_frames.size();
    view.total_checkpoints = session.checkpoints.size();
    view.total_judgement_samples = session.judgement_samples.size();
    view.total_input_frames_recorded = session.total_input_frames_recorded;
    view.total_checkpoints_recorded = session.total_checkpoints_recorded;
    view.total_judgement_samples_recorded = session.total_judgement_samples_recorded;
    view.input_frames_truncated = session.input_frames_truncated;
    view.checkpoints_truncated = session.checkpoints_truncated;
    view.judgement_samples_truncated = session.judgement_samples_truncated;

    {
        const std::size_t take = std::min(options.recent_input_frame_count, session.input_frames.size());
        view.recent_input_timeline.reserve(take);
        for (std::size_t index = session.input_frames.size() - take; index < session.input_frames.size(); ++index) {
            view.recent_input_timeline.push_back(build_timeline_row(session.input_frames[index]));
        }
    }

    view.judgement_summary = summarize_judgements(session.judgement_samples);
    view.judgement_offset_histogram = build_offset_histogram(
        session.judgement_samples,
        options.histogram_bucket_count,
        options.histogram_bucket_microseconds);

    {
        const std::size_t take = std::min(options.recent_judgement_count, session.judgement_samples.size());
        view.recent_judgements.reserve(take);
        for (std::size_t index = session.judgement_samples.size() - take; index < session.judgement_samples.size(); ++index) {
            view.recent_judgements.push_back(session.judgement_samples[index]);
        }
    }

    view.checkpoint_summary = build_checkpoint_summary(session.checkpoints, options.recent_checkpoint_count);
    return view;
}

std::string format_replay_inspection_view(const ReplayInspectionView& view) {
    std::ostringstream stream;
    stream << "Replay inspection: mode=" << view.metadata.mode_id
           << " inputs=" << view.total_input_frames << "/" << view.total_input_frames_recorded
           << " checkpoints=" << view.total_checkpoints << "/" << view.total_checkpoints_recorded
           << " judgements=" << view.total_judgement_samples << "/" << view.total_judgement_samples_recorded;
    if (view.input_frames_truncated || view.checkpoints_truncated || view.judgement_samples_truncated) {
        stream << " truncated="
               << "input=" << view.input_frames_truncated
               << ",ckpt=" << view.checkpoints_truncated
               << ",judge=" << view.judgement_samples_truncated;
    }

    if (view.judgement_summary.sample_count > 0) {
        const ReplayJudgementSummary& summary = view.judgement_summary;
        stream << "\n  judgement: count=" << summary.sample_count
               << " perfect=" << summary.judgement_counts[static_cast<std::size_t>(rhythm::TimingJudgement::Perfect)]
               << " great=" << summary.judgement_counts[static_cast<std::size_t>(rhythm::TimingJudgement::Great)]
               << " good=" << summary.judgement_counts[static_cast<std::size_t>(rhythm::TimingJudgement::Good)]
               << " miss=" << summary.judgement_counts[static_cast<std::size_t>(rhythm::TimingJudgement::Miss)]
               << " early=" << summary.early_count
               << " late=" << summary.late_count
               << " mean-err-ms=" << std::fixed << std::setprecision(2)
               << static_cast<double>(summary.mean_corrected_error_microseconds) / 1000.0
               << " mean-abs-err-ms="
               << static_cast<double>(summary.mean_absolute_corrected_error_microseconds) / 1000.0
               << " min-err-ms="
               << static_cast<double>(summary.min_corrected_error_microseconds) / 1000.0
               << " max-err-ms="
               << static_cast<double>(summary.max_corrected_error_microseconds) / 1000.0
               << " accuracy=" << summary.accuracy_ratio;
    }

    const ReplayJudgementOffsetHistogram& histogram = view.judgement_offset_histogram;
    if (histogram.total_samples > 0) {
        stream << "\n  offsets[" << histogram.first_bucket_min_microseconds / 1000 << "..."
               << histogram.last_bucket_max_microseconds / 1000 << "ms,bucket="
               << histogram.bucket_microseconds / 1000 << "ms]";
        stream << " <" << histogram.below_range_count;
        for (std::uint32_t count : histogram.bucket_counts) {
            stream << " " << count;
        }
        stream << " >" << histogram.above_range_count;
    }

    if (!view.recent_input_timeline.empty()) {
        stream << "\n  recent-inputs:";
        for (const ReplayInputTimelineRow& row : view.recent_input_timeline) {
            stream << " [f" << row.frame_index << " evt=" << row.total_event_count
                   << " kb=" << row.keyboard_event_count
                   << " mouse=" << row.mouse_event_count
                   << " pad=" << row.gamepad_event_count << "]";
        }
    }

    if (!view.recent_judgements.empty()) {
        stream << "\n  recent-judgements:";
        for (const ReplayJudgementSample& sample : view.recent_judgements) {
            stream << " [s" << sample.simulation_step
                   << " ch" << sample.channel_index
                   << " " << rhythm::to_string(sample.judgement)
                   << " err=" << std::fixed << std::setprecision(2)
                   << static_cast<double>(sample.corrected_error_microseconds) / 1000.0 << "ms]";
        }
    }

    if (view.checkpoint_summary.total_in_session > 0) {
        stream << "\n  recent-checkpoints:";
        for (const ReplayCheckpoint& checkpoint : view.checkpoint_summary.recent) {
            stream << " [s" << checkpoint.simulation_step << " " << checkpoint.label
                   << " hash=0x" << std::hex << checkpoint.authoritative_state_hash << std::dec << "]";
        }
    }

    return stream.str();
}

} // namespace reaktio::gameplay
