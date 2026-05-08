#include "reaktio/tools/AssetBrowserModel.hpp"

#include <sstream>
#include <utility>

namespace reaktio::tools {

AssetBrowserSnapshot build_asset_browser_snapshot(const std::filesystem::path& manifest_path) {
    AssetBrowserSnapshot snapshot{};
    snapshot.manifest_path = manifest_path;

    std::string error_message;
    if (!content::load_cooked_chart_manifest(manifest_path, snapshot.records, error_message)) {
        snapshot.loaded_from_manifest = false;
        return snapshot;
    }
    snapshot.loaded_from_manifest = true;

    for (const content::CookedChartManifestRecord& record : snapshot.records) {
        for (const content::CookedChartDependencyRecord& dep : record.dependencies) {
            AssetBrowserDependencyEdge edge{};
            edge.from_authoring_id = record.authoring_id;
            edge.dependency_path = dep.path;
            edge.dependency_hash = dep.hash;
            snapshot.dependency_edges.push_back(std::move(edge));
            const auto key = dep.path.native();
            snapshot.dependency_reference_counts[key] =
                snapshot.dependency_reference_counts[key] + 1u;
        }
    }
    return snapshot;
}

AssetBrowserSnapshot build_asset_browser_snapshot_default() {
    AssetBrowserSnapshot snapshot{};
    if (auto path = content::find_default_cooked_chart_manifest_path()) {
        snapshot = build_asset_browser_snapshot(*path);
    } else {
        snapshot.loaded_from_manifest = false;
    }
    return snapshot;
}

InspectorPanel build_asset_browser_inspector(const AssetBrowserSnapshot& snapshot) {
    InspectorPanel panel{};
    panel.id = "asset-browser";
    panel.title = "Asset Browser";

    push_row(panel, "manifest_path", snapshot.manifest_path.string());
    push_row(
        panel,
        "loaded",
        snapshot.loaded_from_manifest ? "1" : "0",
        snapshot.loaded_from_manifest ? InspectorRowSeverity::Info : InspectorRowSeverity::Error);
    push_row(panel, "chart_records", std::to_string(snapshot.records.size()));
    push_row(panel, "dependency_edges", std::to_string(snapshot.dependency_edges.size()));
    push_row(panel, "unique_dependencies",
        std::to_string(snapshot.dependency_reference_counts.size()));

    for (const content::CookedChartManifestRecord& record : snapshot.records) {
        std::ostringstream line;
        line << "chart id=" << record.authoring_id
             << " label=" << record.runtime_label
             << " payload=" << record.payload_path.filename().string()
             << " deps=" << record.dependencies.size();
        panel.body_lines.push_back(line.str());
    }
    // List unique dependencies with reference counts for quick triage.
    for (const auto& [path, count] : snapshot.dependency_reference_counts) {
        std::ostringstream line;
        line << "dep refs=" << count << " path="
             << std::filesystem::path(path).filename().string();
        panel.body_lines.push_back(line.str());
    }
    return panel;
}

} // namespace reaktio::tools
