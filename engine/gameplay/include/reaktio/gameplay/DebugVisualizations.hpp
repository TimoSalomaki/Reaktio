#pragma once

#include "reaktio/gameplay/MotionCollision.hpp"
#include "reaktio/render/RenderExtraction.hpp"

#include <cstdint>
#include <span>

namespace reaktio::gameplay {

struct CollisionDebugVisualizationStyle {
    std::uint32_t circle_rgba{0xff8040ffu};
    std::uint32_t box_rgba{0x40ff40ffu};
    std::uint32_t contact_rgba{0xfff040ffu};
    float contact_normal_length{24.0f};
};

struct CueLaneDebugVisualization {
    float lane_start_x{};
    float lane_end_x{};
    float center_y{};
    std::uint32_t lane_rgba{0x5050a0ffu};
    float timing_line_x{};
    float timing_line_half_height{48.0f};
    std::uint32_t timing_line_rgba{0xffc040ffu};
    Vector2 spawn_window_center{};
    Vector2 spawn_window_half_extents{};
    std::uint32_t spawn_window_rgba{0x4080ffffu};
};

void emit_collision_debug_visualizations(
    render::RenderExtractionContext& render_extraction,
    const WorldModel& world,
    const CollisionDetectionReport* collision_report,
    const CollisionDebugVisualizationStyle& style = {});

void emit_cue_lane_debug_visualizations(
    render::RenderExtractionContext& render_extraction,
    std::span<const CueLaneDebugVisualization> lanes);

} // namespace reaktio::gameplay