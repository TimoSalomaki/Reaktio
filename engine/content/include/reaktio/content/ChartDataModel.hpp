#pragma once

#include "reaktio/rhythm/TempoMap.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace reaktio::content {

using TimelineMilliseconds = std::int64_t;

struct AuthoredField {
    std::string key;
    std::string value;
};

using AuthoredFieldList = std::vector<AuthoredField>;

struct AuthoringMetadata {
    std::string schema;
    std::string id;
    std::string display_name;
    std::string author;
    std::string description;
    std::vector<std::string> tags;
    std::string source_revision;
};

struct ChartAudioBinding {
    std::string clip_id;
    TimelineMilliseconds preview_start_ms{};
    TimelineMilliseconds preview_end_ms{};
    TimelineMilliseconds lead_in_ms{};
    TimelineMilliseconds tail_out_ms{};
};

struct ScrollProfileDefinition {
    std::string id;
    double units_per_second{};
    rhythm::ChartTick spawn_lead_ticks{};
    rhythm::ChartTick release_tail_ticks{};
    AuthoredFieldList extensions;
};

struct TimelinePlacement {
    rhythm::ChartTick start_tick{};
    rhythm::ChartTick duration_ticks{};
};

struct TimelineRoute {
    std::optional<std::uint32_t> lane_index;
    std::optional<std::uint32_t> channel_index;
};

struct TimedEventData {
    std::string id;
    TimelinePlacement placement;
    AuthoredFieldList extensions;
};

struct RoutedCueData {
    TimedEventData event;
    TimelineRoute route;
    std::string scroll_profile_id;
    std::string judgement_profile_id;
};

struct NoteCue {
    RoutedCueData cue;
};

struct HoldCue {
    RoutedCueData cue;
};

struct HazardCue {
    RoutedCueData cue;
    std::string hazard_profile_id;
};

struct TriggerEvent {
    TimedEventData event;
    std::string trigger_id;
    std::string payload;
};

struct CameraEvent {
    TimedEventData event;
    std::string camera_action_id;
    std::string payload;
};

struct TextPromptEvent {
    TimedEventData event;
    std::string prompt_text;
    std::string prompt_token;
    std::string locale_table_id;
};

struct VfxEvent {
    TimedEventData event;
    std::string effect_id;
    std::string payload;
};

enum class ChartEventKind : std::uint8_t {
    Note,
    Hold,
    Hazard,
    Trigger,
    Camera,
    TextPrompt,
    Vfx,
};

using ChartEvent = std::variant<NoteCue, HoldCue, HazardCue, TriggerEvent, CameraEvent, TextPromptEvent, VfxEvent>;

struct ChartDocument {
    AuthoringMetadata metadata;
    ChartAudioBinding audio;
    rhythm::TempoMapDefinition tempo_map;
    TimelineMilliseconds beat_zero_offset_ms{};
    std::string default_scroll_profile_id;
    std::vector<ScrollProfileDefinition> scroll_profiles;
    std::vector<ChartEvent> events;
};

struct ChartDocumentSummary {
    std::size_t scroll_profile_count{};
    std::size_t event_count{};
    std::size_t note_count{};
    std::size_t hold_count{};
    std::size_t hazard_count{};
    std::size_t trigger_count{};
    std::size_t camera_count{};
    std::size_t text_prompt_count{};
    std::size_t vfx_count{};
    std::size_t interactive_cue_count{};
    rhythm::ChartTick first_event_tick{};
    rhythm::ChartTick last_event_tick{};
    bool empty{true};
};

[[nodiscard]] constexpr bool is_interactive_event_kind(ChartEventKind kind) noexcept {
    return kind == ChartEventKind::Note || kind == ChartEventKind::Hold || kind == ChartEventKind::Hazard;
}

[[nodiscard]] std::string_view to_string(ChartEventKind kind) noexcept;
[[nodiscard]] ChartEventKind event_kind(const ChartEvent& event) noexcept;
[[nodiscard]] const std::string& event_id(const ChartEvent& event) noexcept;
[[nodiscard]] const TimedEventData& timed_event_data(const ChartEvent& event) noexcept;
[[nodiscard]] rhythm::ChartTick event_start_tick(const ChartEvent& event) noexcept;
[[nodiscard]] rhythm::ChartTick event_duration_ticks(const ChartEvent& event) noexcept;
[[nodiscard]] rhythm::ChartTick event_end_tick_exclusive(const ChartEvent& event) noexcept;
[[nodiscard]] bool is_interactive_event(const ChartEvent& event) noexcept;
[[nodiscard]] bool uses_lane_routing(const ChartEvent& event) noexcept;
[[nodiscard]] bool uses_channel_routing(const ChartEvent& event) noexcept;
[[nodiscard]] std::span<const AuthoredField> event_extensions(const ChartEvent& event) noexcept;
[[nodiscard]] const ScrollProfileDefinition* find_scroll_profile(
    const ChartDocument& document,
    std::string_view scroll_profile_id) noexcept;
[[nodiscard]] ChartDocumentSummary summarize_chart_document(const ChartDocument& document) noexcept;

} // namespace reaktio::content