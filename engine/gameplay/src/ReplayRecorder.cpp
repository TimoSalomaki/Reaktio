#include "reaktio/gameplay/ReplayRecorder.hpp"

#include "reaktio/platform/FrameClock.hpp"

#include <utility>

namespace reaktio::gameplay {

void ReplayRecorder::begin_session(ReplaySessionMetadata metadata) {
    session_ = std::move(metadata);
    session_active_ = true;
    input_frames_.clear();
    checkpoints_.clear();
}

void ReplayRecorder::reset() noexcept {
    session_ = ReplaySessionMetadata{};
    session_active_ = false;
    input_frames_.clear();
    checkpoints_.clear();
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

    if (input_frames_.size() > k_max_input_frames) {
        input_frames_.pop_front();
    }
}

void ReplayRecorder::record_checkpoint(ReplayCheckpoint checkpoint) {
    if (!session_active_) {
        return;
    }

    checkpoints_.push_back(std::move(checkpoint));
    if (checkpoints_.size() > k_max_checkpoints) {
        checkpoints_.pop_front();
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

std::size_t ReplayRecorder::input_frame_count() const noexcept {
    return input_frames_.size();
}

std::size_t ReplayRecorder::checkpoint_count() const noexcept {
    return checkpoints_.size();
}

} // namespace reaktio::gameplay