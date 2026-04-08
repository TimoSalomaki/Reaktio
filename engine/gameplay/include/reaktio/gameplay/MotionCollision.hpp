#pragma once

#include "reaktio/gameplay/Transforms.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace reaktio::gameplay {

struct LinearVelocity2D {
    Vector2 units_per_second{};
};

struct AngularVelocity2D {
    float radians_per_second{};
};

struct LinearVelocity3D {
    Vector3 units_per_second{};
};

struct AngularVelocity3D {
    Vector3 axis{.x = 0.0f, .y = 1.0f, .z = 0.0f};
    float radians_per_second{};
};

struct CircleCollider2D {
    float radius{0.5f};
    Vector2 center_offset{};
};

struct AxisAlignedBoxCollider2D {
    Vector2 half_extents{1.0f, 1.0f};
    Vector2 center_offset{};
};

struct CollisionFilter2D {
    std::uint32_t layer_bits{1u};
    std::uint32_t collides_with_bits{0xffffffffu};
};

enum class CollisionShapePair2D {
    CircleCircle,
    BoxBox,
    CircleBox,
};

struct CollisionContact2D {
    WorldEntity first{};
    WorldEntity second{};
    CollisionShapePair2D shape_pair{CollisionShapePair2D::CircleCircle};
    Vector2 normal{};
    float penetration{};
};

struct MotionIntegrationReport {
    std::size_t linear_2d{};
    std::size_t angular_2d{};
    std::size_t linear_3d{};
    std::size_t angular_3d{};
};

struct CollisionDetectionReport {
    std::size_t circle_circle_contacts{};
    std::size_t box_box_contacts{};
    std::size_t circle_box_contacts{};
    std::size_t skipped_missing_transforms{};
    std::vector<CollisionContact2D> contacts;
};

[[nodiscard]] constexpr std::string_view to_string(CollisionShapePair2D shape_pair) noexcept {
    switch (shape_pair) {
    case CollisionShapePair2D::CircleCircle:
        return "circle-circle";
    case CollisionShapePair2D::BoxBox:
        return "box-box";
    case CollisionShapePair2D::CircleBox:
        return "circle-box";
    }

    return "unknown";
}

[[nodiscard]] MotionIntegrationReport integrate_motion(WorldModel& world, double fixed_delta_seconds);
[[nodiscard]] CollisionDetectionReport detect_collisions_2d(const WorldModel& world);

} // namespace reaktio::gameplay