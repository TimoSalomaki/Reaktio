#include "reaktio/gameplay/RailChart.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace reaktio::gameplay {

namespace {

[[nodiscard]] std::int32_t channel_to_signed_lane(
    std::uint32_t channel_index, std::int32_t lane_count) noexcept {
    if (lane_count <= 0) {
        return 0;
    }
    // Channel 0 maps to lane 0 (centerline). Even channels go right, odd go
    // left, so a 4-channel chart on a 5-lane rail produces a balanced layout
    // around the centerline (0, +1, -1, +2, -2). Lane count truncates the
    // outermost lanes deterministically.
    const std::int32_t signed_index =
        (channel_index % 2u == 0u)
            ? static_cast<std::int32_t>(channel_index / 2u)
            : -static_cast<std::int32_t>((channel_index + 1u) / 2u);
    const std::int32_t half_extent = (lane_count - 1) / 2;
    return std::clamp(signed_index, -half_extent, half_extent);
}

} // namespace

RailChart make_rail_chart(
    std::span<const rhythm::ScheduledCue> scheduled_cues,
    const RailChartConfig& config) {
    RailChart chart{};
    chart.config = config;
    if (chart.config.lane_spacing_override > 0.0) {
        chart.config.lane_layout.lane_spacing = chart.config.lane_spacing_override;
    }
    if (chart.config.lane_layout.lane_count <= 0) {
        chart.config.lane_layout.lane_count = 1;
    }
    chart.cues.reserve(scheduled_cues.size());

    for (std::size_t i = 0; i < scheduled_cues.size(); ++i) {
        const rhythm::ScheduledCue& cue = scheduled_cues[i];
        RailChartCue rail_cue{};
        rail_cue.schedule_index = i;
        rail_cue.hit_tick = cue.hit_tick;
        rail_cue.signed_lane_index =
            channel_to_signed_lane(cue.channel_index, chart.config.lane_layout.lane_count);
        // hit_arc_length placement is governed by config.cue_placement:
        //   ScrollingHighway: every cue lands at the same judge arc length;
        //     resolve_spatial_cues() projects the cue position over time
        //     using arc_length_per_tick * (hit_tick - current_tick).
        //   FixedWorld: cues are spread at static world positions
        //     hit_tick * arc_length_per_tick along the rail. Cue position
        //     does not change with time; the player is the moving frame.
        if (chart.config.cue_placement == RailChartConfig::CuePlacement::FixedWorld) {
            rail_cue.hit_arc_length =
                static_cast<double>(cue.hit_tick) * chart.config.arc_length_per_tick;
        } else {
            rail_cue.hit_arc_length = chart.config.judge_arc_length;
        }
        chart.cues.push_back(rail_cue);
    }
    return chart;
}

void resolve_spatial_cues(
    const RailChart& chart,
    const RailPath& path,
    rhythm::ChartTick current_tick,
    std::span<const ActiveCue> active_cues,
    std::vector<SpatialCueSample>& out_samples) {
    out_samples.clear();
    out_samples.reserve(active_cues.size());
    if (!path.valid()) {
        return;
    }

    for (const ActiveCue& active : active_cues) {
        if (active.schedule_index >= chart.cues.size()) {
            continue;
        }
        const RailChartCue& chart_cue = chart.cues[active.schedule_index];

        // ScrollingHighway: cues approach the judge line from ahead. At
        //   hit_tick the cue sits at hit_arc_length; before hit_tick the
        //   cue sits forward by (hit_tick - current_tick) * per_tick.
        // FixedWorld: cues sit at hit_arc_length permanently; their world
        //   position never changes with current_tick.
        double cue_arc{};
        if (chart.config.cue_placement == RailChartConfig::CuePlacement::FixedWorld) {
            cue_arc = chart_cue.hit_arc_length;
        } else {
            const double ticks_until_judge =
                static_cast<double>(chart_cue.hit_tick - current_tick);
            cue_arc =
                chart_cue.hit_arc_length + ticks_until_judge * chart.config.arc_length_per_tick;
        }
        if (chart.config.wrap_arc_length) {
            cue_arc = path.wrap_arc_length(cue_arc);
        }

        const RailPathSample path_sample = path.sample_at_arc_length(cue_arc);
        const Vector3 world_position = rail_lane_position(
            path_sample, chart_cue.signed_lane_index, chart.config.lane_layout);

        SpatialCueSample sample{};
        sample.schedule_index = chart_cue.schedule_index;
        sample.hit_tick = chart_cue.hit_tick;
        sample.signed_lane_index = chart_cue.signed_lane_index;
        sample.cue_arc_length = cue_arc;
        sample.distance_to_judge = cue_arc - chart_cue.hit_arc_length;
        sample.world_position = world_position;
        sample.past_judge = current_tick > chart_cue.hit_tick;
        out_samples.push_back(sample);
    }
}

} // namespace reaktio::gameplay
