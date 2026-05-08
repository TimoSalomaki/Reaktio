#pragma once

#include "reaktio/gameplay/RailObstacles.hpp"
#include "reaktio/gameplay/RailPath.hpp"
#include "reaktio/tools/InspectorPanel.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace reaktio::tools {

// Authoring helpers for rail / lane modes. Two read-only inspectors:
//
//   - RailPathInspector reports per-segment lengths, the longest /
//     shortest segments, and a sampled coverage table so authors can
//     see hot-spots in their path before running a chart.
//
//   - RailLayoutInspector reports the obstacle distribution along the
//     rail: arc-length histogram (binned), lane occupancy, and a
//     pairwise overlap report so authors can spot accidentally
//     stacked or unreachable obstacles.

struct RailPathInspectorOptions {
    std::size_t sample_count{8};      // Even-spaced arc-length samples to report.
    std::size_t max_segment_rows{8};  // Detailed segment rows.
};

[[nodiscard]] InspectorPanel build_rail_path_inspector(
    const gameplay::RailPath& path,
    RailPathInspectorOptions options = {});

struct RailLayoutOverlapEntry {
    std::uint64_t obstacle_a{};
    std::uint64_t obstacle_b{};
    std::int32_t shared_lane_min{};
    std::int32_t shared_lane_max{};
    double arc_overlap_length{};
};

struct RailLayoutInspectorOptions {
    std::size_t arc_bin_count{8};
    std::size_t max_overlap_entries{8};
};

struct RailLayoutSnapshot {
    std::size_t obstacle_count{};
    std::int32_t min_lane{};
    std::int32_t max_lane{};
    double min_arc_length{};
    double max_arc_length{};
    std::vector<std::uint32_t> arc_bin_counts;     // Size == options.arc_bin_count.
    std::unordered_map<std::int32_t, std::uint32_t> lane_histogram;
    std::vector<RailLayoutOverlapEntry> overlap_entries;
};

[[nodiscard]] RailLayoutSnapshot build_rail_layout_snapshot(
    const gameplay::RailObstacleField& field,
    const gameplay::RailPath& path,
    RailLayoutInspectorOptions options = {});

[[nodiscard]] InspectorPanel build_rail_layout_inspector(
    const RailLayoutSnapshot& snapshot);

} // namespace reaktio::tools
