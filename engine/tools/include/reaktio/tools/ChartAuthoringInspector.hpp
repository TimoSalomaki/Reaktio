#pragma once

#include "reaktio/content/ChartDataModel.hpp"
#include "reaktio/tools/InspectorPanel.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace reaktio::tools {

// Authoring-grade chart inspector. Composes the existing
// ChartDocumentSummary + ChartPreviewSnapshot with author-facing
// diagnostics that the lightweight CLI preview tool didn't expose:
//
//   - Per-bar interactive-cue density: highlights overloaded bars and
//     long stretches with no cues.
//   - Channel / lane usage histograms: helps authors spot overloaded
//     channels and unused lanes.
//   - Hold-cue lifecycle table: long-running holds need careful
//     attention; the inspector reports the longest holds.
//   - Cross-reference checks: verifies that every routed cue's
//     scroll_profile_id references an actually-defined scroll profile,
//     and that routed cues without an explicit profile have a
//     default_scroll_profile_id available.
//
// Pure read-only over a ChartDocument; no IO. The CLI driver layer is
// the only thing that performs file IO and consumes this inspector.

struct ChartAuthoringDensityRow {
    std::int64_t bar_index{};
    std::uint32_t interactive_cues{};
};

struct ChartAuthoringLongDurationRow {
    content::ChartEventKind kind{};
    std::string id;
    rhythm::ChartTick start_tick{};
    rhythm::ChartTick end_tick_exclusive{};
    rhythm::ChartTick duration_ticks{};
    std::optional<std::uint32_t> lane_index;
    std::optional<std::uint32_t> channel_index;
};

struct ChartAuthoringCrossReferenceIssue {
    std::string event_id;
    std::string detail;
};

struct ChartAuthoringInspectorOptions {
    std::size_t max_density_rows{8};
    // Cap for the long-duration cue table. The table includes any
    // routed cue (Note / Hold / Hazard) with a non-zero authored
    // duration, sorted descending by duration. Most authoring formats
    // use this for holds, but instantaneous notes/hazards with
    // non-zero placement.duration_ticks also surface here.
    std::size_t max_long_duration_rows{8};
    std::size_t max_cross_reference_issues{8};
};

struct ChartAuthoringSnapshot {
    content::ChartDocumentSummary summary{};
    std::unordered_map<std::int64_t, std::uint32_t> interactive_cues_per_bar;
    std::unordered_map<std::uint32_t, std::uint32_t> channel_histogram;
    std::unordered_map<std::uint32_t, std::uint32_t> lane_histogram;
    std::vector<ChartAuthoringDensityRow> top_density_rows;
    std::vector<ChartAuthoringLongDurationRow> top_long_duration_rows;
    std::vector<ChartAuthoringCrossReferenceIssue> cross_reference_issues;
};

[[nodiscard]] ChartAuthoringSnapshot build_chart_authoring_snapshot(
    const content::ChartDocument& document,
    ChartAuthoringInspectorOptions options = {});

[[nodiscard]] InspectorPanel build_chart_authoring_inspector(
    const ChartAuthoringSnapshot& snapshot);

} // namespace reaktio::tools
