#pragma once

#include "reaktio/gameplay/WorldModel.hpp"

#include <cstddef>

namespace reaktio::gameplay {

struct Vector2 {
    float x{};
    float y{};
};

struct Vector3 {
    float x{};
    float y{};
    float z{};
};

struct Quaternion {
    float x{};
    float y{};
    float z{};
    float w{1.0f};
};

struct TransformParent {
    WorldEntity parent{};
};

struct LocalTransform2D {
    Vector2 translation{};
    float rotation_radians{};
    Vector2 scale{1.0f, 1.0f};
};

struct WorldTransform2D {
    Vector2 translation{};
    float rotation_radians{};
    Vector2 scale{1.0f, 1.0f};
};

struct LocalTransform3D {
    Vector3 translation{};
    Quaternion rotation{};
    Vector3 scale{1.0f, 1.0f, 1.0f};
};

struct WorldTransform3D {
    Vector3 translation{};
    Quaternion rotation{};
    Vector3 scale{1.0f, 1.0f, 1.0f};
};

struct TransformPropagationReport {
    std::size_t propagated_2d{};
    std::size_t propagated_3d{};
    std::size_t detached_2d{};
    std::size_t detached_3d{};
    std::size_t stale_world_transforms_2d{};
    std::size_t stale_world_transforms_3d{};
    std::size_t cycle_breaks_2d{};
    std::size_t cycle_breaks_3d{};
};

[[nodiscard]] Quaternion make_axis_angle_rotation(Vector3 axis, float angle_radians) noexcept;
[[nodiscard]] TransformPropagationReport propagate_transforms(WorldModel& world);

} // namespace reaktio::gameplay