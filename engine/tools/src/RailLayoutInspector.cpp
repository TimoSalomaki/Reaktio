#include "reaktio/tools/RailLayoutInspector.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace reaktio::tools {

namespace {

[[nodiscard]] std::string format_double(double value, int precision = 3) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

} // namespace

InspectorPanel build_rail_path_inspector(
    const gameplay::RailPath& path,
    RailPathInspectorOptions options) {
    InspectorPanel panel{};
    panel.id = "rail-path";
    panel.title = "Rail Path";

    push_row(
        panel,
        "valid",
        path.valid() ? "1" : "0",
        path.valid() ? InspectorRowSeverity::Info : InspectorRowSeverity::Error);
    if (!path.valid()) {
        return panel;
    }

    const gameplay::RailPathStatistics& stats = path.statistics();
    push_row(panel, "control_points", std::to_string(stats.control_point_count));
    push_row(panel, "segments", std::to_string(stats.segment_count));
    push_row(panel, "total_length", format_double(stats.total_length));
    push_row(panel, "shortest_segment", format_double(stats.shortest_segment_length));
    push_row(panel, "longest_segment", format_double(stats.longest_segment_length));

    const double total_length = stats.total_length;
    const std::size_t sample_count = options.sample_count > 0 ? options.sample_count : 1;
    for (std::size_t i = 0; i < sample_count; ++i) {
        const double t = sample_count == 1
            ? 0.0
            : static_cast<double>(i) / static_cast<double>(sample_count - 1);
        const double arc = total_length * t;
        const gameplay::RailPathSample sample = path.sample_at_arc_length(arc);
        std::ostringstream line;
        line << "sample arc=" << format_double(sample.arc_length)
             << " segment=" << sample.segment_index
             << " alpha=" << format_double(sample.segment_alpha, 4)
             << " pos=(" << format_double(sample.position.x) << ","
             << format_double(sample.position.y) << ","
             << format_double(sample.position.z) << ")";
        panel.body_lines.push_back(line.str());
    }
    return panel;
}

RailLayoutSnapshot build_rail_layout_snapshot(
    const gameplay::RailObstacleField& field,
    const gameplay::RailPath& path,
    RailLayoutInspectorOptions options) {
    RailLayoutSnapshot snapshot{};
    snapshot.arc_bin_counts.assign(options.arc_bin_count, 0u);
    const std::span<const gameplay::RailObstacle> obstacles = field.obstacles();
    snapshot.obstacle_count = obstacles.size();
    if (obstacles.empty()) {
        return snapshot;
    }

    snapshot.min_lane = std::numeric_limits<std::int32_t>::max();
    snapshot.max_lane = std::numeric_limits<std::int32_t>::min();
    snapshot.min_arc_length = std::numeric_limits<double>::max();
    snapshot.max_arc_length = std::numeric_limits<double>::lowest();
    for (const gameplay::RailObstacle& obstacle : obstacles) {
        snapshot.min_arc_length = std::min(snapshot.min_arc_length, obstacle.arc_length);
        snapshot.max_arc_length = std::max(snapshot.max_arc_length, obstacle.arc_length);
        snapshot.min_lane = std::min(snapshot.min_lane, obstacle.signed_lane_min);
        snapshot.max_lane = std::max(snapshot.max_lane, obstacle.signed_lane_max);
        for (std::int32_t lane = obstacle.signed_lane_min; lane <= obstacle.signed_lane_max; ++lane) {
            snapshot.lane_histogram[lane] = snapshot.lane_histogram[lane] + 1u;
        }
    }

    // Bin obstacles by arc length. Use the rail path's total_length as
    // the bin range when a path is available so authors see distribution
    // relative to the actual rail; otherwise fall back to the observed
    // arc range.
    double bin_min = snapshot.min_arc_length;
    double bin_max = snapshot.max_arc_length;
    if (path.valid()) {
        bin_min = 0.0;
        bin_max = path.total_length();
    }
    const double range = std::max(0.0001, bin_max - bin_min);
    const std::size_t bin_count = options.arc_bin_count > 0 ? options.arc_bin_count : 1;
    for (const gameplay::RailObstacle& obstacle : obstacles) {
        const double normalized = (obstacle.arc_length - bin_min) / range;
        const std::size_t index = std::clamp<std::size_t>(
            static_cast<std::size_t>(normalized * static_cast<double>(bin_count)),
            0u, bin_count - 1u);
        snapshot.arc_bin_counts[index]++;
    }

    // Pairwise overlap detection. O(n^2) -- intentional, this is an
    // authoring tool that runs offline / on chart load, and obstacle
    // counts in a single rail chart are typically <= a few thousand.
    for (std::size_t i = 0; i < obstacles.size(); ++i) {
        for (std::size_t j = i + 1; j < obstacles.size(); ++j) {
            const gameplay::RailObstacle& a = obstacles[i];
            const gameplay::RailObstacle& b = obstacles[j];
            const double a_min = a.arc_length - a.arc_length_half_extent;
            const double a_max = a.arc_length + a.arc_length_half_extent;
            const double b_min = b.arc_length - b.arc_length_half_extent;
            const double b_max = b.arc_length + b.arc_length_half_extent;
            const double arc_overlap_min = std::max(a_min, b_min);
            const double arc_overlap_max = std::min(a_max, b_max);
            if (arc_overlap_max <= arc_overlap_min) {
                continue;
            }
            const std::int32_t lane_overlap_min = std::max(a.signed_lane_min, b.signed_lane_min);
            const std::int32_t lane_overlap_max = std::min(a.signed_lane_max, b.signed_lane_max);
            if (lane_overlap_max < lane_overlap_min) {
                continue;
            }
            if (snapshot.overlap_entries.size() >= options.max_overlap_entries) {
                break;
            }
            RailLayoutOverlapEntry entry{};
            entry.obstacle_a = a.obstacle_id;
            entry.obstacle_b = b.obstacle_id;
            entry.shared_lane_min = lane_overlap_min;
            entry.shared_lane_max = lane_overlap_max;
            entry.arc_overlap_length = arc_overlap_max - arc_overlap_min;
            snapshot.overlap_entries.push_back(entry);
        }
        if (snapshot.overlap_entries.size() >= options.max_overlap_entries) {
            break;
        }
    }
    return snapshot;
}

InspectorPanel build_rail_layout_inspector(const RailLayoutSnapshot& snapshot) {
    InspectorPanel panel{};
    panel.id = "rail-layout";
    panel.title = "Rail Layout";
    push_row(panel, "obstacles", std::to_string(snapshot.obstacle_count));
    if (snapshot.obstacle_count == 0) {
        return panel;
    }
    push_row(panel, "min_lane", std::to_string(snapshot.min_lane));
    push_row(panel, "max_lane", std::to_string(snapshot.max_lane));
    push_row(panel, "min_arc_length", format_double(snapshot.min_arc_length));
    push_row(panel, "max_arc_length", format_double(snapshot.max_arc_length));
    push_row(panel, "lanes_used", std::to_string(snapshot.lane_histogram.size()));
    push_row(
        panel,
        "overlap_entries",
        std::to_string(snapshot.overlap_entries.size()),
        snapshot.overlap_entries.empty() ? InspectorRowSeverity::Info : InspectorRowSeverity::Warning);

    {
        std::ostringstream line;
        line << "arc_bins=";
        for (std::size_t i = 0; i < snapshot.arc_bin_counts.size(); ++i) {
            if (i > 0) {
                line << ',';
            }
            line << snapshot.arc_bin_counts[i];
        }
        panel.body_lines.push_back(line.str());
    }
    for (const auto& [lane, count] : snapshot.lane_histogram) {
        std::ostringstream line;
        line << "lane=" << lane << " count=" << count;
        panel.body_lines.push_back(line.str());
    }
    for (const RailLayoutOverlapEntry& overlap : snapshot.overlap_entries) {
        std::ostringstream line;
        line << "overlap a=" << overlap.obstacle_a
             << " b=" << overlap.obstacle_b
             << " lanes=[" << overlap.shared_lane_min << "," << overlap.shared_lane_max
             << "] arc=" << format_double(overlap.arc_overlap_length);
        panel.body_lines.push_back(line.str());
    }
    return panel;
}

} // namespace reaktio::tools
