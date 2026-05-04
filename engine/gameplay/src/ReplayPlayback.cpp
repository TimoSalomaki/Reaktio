#include "reaktio/gameplay/ReplayPlayback.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace reaktio::gameplay {

namespace {

std::string format_session_issue(std::string_view prefix, std::uint64_t value) {
    std::ostringstream stream;
    stream << prefix << value;
    return stream.str();
}

} // namespace

ReplayPlayer::ReplayPlayer(const ReplaySession& session) noexcept
    : session_(&session) {}

void ReplayPlayer::reset() noexcept {
    cursor_ = 0;
}

bool ReplayPlayer::finished() const noexcept {
    return session_ == nullptr || cursor_ >= session_->input_frames.size();
}

std::size_t ReplayPlayer::cursor() const noexcept {
    return cursor_;
}

std::size_t ReplayPlayer::remaining() const noexcept {
    if (session_ == nullptr || cursor_ >= session_->input_frames.size()) {
        return 0;
    }
    return session_->input_frames.size() - cursor_;
}

std::size_t ReplayPlayer::input_frame_count() const noexcept {
    return session_ != nullptr ? session_->input_frames.size() : 0;
}

std::size_t ReplayPlayer::checkpoint_count() const noexcept {
    return session_ != nullptr ? session_->checkpoints.size() : 0;
}

const ReplaySession& ReplayPlayer::session() const noexcept {
    return *session_;
}

const ReplayInputFrame* ReplayPlayer::peek() const noexcept {
    if (finished()) {
        return nullptr;
    }
    return &session_->input_frames[cursor_];
}

const ReplayInputFrame* ReplayPlayer::advance() noexcept {
    if (finished()) {
        return nullptr;
    }
    const ReplayInputFrame* frame = &session_->input_frames[cursor_];
    ++cursor_;
    return frame;
}

const ReplayInputFrame* ReplayPlayer::find_input_frame(std::uint64_t frame_index) const noexcept {
    if (session_ == nullptr) {
        return nullptr;
    }
    const auto it = std::find_if(
        session_->input_frames.begin(),
        session_->input_frames.end(),
        [frame_index](const ReplayInputFrame& frame) noexcept {
            return frame.frame_index == frame_index;
        });
    return it != session_->input_frames.end() ? &(*it) : nullptr;
}

const ReplayCheckpoint* ReplayPlayer::find_checkpoint_for_step(std::uint64_t simulation_step) const noexcept {
    if (session_ == nullptr) {
        return nullptr;
    }
    const auto it = std::find_if(
        session_->checkpoints.begin(),
        session_->checkpoints.end(),
        [simulation_step](const ReplayCheckpoint& checkpoint) noexcept {
            return checkpoint.simulation_step == simulation_step;
        });
    return it != session_->checkpoints.end() ? &(*it) : nullptr;
}

const ReplayCheckpoint* ReplayPlayer::find_checkpoint_with_label(std::string_view label) const noexcept {
    if (session_ == nullptr) {
        return nullptr;
    }
    const auto it = std::find_if(
        session_->checkpoints.begin(),
        session_->checkpoints.end(),
        [label](const ReplayCheckpoint& checkpoint) noexcept {
            return checkpoint.label == label;
        });
    return it != session_->checkpoints.end() ? &(*it) : nullptr;
}

ReplayValidationReport ReplayValidator::validate(const ReplaySession& session) const {
    ReplayValidationReport report{};
    report.input_frame_count = session.input_frames.size();
    report.checkpoint_count = session.checkpoints.size();
    report.total_input_frames_recorded = session.total_input_frames_recorded;
    report.total_checkpoints_recorded = session.total_checkpoints_recorded;
    report.input_frames_truncated = session.input_frames_truncated;
    report.checkpoints_truncated = session.checkpoints_truncated;

    bool any_failure = false;

    if (!session.input_frames.empty()) {
        report.first_frame_index = session.input_frames.front().frame_index;
        report.last_frame_index = session.input_frames.back().frame_index;

        for (std::size_t index = 1; index < session.input_frames.size(); ++index) {
            if (session.input_frames[index].frame_index <= session.input_frames[index - 1].frame_index) {
                ++report.monotonic_frame_violations;
            }
        }
    }

    if (!session.checkpoints.empty()) {
        report.first_simulation_step = session.checkpoints.front().simulation_step;
        report.last_simulation_step = session.checkpoints.back().simulation_step;

        std::unordered_map<std::uint64_t, std::size_t> step_seen;
        step_seen.reserve(session.checkpoints.size());
        for (std::size_t index = 0; index < session.checkpoints.size(); ++index) {
            const ReplayCheckpoint& checkpoint = session.checkpoints[index];
            if (index > 0 && checkpoint.simulation_step < session.checkpoints[index - 1].simulation_step) {
                ++report.monotonic_step_violations;
            }
            const auto [it, inserted] = step_seen.try_emplace(checkpoint.simulation_step, index);
            if (!inserted) {
                ++report.duplicate_step_count;
            }
        }
    }

    if (report.monotonic_frame_violations > 0) {
        report.issues.push_back(format_session_issue(
            "non-monotonic input frame indices count=", report.monotonic_frame_violations));
        any_failure = true;
    }
    if (report.monotonic_step_violations > 0) {
        report.issues.push_back(format_session_issue(
            "non-monotonic checkpoint steps count=", report.monotonic_step_violations));
        any_failure = true;
    }
    if (report.duplicate_step_count > 0) {
        report.issues.push_back(format_session_issue(
            "duplicate checkpoint simulation steps count=", report.duplicate_step_count));
        any_failure = true;
    }
    if (report.input_frames_truncated) {
        report.issues.emplace_back(
            "input-frame ring buffer truncated; raise capacity or persist before validation");
    }
    if (report.checkpoints_truncated) {
        report.issues.emplace_back(
            "checkpoint ring buffer truncated; raise capacity or persist before validation");
    }

    report.ok = !any_failure;
    return report;
}

ReplayDivergenceReport ReplayValidator::compare_observations(
    const ReplaySession& session,
    std::span<const ReplayObservedHash> observed_hashes) const {
    ReplayDivergenceReport report{};
    report.observed_count = observed_hashes.size();

    std::unordered_map<std::uint64_t, const ReplayCheckpoint*> by_step;
    by_step.reserve(session.checkpoints.size());
    for (const ReplayCheckpoint& checkpoint : session.checkpoints) {
        by_step[checkpoint.simulation_step] = &checkpoint;
    }

    std::unordered_map<std::uint64_t, std::size_t> observed_by_step;
    observed_by_step.reserve(observed_hashes.size());
    for (const ReplayObservedHash& observation : observed_hashes) {
        observed_by_step[observation.simulation_step] = observation.state_hash;
    }

    for (const ReplayCheckpoint& checkpoint : session.checkpoints) {
        const auto observed = observed_by_step.find(checkpoint.simulation_step);
        if (observed == observed_by_step.end()) {
            ++report.missing_observation_count;
            continue;
        }

        if (observed->second == checkpoint.authoritative_state_hash) {
            ++report.matched_count;
        } else {
            ++report.mismatched_count;
            if (report.mismatches.size() < ReplayValidator::k_max_recorded_mismatches) {
                report.mismatches.push_back(ReplayDivergence{
                    .simulation_step = checkpoint.simulation_step,
                    .expected_hash = checkpoint.authoritative_state_hash,
                    .observed_hash = observed->second,
                    .label = checkpoint.label,
                });
            }
        }
    }

    for (const ReplayObservedHash& observation : observed_hashes) {
        if (by_step.find(observation.simulation_step) == by_step.end()) {
            ++report.unexpected_observation_count;
        }
    }

    return report;
}

} // namespace reaktio::gameplay
