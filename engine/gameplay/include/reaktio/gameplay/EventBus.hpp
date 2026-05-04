#pragma once

#include "reaktio/gameplay/IGameMode.hpp"
#include "reaktio/gameplay/ModeFlow.hpp"
#include "reaktio/gameplay/Transport.hpp"

#include "reaktio/content/HotReload.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <variant>

namespace reaktio::gameplay {

enum class ModeLifecyclePhase {
    Entering,
    Entered,
    Exiting,
    Exited,
};

struct ModeLifecycleEvent {
    std::string mode_id;
    ModeLifecyclePhase phase{ModeLifecyclePhase::Entered};
    ModeLifecycleReason reason{ModeLifecycleReason::Startup};
    std::uint32_t api_version{};
};

struct TransportEvent {
    std::string action;
    TransportPlaybackState playback_state{TransportPlaybackState::Stopped};
    TransportPlaybackMode playback_mode{TransportPlaybackMode::Normal};
    TransportPositionAuthority position_authority{TransportPositionAuthority::Simulation};
    double position_seconds{};
    bool loop_enabled{};
    bool preview_enabled{};
    std::uint64_t timeline_revision{};
    TransportDiscontinuityReason discontinuity_reason{TransportDiscontinuityReason::None};
};

struct ReplayCheckpointEvent {
    std::string label;
    std::uint64_t simulation_step{};
    std::uint64_t authoritative_state_hash{};
    std::size_t checkpoint_count{};
};

struct DiagnosticEvent {
    std::string message;
};

enum class ContentHotReloadStatus : std::uint8_t {
    Reloaded,
    Pending,
    Failed,
};

struct ContentHotReloadEvent {
    content::HotReloadAssetFamily family{content::HotReloadAssetFamily::Charts};
    ContentHotReloadStatus status{ContentHotReloadStatus::Reloaded};
    std::filesystem::path trigger_path;
    std::size_t changed_file_count{};
    std::uint64_t revision{};
    std::string detail;
};

struct ModeFlowEvent {
    ModeFlowTransition transition{ModeFlowTransition::Begin};
    ModeFlowState from{ModeFlowState::Idle};
    ModeFlowState to{ModeFlowState::Idle};
    ModeFlowReason reason{ModeFlowReason::None};
    bool practice_active{};
    bool no_fail_active{};
    bool autoplay_active{};
};

using EventPayload = std::variant<ModeLifecycleEvent, TransportEvent, ReplayCheckpointEvent, DiagnosticEvent, ContentHotReloadEvent, ModeFlowEvent>;

struct EventRecord {
    std::uint64_t sequence{};
    std::uint64_t frame_index{};
    std::uint64_t simulation_step{};
    std::string source;
    EventPayload payload;
};

class EventBus {
  public:
    void reset() noexcept;
    void publish(
        std::string_view source,
        std::uint64_t frame_index,
        std::uint64_t simulation_step,
        EventPayload payload);

    [[nodiscard]] const EventRecord* last() const noexcept;
    [[nodiscard]] const std::deque<EventRecord>& history() const noexcept;
    [[nodiscard]] std::size_t count() const noexcept;
    [[nodiscard]] std::uint64_t published_count() const noexcept;

  private:
    static constexpr std::size_t k_max_events = 512;

    std::uint64_t next_sequence_{1};
    std::deque<EventRecord> history_;
};

[[nodiscard]] constexpr std::string_view to_string(ModeLifecyclePhase phase) noexcept {
    switch (phase) {
    case ModeLifecyclePhase::Entering:
        return "entering";
    case ModeLifecyclePhase::Entered:
        return "entered";
    case ModeLifecyclePhase::Exiting:
        return "exiting";
    case ModeLifecyclePhase::Exited:
        return "exited";
    }

    return "unknown";
}

[[nodiscard]] std::string describe_event(const EventRecord& event);

[[nodiscard]] constexpr std::string_view to_string(ContentHotReloadStatus status) noexcept {
    switch (status) {
    case ContentHotReloadStatus::Reloaded:
        return "reloaded";
    case ContentHotReloadStatus::Pending:
        return "pending";
    case ContentHotReloadStatus::Failed:
        return "failed";
    }

    return "unknown";
}

} // namespace reaktio::gameplay