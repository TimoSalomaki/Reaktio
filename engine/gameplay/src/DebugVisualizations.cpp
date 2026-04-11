#include "reaktio/gameplay/DebugVisualizations.hpp"

#include <algorithm>
#include <cmath>

namespace reaktio::gameplay {

namespace {

[[nodiscard]] Vector2 add(Vector2 lhs, Vector2 rhs) noexcept {
    return Vector2{.x = lhs.x + rhs.x, .y = lhs.y + rhs.y};
}

[[nodiscard]] Vector2 mul(Vector2 value, float scalar) noexcept {
    return Vector2{.x = value.x * scalar, .y = value.y * scalar};
}

[[nodiscard]] Vector2 component_mul(Vector2 lhs, Vector2 rhs) noexcept {
    return Vector2{.x = lhs.x * rhs.x, .y = lhs.y * rhs.y};
}

[[nodiscard]] Vector2 component_abs(Vector2 value) noexcept {
    return Vector2{.x = std::abs(value.x), .y = std::abs(value.y)};
}

[[nodiscard]] Vector2 rotate(Vector2 value, float radians) noexcept {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return Vector2{
        .x = (value.x * cosine) - (value.y * sine),
        .y = (value.x * sine) + (value.y * cosine),
    };
}

bool try_resolve_collision_anchor(const WorldModel& world, WorldEntity entity, Vector2& anchor) noexcept {
    WorldTransform2D transform{};
    if (!try_resolve_world_transform_2d(world, entity, transform)) {
        return false;
    }

    if (const CircleCollider2D* circle = world.try_get<CircleCollider2D>(entity)) {
        const Vector2 scaled_offset = component_mul(circle->center_offset, transform.scale);
        anchor = add(transform.translation, rotate(scaled_offset, transform.rotation_radians));
        return true;
    }

    if (const AxisAlignedBoxCollider2D* box = world.try_get<AxisAlignedBoxCollider2D>(entity)) {
        const Vector2 scaled_offset = component_mul(box->center_offset, transform.scale);
        anchor = add(transform.translation, rotate(scaled_offset, transform.rotation_radians));
        return true;
    }

    anchor = transform.translation;
    return true;
}

} // namespace

void emit_collision_debug_visualizations(
    render::RenderExtractionContext& render_extraction,
    const WorldModel& world,
    const CollisionDetectionReport* collision_report,
    const CollisionDebugVisualizationStyle& style) {
    world.for_each<CircleCollider2D>(
        [&](WorldEntity entity, const CircleCollider2D& collider) {
            WorldTransform2D transform{};
            if (!try_resolve_world_transform_2d(world, entity, transform)) {
                return;
            }

            const Vector2 scaled_offset = component_mul(collider.center_offset, transform.scale);
            const Vector2 center = add(transform.translation, rotate(scaled_offset, transform.rotation_radians));
            const Vector2 abs_scale = component_abs(transform.scale);
            render_extraction.add_debug_circle(render::DebugCircleCommand{
                .center = {center.x, center.y},
                .radius = collider.radius * std::max(abs_scale.x, abs_scale.y),
                .rgba = style.circle_rgba,
                .segments = 16,
            });
        });

    world.for_each<AxisAlignedBoxCollider2D>(
        [&](WorldEntity entity, const AxisAlignedBoxCollider2D& collider) {
            WorldTransform2D transform{};
            if (!try_resolve_world_transform_2d(world, entity, transform)) {
                return;
            }

            const Vector2 scaled_offset = component_mul(collider.center_offset, transform.scale);
            const Vector2 center = add(transform.translation, rotate(scaled_offset, transform.rotation_radians));
            const Vector2 scaled_extents = component_abs(component_mul(collider.half_extents, transform.scale));
            const float cosine = std::abs(std::cos(transform.rotation_radians));
            const float sine = std::abs(std::sin(transform.rotation_radians));
            render_extraction.add_debug_rect(render::DebugRectCommand{
                .position = {center.x, center.y},
                .half_extents = {
                    (scaled_extents.x * cosine) + (scaled_extents.y * sine),
                    (scaled_extents.x * sine) + (scaled_extents.y * cosine),
                },
                .rgba = style.box_rgba,
            });
        });

    if (collision_report == nullptr) {
        return;
    }

    for (const CollisionContact2D& contact : collision_report->contacts) {
        Vector2 first_position{};
        Vector2 second_position{};
        if (!try_resolve_collision_anchor(world, contact.first, first_position) ||
            !try_resolve_collision_anchor(world, contact.second, second_position)) {
            continue;
        }

        const Vector2 midpoint = mul(add(first_position, second_position), 0.5f);
        const Vector2 normal_tip = add(
            midpoint,
            mul(contact.normal, style.contact_normal_length + (contact.penetration * 8.0f)));
        render_extraction.add_debug_line(render::DebugLineCommand{
            .start = {midpoint.x, midpoint.y},
            .end = {normal_tip.x, normal_tip.y},
            .rgba = style.contact_rgba,
        });
    }
}

void emit_cue_lane_debug_visualizations(
    render::RenderExtractionContext& render_extraction,
    std::span<const CueLaneDebugVisualization> lanes) {
    for (const CueLaneDebugVisualization& lane : lanes) {
        render_extraction.add_debug_line(render::DebugLineCommand{
            .start = {lane.lane_start_x, lane.center_y},
            .end = {lane.lane_end_x, lane.center_y},
            .rgba = lane.lane_rgba,
        });

        render_extraction.add_debug_line(render::DebugLineCommand{
            .start = {lane.timing_line_x, lane.center_y - lane.timing_line_half_height},
            .end = {lane.timing_line_x, lane.center_y + lane.timing_line_half_height},
            .rgba = lane.timing_line_rgba,
        });

        render_extraction.add_debug_rect(render::DebugRectCommand{
            .position = {lane.spawn_window_center.x, lane.spawn_window_center.y},
            .half_extents = {lane.spawn_window_half_extents.x, lane.spawn_window_half_extents.y},
            .rgba = lane.spawn_window_rgba,
        });
    }
}

} // namespace reaktio::gameplay