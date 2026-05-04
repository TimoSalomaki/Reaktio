#pragma once

#include "reaktio/content/ChartDataModel.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace reaktio::content {

struct CookedChartDependencyRecord {
    std::filesystem::path path;
    std::string hash;
};

struct CookedChartManifestRecord {
    std::string authoring_id;
    std::string runtime_label;
    std::filesystem::path source_path;
    std::string source_hash;
    std::filesystem::path payload_path;
    std::string payload_hash;
    std::vector<CookedChartDependencyRecord> dependencies;
};

struct ChartPreviewWindow {
    rhythm::ChartTick before_ticks{};
    rhythm::ChartTick after_ticks{};
    std::size_t max_events{16};
};

enum class ChartPreviewEventRelation : std::uint8_t {
    Past,
    Active,
    Upcoming,
};

struct ChartPreviewEvent {
    ChartEventKind kind{};
    std::string id;
    ChartPreviewEventRelation relation{ChartPreviewEventRelation::Upcoming};
    rhythm::ChartTick start_tick{};
    rhythm::ChartTick end_tick_exclusive{};
    rhythm::ChartTick delta_ticks{};
    double delta_seconds{};
    rhythm::RhythmPosition start_position{};
    rhythm::RhythmPosition end_position{};
    std::optional<std::uint32_t> lane_index;
    std::optional<std::uint32_t> channel_index;
    std::string scroll_profile_id;
    std::string judgement_profile_id;
    std::string detail;
};

struct ChartPreviewSnapshot {
    ChartDocumentSummary summary;
    rhythm::RhythmPosition cursor;
    rhythm::ChartTick window_start_tick{};
    rhythm::ChartTick window_end_tick{};
    std::size_t total_window_event_count{};
    bool truncated{};
    std::vector<ChartPreviewEvent> events;
};

[[nodiscard]] std::optional<std::filesystem::path> find_default_cooked_chart_manifest_path();

[[nodiscard]] bool load_cooked_chart_manifest(
    const std::filesystem::path& manifest_path,
    std::vector<CookedChartManifestRecord>& records,
    std::string& error_message);

[[nodiscard]] bool load_cooked_chart_document(
    const std::filesystem::path& cooked_chart_path,
    ChartDocument& document,
    std::string& error_message);

[[nodiscard]] bool build_chart_preview_snapshot(
    const ChartDocument& document,
    rhythm::ChartTick cursor_tick,
    const ChartPreviewWindow& window,
    ChartPreviewSnapshot& snapshot,
    std::string& error_message);

[[nodiscard]] std::string_view to_string(ChartPreviewEventRelation relation) noexcept;

} // namespace reaktio::content