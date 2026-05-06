#pragma once

#include "reaktio/gameplay/CueScheduler.hpp"
#include "reaktio/gameplay/RailPath.hpp"
#include "reaktio/rhythm/CueTravelModel.hpp"
#include "reaktio/rhythm/TempoMap.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace reaktio::gameplay {

// Adapter that maps rhythm-domain cues onto a RailPath. Lives in the gameplay
// layer so any lane/rail/runner mode can reuse it without re-implementing the
// "tick -> arc length -> world position" pipeline.
//
// Architectural rule: chart timing and presentation timing stay separate
// (Phase 0 architectural conclusion #5). The chart owns hit_tick + lane;
// the adapter owns arc-length-per-tick scrolling, lane geometry, and travel
// lead. Modes consume the SpatialCueSample stream without touching either
// directly.

struct RailChartConfig {
    RailLaneLayout lane_layout{};
    double arc_length_per_tick{0.005};   // World units travelled per chart tick.
    rhythm::ChartTick travel_lead_ticks{1920};
    rhythm::ChartTick despawn_lag_ticks{480};
    bool wrap_arc_length{false};
    double judge_arc_length{0.0};        // Arc length where cues are judged (player position).
    double lane_spacing_override{0.0};   // If > 0 overrides lane_layout.lane_spacing on apply.

    // Cue placement convention. Two modes are supported because rail-family
    // games split cleanly into two camps:
    //   ScrollingHighway (default): every cue's hit_arc_length collapses to
    //     judge_arc_length. The cue's world position over time is computed
    //     by resolve_spatial_cues() as judge_arc_length + (hit_tick -
    //     current_tick) * arc_length_per_tick, i.e. cues converge on a
    //     fixed judge line as music time advances. Matches 4-key rhythm
    //     games, falling-token typing modes, etc.
    //   FixedWorld: hit_arc_length is hit_tick * arc_length_per_tick, i.e.
    //     cues are placed at static world positions along the rail. The
    //     player passes through them as they advance. Matches runner /
    //     side-scroller / spatial obstacle modes where the world is fixed
    //     and the player is the moving frame of reference.
    enum class CuePlacement : std::uint8_t {
        ScrollingHighway = 0,
        FixedWorld = 1,
    };
    CuePlacement cue_placement{CuePlacement::ScrollingHighway};
};

struct RailChartCue {
    std::size_t schedule_index{};
    rhythm::ChartTick hit_tick{};
    std::int32_t signed_lane_index{};
    double hit_arc_length{};
};

struct RailChart {
    RailChartConfig config{};
    std::vector<RailChartCue> cues;
};

[[nodiscard]] RailChart make_rail_chart(
    std::span<const rhythm::ScheduledCue> scheduled_cues,
    const RailChartConfig& config);

// Resolve a cue's world-space position at the given simulation tick. Returns
// the cue position along the rail, displaced laterally by the cue's lane
// index. The arc length is (hit_arc_length) + (current_tick - hit_tick) *
// arc_length_per_tick * (-1 if cues approach from ahead) — the convention
// used here is that cues spawn ahead of the judge line and travel toward it
// as time advances. Modes that prefer the opposite convention can negate
// arc_length_per_tick in the config.
struct SpatialCueSample {
    std::size_t schedule_index{};
    rhythm::ChartTick hit_tick{};
    std::int32_t signed_lane_index{};
    double cue_arc_length{};
    double distance_to_judge{};
    Vector3 world_position{};
    bool past_judge{};
};

void resolve_spatial_cues(
    const RailChart& chart,
    const RailPath& path,
    rhythm::ChartTick current_tick,
    std::span<const ActiveCue> active_cues,
    std::vector<SpatialCueSample>& out_samples);

} // namespace reaktio::gameplay
