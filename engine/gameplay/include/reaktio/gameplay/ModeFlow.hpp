#pragma once

#include "reaktio/gameplay/Scoring.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace reaktio::gameplay {

enum class ModeFlowState : std::uint8_t {
    Idle,
    Playing,
    Paused,
    Failed,
    Cleared,
    Results,
    Aborted,
};

enum class ModeFlowReason : std::uint8_t {
    None,
    EnterMode,
    UserPause,
    UserResume,
    UserRestart,
    UserAbort,
    HealthDepleted,
    SongEnded,
    PracticeToggle,
    PresentResults,
    DismissResults,
    Custom,
};

enum class ModeFlowTransition : std::uint8_t {
    Begin,
    Pause,
    Resume,
    Restart,
    Fail,
    Succeed,
    PresentResults,
    DismissResults,
    TogglePractice,
    Abort,
};

struct ModeFlowFlags {
    bool practice_active{false};
    bool no_fail_active{false};
    bool autoplay_active{false};
};

struct ModeFlowResults {
    bool present{false};
    ScoreSummary score_summary{};
    std::uint64_t simulation_step{};
    std::uint64_t frame_index{};
    std::string label;
};

struct ModeFlowSnapshot {
    ModeFlowState state{ModeFlowState::Idle};
    ModeFlowReason last_reason{ModeFlowReason::None};
    ModeFlowTransition last_transition{ModeFlowTransition::Begin};
    ModeFlowFlags flags{};
    ModeFlowResults results{};
    std::uint64_t transition_count{};
    std::uint64_t simulation_step{};
    std::uint64_t frame_index{};
};

struct ModeFlowTransitionRecord {
    ModeFlowTransition transition{ModeFlowTransition::Begin};
    ModeFlowState from{ModeFlowState::Idle};
    ModeFlowState to{ModeFlowState::Idle};
    ModeFlowReason reason{ModeFlowReason::None};
    std::uint64_t simulation_step{};
    std::uint64_t frame_index{};
};

[[nodiscard]] constexpr std::string_view to_string(ModeFlowState state) noexcept {
    switch (state) {
    case ModeFlowState::Idle:
        return "idle";
    case ModeFlowState::Playing:
        return "playing";
    case ModeFlowState::Paused:
        return "paused";
    case ModeFlowState::Failed:
        return "failed";
    case ModeFlowState::Cleared:
        return "cleared";
    case ModeFlowState::Results:
        return "results";
    case ModeFlowState::Aborted:
        return "aborted";
    }

    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(ModeFlowReason reason) noexcept {
    switch (reason) {
    case ModeFlowReason::None:
        return "none";
    case ModeFlowReason::EnterMode:
        return "enter_mode";
    case ModeFlowReason::UserPause:
        return "user_pause";
    case ModeFlowReason::UserResume:
        return "user_resume";
    case ModeFlowReason::UserRestart:
        return "user_restart";
    case ModeFlowReason::UserAbort:
        return "user_abort";
    case ModeFlowReason::HealthDepleted:
        return "health_depleted";
    case ModeFlowReason::SongEnded:
        return "song_ended";
    case ModeFlowReason::PracticeToggle:
        return "practice_toggle";
    case ModeFlowReason::PresentResults:
        return "present_results";
    case ModeFlowReason::DismissResults:
        return "dismiss_results";
    case ModeFlowReason::Custom:
        return "custom";
    }

    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(ModeFlowTransition transition) noexcept {
    switch (transition) {
    case ModeFlowTransition::Begin:
        return "begin";
    case ModeFlowTransition::Pause:
        return "pause";
    case ModeFlowTransition::Resume:
        return "resume";
    case ModeFlowTransition::Restart:
        return "restart";
    case ModeFlowTransition::Fail:
        return "fail";
    case ModeFlowTransition::Succeed:
        return "succeed";
    case ModeFlowTransition::PresentResults:
        return "present_results";
    case ModeFlowTransition::DismissResults:
        return "dismiss_results";
    case ModeFlowTransition::TogglePractice:
        return "toggle_practice";
    case ModeFlowTransition::Abort:
        return "abort";
    }

    return "unknown";
}

class ModeFlowController {
  public:
    void reset(ModeFlowFlags initial_flags = {}) noexcept;
    void set_clock(std::uint64_t simulation_step, std::uint64_t frame_index) noexcept;

    bool begin(ModeFlowReason reason = ModeFlowReason::EnterMode);
    bool pause(ModeFlowReason reason = ModeFlowReason::UserPause);
    bool resume(ModeFlowReason reason = ModeFlowReason::UserResume);
    bool restart(ModeFlowReason reason = ModeFlowReason::UserRestart);
    bool fail(ModeFlowReason reason = ModeFlowReason::HealthDepleted);
    bool succeed(ModeFlowReason reason = ModeFlowReason::SongEnded);
    bool present_results(ScoreSummary summary, std::string label = {}, ModeFlowReason reason = ModeFlowReason::PresentResults);
    bool dismiss_results(ModeFlowReason reason = ModeFlowReason::DismissResults);
    bool set_practice_active(bool practice_active, ModeFlowReason reason = ModeFlowReason::PracticeToggle);
    void update_modifier_flags(bool no_fail, bool autoplay) noexcept;
    bool abort(ModeFlowReason reason = ModeFlowReason::UserAbort);

    [[nodiscard]] const ModeFlowSnapshot& snapshot() const noexcept;
    [[nodiscard]] const ModeFlowTransitionRecord* last_transition() const noexcept;
    [[nodiscard]] bool can_pause() const noexcept;
    [[nodiscard]] bool can_resume() const noexcept;
    [[nodiscard]] bool can_restart() const noexcept;
    [[nodiscard]] bool can_fail() const noexcept;
    [[nodiscard]] bool can_succeed() const noexcept;
    [[nodiscard]] bool can_present_results() const noexcept;
    [[nodiscard]] bool can_toggle_practice() const noexcept;

  private:
    bool record_transition(
        ModeFlowTransition transition,
        ModeFlowState target_state,
        ModeFlowReason reason) noexcept;

    ModeFlowSnapshot snapshot_{};
    ModeFlowTransitionRecord last_transition_record_{};
    bool has_last_transition_{};
};

} // namespace reaktio::gameplay
