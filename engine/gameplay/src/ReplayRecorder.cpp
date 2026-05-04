#include "reaktio/gameplay/ReplayRecorder.hpp"

#include "reaktio/platform/FrameClock.hpp"

#include <utility>

namespace reaktio::gameplay {

void ReplayRecorder::begin_session(ReplaySessionMetadata metadata) {
    session_ = std::move(metadata);
    session_active_ = true;
    input_frames_.clear();
    checkpoints_.clear();
    judgement_samples_.clear();
    total_input_frames_recorded_ = 0;
    total_checkpoints_recorded_ = 0;
    total_judgement_samples_recorded_ = 0;
}

void ReplayRecorder::reset() noexcept {
    session_ = ReplaySessionMetadata{};
    session_active_ = false;
    input_frames_.clear();
    checkpoints_.clear();
    judgement_samples_.clear();
    total_input_frames_recorded_ = 0;
    total_checkpoints_recorded_ = 0;
    total_judgement_samples_recorded_ = 0;
}

void ReplayRecorder::record_input_frame(
    const platform::FrameTiming& frame_timing,
    const platform::InputSnapshot& input_snapshot) {
    if (!session_active_) {
        return;
    }

    input_frames_.push_back(ReplayInputFrame{
        .frame_index = frame_timing.frame_index,
        .wall_clock_ns = frame_timing.wall_clock_ns,
        .frame_delta_seconds = frame_timing.frame_delta_seconds,
        .interpolation_alpha = frame_timing.interpolation_alpha,
        .keyboard_events = input_snapshot.keyboard_events(),
        .text_editing_events = input_snapshot.text_editing_events(),
        .text_editing_candidates_events = input_snapshot.text_editing_candidates_events(),
        .text_input_events = input_snapshot.text_input_events(),
        .mouse_motion_events = input_snapshot.mouse_motion_events(),
        .mouse_button_events = input_snapshot.mouse_button_events(),
        .mouse_wheel_events = input_snapshot.mouse_wheel_events(),
        .gamepad_connection_events = input_snapshot.gamepad_connection_events(),
        .gamepad_axis_events = input_snapshot.gamepad_axis_events(),
        .gamepad_button_events = input_snapshot.gamepad_button_events(),
    });
    ++total_input_frames_recorded_;

    if (input_frames_.size() > k_max_input_frames) {
        input_frames_.pop_front();
    }
}

void ReplayRecorder::record_checkpoint(ReplayCheckpoint checkpoint) {
    if (!session_active_) {
        return;
    }

    checkpoints_.push_back(std::move(checkpoint));
    ++total_checkpoints_recorded_;
    if (checkpoints_.size() > k_max_checkpoints) {
        checkpoints_.pop_front();
    }
}

void ReplayRecorder::record_judgement_sample(ReplayJudgementSample sample) {
    if (!session_active_) {
        return;
    }

    judgement_samples_.push_back(std::move(sample));
    ++total_judgement_samples_recorded_;
    if (judgement_samples_.size() > k_max_judgement_samples) {
        judgement_samples_.pop_front();
    }
}

const ReplaySessionMetadata* ReplayRecorder::session() const noexcept {
    return session_active_ ? &session_ : nullptr;
}

const ReplayInputFrame* ReplayRecorder::last_input_frame() const noexcept {
    return input_frames_.empty() ? nullptr : &input_frames_.back();
}

const ReplayCheckpoint* ReplayRecorder::last_checkpoint() const noexcept {
    return checkpoints_.empty() ? nullptr : &checkpoints_.back();
}

const ReplayJudgementSample* ReplayRecorder::last_judgement_sample() const noexcept {
    return judgement_samples_.empty() ? nullptr : &judgement_samples_.back();
}

std::size_t ReplayRecorder::input_frame_count() const noexcept {
    return input_frames_.size();
}

std::size_t ReplayRecorder::checkpoint_count() const noexcept {
    return checkpoints_.size();
}

std::size_t ReplayRecorder::judgement_sample_count() const noexcept {
    return judgement_samples_.size();
}

std::uint64_t ReplayRecorder::total_input_frames_recorded() const noexcept {
    return total_input_frames_recorded_;
}

std::uint64_t ReplayRecorder::total_checkpoints_recorded() const noexcept {
    return total_checkpoints_recorded_;
}

std::uint64_t ReplayRecorder::total_judgement_samples_recorded() const noexcept {
    return total_judgement_samples_recorded_;
}

const std::deque<ReplayInputFrame>& ReplayRecorder::input_frames() const noexcept {
    return input_frames_;
}

const std::deque<ReplayCheckpoint>& ReplayRecorder::checkpoints() const noexcept {
    return checkpoints_;
}

const std::deque<ReplayJudgementSample>& ReplayRecorder::judgement_samples() const noexcept {
    return judgement_samples_;
}

ReplaySession make_replay_session(const ReplayRecorder& recorder) {
    ReplaySession session{};
    if (const ReplaySessionMetadata* metadata = recorder.session(); metadata != nullptr) {
        session.metadata = *metadata;
    }
    session.input_frames.reserve(recorder.input_frame_count());
    for (const ReplayInputFrame& frame : recorder.input_frames()) {
        session.input_frames.push_back(frame);
    }
    session.checkpoints.reserve(recorder.checkpoint_count());
    for (const ReplayCheckpoint& checkpoint : recorder.checkpoints()) {
        session.checkpoints.push_back(checkpoint);
    }
    session.judgement_samples.reserve(recorder.judgement_sample_count());
    for (const ReplayJudgementSample& sample : recorder.judgement_samples()) {
        session.judgement_samples.push_back(sample);
    }
    session.total_input_frames_recorded = recorder.total_input_frames_recorded();
    session.total_checkpoints_recorded = recorder.total_checkpoints_recorded();
    session.total_judgement_samples_recorded = recorder.total_judgement_samples_recorded();
    session.input_frames_truncated = session.total_input_frames_recorded > session.input_frames.size();
    session.checkpoints_truncated = session.total_checkpoints_recorded > session.checkpoints.size();
    session.judgement_samples_truncated =
        session.total_judgement_samples_recorded > session.judgement_samples.size();
    return session;
}

} // namespace reaktio::gameplay