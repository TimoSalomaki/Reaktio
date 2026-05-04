#include "reaktio/gameplay/ModeFlow.hpp"

#include <utility>

namespace reaktio::gameplay {

namespace {

bool state_allows_pause(ModeFlowState state) noexcept {
    return state == ModeFlowState::Playing;
}

bool state_allows_resume(ModeFlowState state) noexcept {
    return state == ModeFlowState::Paused;
}

bool state_allows_restart(ModeFlowState state) noexcept {
    switch (state) {
    case ModeFlowState::Playing:
    case ModeFlowState::Paused:
    case ModeFlowState::Failed:
    case ModeFlowState::Cleared:
    case ModeFlowState::Results:
        return true;
    case ModeFlowState::Idle:
    case ModeFlowState::Aborted:
        return false;
    }
    return false;
}

bool state_allows_fail(ModeFlowState state) noexcept {
    return state == ModeFlowState::Playing || state == ModeFlowState::Paused;
}

bool state_allows_succeed(ModeFlowState state) noexcept {
    return state == ModeFlowState::Playing || state == ModeFlowState::Paused;
}

bool state_allows_present_results(ModeFlowState state) noexcept {
    switch (state) {
    case ModeFlowState::Failed:
    case ModeFlowState::Cleared:
    case ModeFlowState::Aborted:
        return true;
    default:
        return false;
    }
}

bool state_allows_toggle_practice(ModeFlowState state) noexcept {
    switch (state) {
    case ModeFlowState::Idle:
    case ModeFlowState::Playing:
    case ModeFlowState::Paused:
    case ModeFlowState::Results:
        return true;
    default:
        return false;
    }
}

} // namespace

void ModeFlowController::reset(ModeFlowFlags initial_flags) noexcept {
    snapshot_ = ModeFlowSnapshot{};
    snapshot_.flags = initial_flags;
    last_transition_record_ = {};
    has_last_transition_ = false;
}

void ModeFlowController::set_clock(std::uint64_t simulation_step, std::uint64_t frame_index) noexcept {
    snapshot_.simulation_step = simulation_step;
    snapshot_.frame_index = frame_index;
}

bool ModeFlowController::begin(ModeFlowReason reason) {
    if (snapshot_.state != ModeFlowState::Idle) {
        return false;
    }

    return record_transition(ModeFlowTransition::Begin, ModeFlowState::Playing, reason);
}

bool ModeFlowController::pause(ModeFlowReason reason) {
    if (!state_allows_pause(snapshot_.state)) {
        return false;
    }

    return record_transition(ModeFlowTransition::Pause, ModeFlowState::Paused, reason);
}

bool ModeFlowController::resume(ModeFlowReason reason) {
    if (!state_allows_resume(snapshot_.state)) {
        return false;
    }

    return record_transition(ModeFlowTransition::Resume, ModeFlowState::Playing, reason);
}

bool ModeFlowController::restart(ModeFlowReason reason) {
    if (!state_allows_restart(snapshot_.state)) {
        return false;
    }

    snapshot_.results = ModeFlowResults{};
    return record_transition(ModeFlowTransition::Restart, ModeFlowState::Playing, reason);
}

bool ModeFlowController::fail(ModeFlowReason reason) {
    if (!state_allows_fail(snapshot_.state)) {
        return false;
    }

    if (snapshot_.flags.no_fail_active) {
        return false;
    }

    return record_transition(ModeFlowTransition::Fail, ModeFlowState::Failed, reason);
}

bool ModeFlowController::succeed(ModeFlowReason reason) {
    if (!state_allows_succeed(snapshot_.state)) {
        return false;
    }

    return record_transition(ModeFlowTransition::Succeed, ModeFlowState::Cleared, reason);
}

bool ModeFlowController::present_results(ScoreSummary summary, std::string label, ModeFlowReason reason) {
    if (!state_allows_present_results(snapshot_.state)) {
        return false;
    }

    snapshot_.results = ModeFlowResults{
        .present = true,
        .score_summary = std::move(summary),
        .simulation_step = snapshot_.simulation_step,
        .frame_index = snapshot_.frame_index,
        .label = std::move(label),
    };
    return record_transition(ModeFlowTransition::PresentResults, ModeFlowState::Results, reason);
}

bool ModeFlowController::dismiss_results(ModeFlowReason reason) {
    if (snapshot_.state != ModeFlowState::Results) {
        return false;
    }

    snapshot_.results.present = false;
    return record_transition(ModeFlowTransition::DismissResults, ModeFlowState::Idle, reason);
}

bool ModeFlowController::set_practice_active(bool practice_active, ModeFlowReason reason) {
    if (!state_allows_toggle_practice(snapshot_.state)) {
        return false;
    }

    if (snapshot_.flags.practice_active == practice_active) {
        return false;
    }

    snapshot_.flags.practice_active = practice_active;
    return record_transition(ModeFlowTransition::TogglePractice, snapshot_.state, reason);
}

void ModeFlowController::update_modifier_flags(bool no_fail, bool autoplay) noexcept {
    snapshot_.flags.no_fail_active = no_fail;
    snapshot_.flags.autoplay_active = autoplay;
}

bool ModeFlowController::abort(ModeFlowReason reason) {
    if (snapshot_.state == ModeFlowState::Idle || snapshot_.state == ModeFlowState::Aborted) {
        return false;
    }

    return record_transition(ModeFlowTransition::Abort, ModeFlowState::Aborted, reason);
}

const ModeFlowSnapshot& ModeFlowController::snapshot() const noexcept {
    return snapshot_;
}

const ModeFlowTransitionRecord* ModeFlowController::last_transition() const noexcept {
    return has_last_transition_ ? &last_transition_record_ : nullptr;
}

bool ModeFlowController::can_pause() const noexcept {
    return state_allows_pause(snapshot_.state);
}

bool ModeFlowController::can_resume() const noexcept {
    return state_allows_resume(snapshot_.state);
}

bool ModeFlowController::can_restart() const noexcept {
    return state_allows_restart(snapshot_.state);
}

bool ModeFlowController::can_fail() const noexcept {
    return state_allows_fail(snapshot_.state) && !snapshot_.flags.no_fail_active;
}

bool ModeFlowController::can_succeed() const noexcept {
    return state_allows_succeed(snapshot_.state);
}

bool ModeFlowController::can_present_results() const noexcept {
    return state_allows_present_results(snapshot_.state);
}

bool ModeFlowController::can_toggle_practice() const noexcept {
    return state_allows_toggle_practice(snapshot_.state);
}

bool ModeFlowController::record_transition(
    ModeFlowTransition transition,
    ModeFlowState target_state,
    ModeFlowReason reason) noexcept {
    const ModeFlowState previous_state = snapshot_.state;
    snapshot_.state = target_state;
    snapshot_.last_transition = transition;
    snapshot_.last_reason = reason;
    ++snapshot_.transition_count;

    last_transition_record_ = ModeFlowTransitionRecord{
        .transition = transition,
        .from = previous_state,
        .to = target_state,
        .reason = reason,
        .simulation_step = snapshot_.simulation_step,
        .frame_index = snapshot_.frame_index,
    };
    has_last_transition_ = true;
    return true;
}

} // namespace reaktio::gameplay
