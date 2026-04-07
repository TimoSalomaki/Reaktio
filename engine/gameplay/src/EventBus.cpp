#include "reaktio/gameplay/EventBus.hpp"

#include <sstream>
#include <type_traits>
#include <utility>

namespace reaktio::gameplay {

void EventBus::reset() noexcept {
    next_sequence_ = 1;
    history_.clear();
}

void EventBus::publish(
    std::string_view source,
    std::uint64_t frame_index,
    std::uint64_t simulation_step,
    EventPayload payload) {
    history_.push_back(EventRecord{
        .sequence = next_sequence_++,
        .frame_index = frame_index,
        .simulation_step = simulation_step,
        .source = std::string(source),
        .payload = std::move(payload),
    });

    if (history_.size() > k_max_events) {
        history_.pop_front();
    }
}

const EventRecord* EventBus::last() const noexcept {
    return history_.empty() ? nullptr : &history_.back();
}

const std::deque<EventRecord>& EventBus::history() const noexcept {
    return history_;
}

std::size_t EventBus::count() const noexcept {
    return history_.size();
}

std::uint64_t EventBus::published_count() const noexcept {
    return next_sequence_ - 1;
}

std::string describe_event(const EventRecord& event) {
    return std::visit(
        [&event](const auto& payload) {
            using PayloadType = std::decay_t<decltype(payload)>;
            std::ostringstream stream;
            stream << event.source << '#';

            if constexpr (std::is_same_v<PayloadType, ModeLifecycleEvent>) {
                stream << "mode:" << payload.mode_id << ' ' << to_string(payload.phase);
            } else if constexpr (std::is_same_v<PayloadType, TransportEvent>) {
                stream << "transport:" << payload.action << ' ' << to_string(payload.playback_state)
                       << " @" << payload.position_seconds << " loop=" << payload.loop_enabled;
            } else if constexpr (std::is_same_v<PayloadType, ReplayCheckpointEvent>) {
                stream << "replay:" << payload.label << " step=" << payload.simulation_step
                       << " checkpoints=" << payload.checkpoint_count << " hash=0x" << std::hex
                       << payload.authoritative_state_hash;
            } else {
                stream << "diagnostic:" << payload.message;
            }

            return stream.str();
        },
        event.payload);
}

} // namespace reaktio::gameplay