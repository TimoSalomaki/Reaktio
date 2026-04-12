#pragma once

#include "reaktio/gameplay/Transport.hpp"

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

using EventPayload = std::variant<ModeLifecycleEvent, TransportEvent, ReplayCheckpointEvent, DiagnosticEvent>;

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

} // namespace reaktio::gameplay