#include "reaktio/content/ChartDataModel.hpp"

#include <algorithm>

namespace reaktio::content {

namespace {

const TimedEventData& cue_event_data(const RoutedCueData& cue) noexcept {
    return cue.event;
}

} // namespace

std::string_view to_string(ChartEventKind kind) noexcept {
    switch (kind) {
    case ChartEventKind::Note:
        return "note";
    case ChartEventKind::Hold:
        return "hold";
    case ChartEventKind::Hazard:
        return "hazard";
    case ChartEventKind::Trigger:
        return "trigger";
    case ChartEventKind::Camera:
        return "camera";
    case ChartEventKind::TextPrompt:
        return "text-prompt";
    case ChartEventKind::Vfx:
        return "vfx";
    }

    return "unknown";
}

ChartEventKind event_kind(const ChartEvent& event) noexcept {
    return std::visit(
        [](const auto& value) noexcept -> ChartEventKind {
            using EventType = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<EventType, NoteCue>) {
                return ChartEventKind::Note;
            } else if constexpr (std::is_same_v<EventType, HoldCue>) {
                return ChartEventKind::Hold;
            } else if constexpr (std::is_same_v<EventType, HazardCue>) {
                return ChartEventKind::Hazard;
            } else if constexpr (std::is_same_v<EventType, TriggerEvent>) {
                return ChartEventKind::Trigger;
            } else if constexpr (std::is_same_v<EventType, CameraEvent>) {
                return ChartEventKind::Camera;
            } else if constexpr (std::is_same_v<EventType, TextPromptEvent>) {
                return ChartEventKind::TextPrompt;
            } else {
                return ChartEventKind::Vfx;
            }
        },
        event);
}

const std::string& event_id(const ChartEvent& event) noexcept {
    return std::visit(
        [](const auto& value) noexcept -> const std::string& {
            using EventType = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<EventType, NoteCue> || std::is_same_v<EventType, HoldCue> ||
                          std::is_same_v<EventType, HazardCue>) {
                return value.cue.event.id;
            } else {
                return value.event.id;
            }
        },
        event);
}

const TimedEventData& timed_event_data(const ChartEvent& event) noexcept {
    return std::visit(
        [](const auto& value) noexcept -> const TimedEventData& {
            using EventType = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<EventType, NoteCue> || std::is_same_v<EventType, HoldCue> ||
                          std::is_same_v<EventType, HazardCue>) {
                return cue_event_data(value.cue);
            } else {
                return value.event;
            }
        },
        event);
}

rhythm::ChartTick event_start_tick(const ChartEvent& event) noexcept {
    return timed_event_data(event).placement.start_tick;
}

rhythm::ChartTick event_duration_ticks(const ChartEvent& event) noexcept {
    return timed_event_data(event).placement.duration_ticks;
}

rhythm::ChartTick event_end_tick_exclusive(const ChartEvent& event) noexcept {
    return event_start_tick(event) + std::max<rhythm::ChartTick>(event_duration_ticks(event), 1);
}

bool is_interactive_event(const ChartEvent& event) noexcept {
    return is_interactive_event_kind(event_kind(event));
}

bool uses_lane_routing(const ChartEvent& event) noexcept {
    return std::visit(
        [](const auto& value) noexcept {
            using EventType = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<EventType, NoteCue> || std::is_same_v<EventType, HoldCue> ||
                          std::is_same_v<EventType, HazardCue>) {
                return value.cue.route.lane_index.has_value();
            } else {
                return false;
            }
        },
        event);
}

bool uses_channel_routing(const ChartEvent& event) noexcept {
    return std::visit(
        [](const auto& value) noexcept {
            using EventType = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<EventType, NoteCue> || std::is_same_v<EventType, HoldCue> ||
                          std::is_same_v<EventType, HazardCue>) {
                return value.cue.route.channel_index.has_value();
            } else {
                return false;
            }
        },
        event);
}

std::span<const AuthoredField> event_extensions(const ChartEvent& event) noexcept {
    return timed_event_data(event).extensions;
}

const ScrollProfileDefinition* find_scroll_profile(
    const ChartDocument& document,
    std::string_view scroll_profile_id) noexcept {
    if (scroll_profile_id.empty()) {
        return nullptr;
    }

    const auto it = std::find_if(
        document.scroll_profiles.begin(),
        document.scroll_profiles.end(),
        [scroll_profile_id](const ScrollProfileDefinition& profile) {
            return profile.id == scroll_profile_id;
        });
    return it != document.scroll_profiles.end() ? &(*it) : nullptr;
}

ChartDocumentSummary summarize_chart_document(const ChartDocument& document) noexcept {
    ChartDocumentSummary summary{};
    summary.scroll_profile_count = document.scroll_profiles.size();
    summary.event_count = document.events.size();

    for (const ChartEvent& event : document.events) {
        const ChartEventKind kind = event_kind(event);
        switch (kind) {
        case ChartEventKind::Note:
            ++summary.note_count;
            break;
        case ChartEventKind::Hold:
            ++summary.hold_count;
            break;
        case ChartEventKind::Hazard:
            ++summary.hazard_count;
            break;
        case ChartEventKind::Trigger:
            ++summary.trigger_count;
            break;
        case ChartEventKind::Camera:
            ++summary.camera_count;
            break;
        case ChartEventKind::TextPrompt:
            ++summary.text_prompt_count;
            break;
        case ChartEventKind::Vfx:
            ++summary.vfx_count;
            break;
        }

        if (is_interactive_event_kind(kind)) {
            ++summary.interactive_cue_count;
        }

        const rhythm::ChartTick start_tick = event_start_tick(event);
        const rhythm::ChartTick last_tick = event_end_tick_exclusive(event) - 1;
        if (summary.empty) {
            summary.first_event_tick = start_tick;
            summary.last_event_tick = last_tick;
            summary.empty = false;
            continue;
        }

        summary.first_event_tick = std::min(summary.first_event_tick, start_tick);
        summary.last_event_tick = std::max(summary.last_event_tick, last_tick);
    }

    return summary;
}

} // namespace reaktio::content