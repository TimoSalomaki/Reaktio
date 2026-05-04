#include "reaktio/gameplay/PresentationEvents.hpp"

#include <utility>

namespace reaktio::gameplay {

namespace {

template <typename QueueT, typename EventT>
bool publish_with_capacity(
    QueueT& queue,
    EventT event,
    std::uint64_t& counter,
    std::uint64_t& dropped) {
    if (queue.size() >= PresentationEventBus::k_per_kind_capacity) {
        ++dropped;
        return false;
    }

    queue.push_back(std::move(event));
    ++counter;
    return true;
}

} // namespace

void PresentationEventBus::reset() noexcept {
    camera_events_.clear();
    screen_effects_.clear();
    haptics_events_.clear();
    statistics_ = {};
}

void PresentationEventBus::clear() noexcept {
    camera_events_.clear();
    screen_effects_.clear();
    haptics_events_.clear();
}

bool PresentationEventBus::publish(CameraEvent event) {
    return publish_with_capacity(
        camera_events_,
        std::move(event),
        statistics_.camera_event_count,
        statistics_.dropped_event_count);
}

bool PresentationEventBus::publish(ScreenEffectEvent event) {
    return publish_with_capacity(
        screen_effects_,
        std::move(event),
        statistics_.screen_effect_count,
        statistics_.dropped_event_count);
}

bool PresentationEventBus::publish(HapticsEvent event) {
    return publish_with_capacity(
        haptics_events_,
        std::move(event),
        statistics_.haptics_event_count,
        statistics_.dropped_event_count);
}

std::span<const CameraEvent> PresentationEventBus::camera_events() const noexcept {
    return std::span<const CameraEvent>{camera_events_.data(), camera_events_.size()};
}

std::span<const ScreenEffectEvent> PresentationEventBus::screen_effects() const noexcept {
    return std::span<const ScreenEffectEvent>{screen_effects_.data(), screen_effects_.size()};
}

std::span<const HapticsEvent> PresentationEventBus::haptics_events() const noexcept {
    return std::span<const HapticsEvent>{haptics_events_.data(), haptics_events_.size()};
}

const PresentationEventStatistics& PresentationEventBus::statistics() const noexcept {
    return statistics_;
}

} // namespace reaktio::gameplay
