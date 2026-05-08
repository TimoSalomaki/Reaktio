#pragma once

#include "reaktio/content/ChartPreview.hpp"
#include "reaktio/tools/InspectorPanel.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace reaktio::tools {

// Asset browser model. Builds a UI-agnostic snapshot of a cooked-chart
// manifest with dependency tracing so authoring tools and the eventual
// in-engine ImGui asset panel can browse cooked content without
// touching the file system on the UI thread.
//
// The browser intentionally focuses on charts + their dependency
// graph. Texture / shader / mesh / font cookers each produce their own
// manifests; once those grow richer cross-referencing data we extend
// this model. For Phase 11 the goal is "dependency tracing" over
// chart bundles, which is what the current cooked-chart manifest
// expresses.

struct AssetBrowserDependencyEdge {
    std::string from_authoring_id;
    std::filesystem::path dependency_path;
    std::string dependency_hash;
};

struct AssetBrowserSnapshot {
    std::filesystem::path manifest_path;
    bool loaded_from_manifest{};
    std::vector<content::CookedChartManifestRecord> records;
    std::vector<AssetBrowserDependencyEdge> dependency_edges;
    // path -> count of charts that reference it. Lets the UI flag
    // missing or orphaned dependencies in O(1).
    std::unordered_map<std::filesystem::path::string_type, std::uint32_t> dependency_reference_counts;
};

// Build a snapshot from an explicit manifest path. Returns
// loaded_from_manifest=false if the manifest cannot be loaded; the
// inspector then renders the failure as an Error severity row.
[[nodiscard]] AssetBrowserSnapshot build_asset_browser_snapshot(
    const std::filesystem::path& manifest_path);

// Build a snapshot from the runtime's default manifest discovery rule.
[[nodiscard]] AssetBrowserSnapshot build_asset_browser_snapshot_default();

[[nodiscard]] InspectorPanel build_asset_browser_inspector(
    const AssetBrowserSnapshot& snapshot);

} // namespace reaktio::tools
