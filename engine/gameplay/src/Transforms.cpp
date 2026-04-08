#include "reaktio/gameplay/Transforms.hpp"

#include <cmath>
#include <vector>

namespace reaktio::gameplay {

namespace {

[[nodiscard]] Vector2 add(Vector2 lhs, Vector2 rhs) noexcept {
    return Vector2{.x = lhs.x + rhs.x, .y = lhs.y + rhs.y};
}

[[nodiscard]] Vector2 component_mul(Vector2 lhs, Vector2 rhs) noexcept {
    return Vector2{.x = lhs.x * rhs.x, .y = lhs.y * rhs.y};
}

[[nodiscard]] Vector2 rotate(Vector2 value, float radians) noexcept {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return Vector2{
        .x = (value.x * cosine) - (value.y * sine),
        .y = (value.x * sine) + (value.y * cosine),
    };
}

[[nodiscard]] WorldTransform2D as_world(LocalTransform2D local) noexcept {
    return WorldTransform2D{
        .translation = local.translation,
        .rotation_radians = local.rotation_radians,
        .scale = local.scale,
    };
}

[[nodiscard]] WorldTransform2D compose(const WorldTransform2D& parent, const LocalTransform2D& local) noexcept {
    return WorldTransform2D{
        .translation = add(parent.translation, rotate(component_mul(local.translation, parent.scale), parent.rotation_radians)),
        .rotation_radians = parent.rotation_radians + local.rotation_radians,
        .scale = component_mul(parent.scale, local.scale),
    };
}

[[nodiscard]] Vector3 add(Vector3 lhs, Vector3 rhs) noexcept {
    return Vector3{.x = lhs.x + rhs.x, .y = lhs.y + rhs.y, .z = lhs.z + rhs.z};
}

[[nodiscard]] Vector3 component_mul(Vector3 lhs, Vector3 rhs) noexcept {
    return Vector3{.x = lhs.x * rhs.x, .y = lhs.y * rhs.y, .z = lhs.z * rhs.z};
}

[[nodiscard]] float dot(Vector3 lhs, Vector3 rhs) noexcept {
    return (lhs.x * rhs.x) + (lhs.y * rhs.y) + (lhs.z * rhs.z);
}

[[nodiscard]] Vector3 cross(Vector3 lhs, Vector3 rhs) noexcept {
    return Vector3{
        .x = (lhs.y * rhs.z) - (lhs.z * rhs.y),
        .y = (lhs.z * rhs.x) - (lhs.x * rhs.z),
        .z = (lhs.x * rhs.y) - (lhs.y * rhs.x),
    };
}

[[nodiscard]] float length(Vector3 value) noexcept {
    return std::sqrt(dot(value, value));
}

[[nodiscard]] Vector3 normalize(Vector3 value) noexcept {
    const float vector_length = length(value);
    if (vector_length <= 0.000001f) {
        return Vector3{.x = 0.0f, .y = 1.0f, .z = 0.0f};
    }

    return Vector3{.x = value.x / vector_length, .y = value.y / vector_length, .z = value.z / vector_length};
}

[[nodiscard]] Quaternion normalize(Quaternion value) noexcept {
    const float magnitude = std::sqrt(
        (value.x * value.x) + (value.y * value.y) + (value.z * value.z) + (value.w * value.w));
    if (magnitude <= 0.000001f) {
        return Quaternion{};
    }

    return Quaternion{
        .x = value.x / magnitude,
        .y = value.y / magnitude,
        .z = value.z / magnitude,
        .w = value.w / magnitude,
    };
}

[[nodiscard]] Quaternion multiply(Quaternion lhs, Quaternion rhs) noexcept {
    return normalize(Quaternion{
        .x = (lhs.w * rhs.x) + (lhs.x * rhs.w) + (lhs.y * rhs.z) - (lhs.z * rhs.y),
        .y = (lhs.w * rhs.y) - (lhs.x * rhs.z) + (lhs.y * rhs.w) + (lhs.z * rhs.x),
        .z = (lhs.w * rhs.z) + (lhs.x * rhs.y) - (lhs.y * rhs.x) + (lhs.z * rhs.w),
        .w = (lhs.w * rhs.w) - (lhs.x * rhs.x) - (lhs.y * rhs.y) - (lhs.z * rhs.z),
    });
}

[[nodiscard]] Vector3 rotate(Vector3 value, Quaternion rotation) noexcept {
    const Quaternion normalized = normalize(rotation);
    const Vector3 axis{.x = normalized.x, .y = normalized.y, .z = normalized.z};
    const float scalar = normalized.w;
    return add(
        add(
            Vector3{
                .x = 2.0f * dot(axis, value) * axis.x,
                .y = 2.0f * dot(axis, value) * axis.y,
                .z = 2.0f * dot(axis, value) * axis.z,
            },
            Vector3{
                .x = ((scalar * scalar) - dot(axis, axis)) * value.x,
                .y = ((scalar * scalar) - dot(axis, axis)) * value.y,
                .z = ((scalar * scalar) - dot(axis, axis)) * value.z,
            }),
        Vector3{
            .x = 2.0f * scalar * cross(axis, value).x,
            .y = 2.0f * scalar * cross(axis, value).y,
            .z = 2.0f * scalar * cross(axis, value).z,
        });
}

[[nodiscard]] WorldTransform3D as_world(LocalTransform3D local) noexcept {
    return WorldTransform3D{
        .translation = local.translation,
        .rotation = normalize(local.rotation),
        .scale = local.scale,
    };
}

[[nodiscard]] WorldTransform3D compose(const WorldTransform3D& parent, const LocalTransform3D& local) noexcept {
    return WorldTransform3D{
        .translation = add(parent.translation, rotate(component_mul(local.translation, parent.scale), parent.rotation)),
        .rotation = multiply(parent.rotation, local.rotation),
        .scale = component_mul(parent.scale, local.scale),
    };
}

template <typename WorldTransform, typename LocalTransform>
void clear_world_transforms(WorldModel& world, std::size_t& stale_count) {
    std::vector<WorldEntity> entities_to_remove;
    world.for_each<WorldTransform>([&](WorldEntity entity, const WorldTransform&) {
        if (!world.has<LocalTransform>(entity)) {
            ++stale_count;
        }
        entities_to_remove.push_back(entity);
    });

    for (const WorldEntity entity : entities_to_remove) {
        world.remove<WorldTransform>(entity);
    }
}

template <typename LocalTransform, typename WorldTransform, typename ComposeFunc>
void propagate_dimension(
    WorldModel& world,
    std::size_t& propagated_count,
    std::size_t& detached_count,
    std::size_t& cycle_break_count,
    ComposeFunc&& compose) {
    std::vector<WorldEntity> pending_entities;
    world.for_each<LocalTransform>([&](WorldEntity entity, const LocalTransform&) {
        pending_entities.push_back(entity);
    });

    while (!pending_entities.empty()) {
        bool made_progress = false;
        std::vector<WorldEntity> unresolved_entities;

        for (const WorldEntity entity : pending_entities) {
            const LocalTransform& local = world.get<LocalTransform>(entity);
            WorldTransform resolved = as_world(local);
            bool can_resolve = true;

            if (const auto* parent = world.try_get<TransformParent>(entity); parent != nullptr && parent->parent.valid()) {
                if (!world.contains(parent->parent)) {
                    ++detached_count;
                } else if (parent->parent == entity) {
                    ++cycle_break_count;
                } else if (const WorldTransform* parent_world = world.try_get<WorldTransform>(parent->parent)) {
                    resolved = compose(*parent_world, local);
                } else if (world.has<LocalTransform>(parent->parent)) {
                    can_resolve = false;
                } else {
                    ++detached_count;
                }
            }

            if (can_resolve) {
                world.get_or_emplace<WorldTransform>(entity) = resolved;
                ++propagated_count;
                made_progress = true;
            } else {
                unresolved_entities.push_back(entity);
            }
        }

        if (!made_progress) {
            for (const WorldEntity entity : unresolved_entities) {
                world.get_or_emplace<WorldTransform>(entity) = as_world(world.get<LocalTransform>(entity));
                ++cycle_break_count;
                ++propagated_count;
            }
            break;
        }

        pending_entities = std::move(unresolved_entities);
    }
}

} // namespace

Quaternion make_axis_angle_rotation(Vector3 axis, float angle_radians) noexcept {
    if (length(axis) <= 0.000001f) {
        return Quaternion{};
    }

    const Vector3 normalized_axis = normalize(axis);
    const float half_angle = angle_radians * 0.5f;
    const float sine = std::sin(half_angle);
    const float cosine = std::cos(half_angle);
    return normalize(Quaternion{
        .x = normalized_axis.x * sine,
        .y = normalized_axis.y * sine,
        .z = normalized_axis.z * sine,
        .w = cosine,
    });
}

TransformPropagationReport propagate_transforms(WorldModel& world) {
    TransformPropagationReport report{};

    clear_world_transforms<WorldTransform2D, LocalTransform2D>(world, report.stale_world_transforms_2d);
    clear_world_transforms<WorldTransform3D, LocalTransform3D>(world, report.stale_world_transforms_3d);

    propagate_dimension<LocalTransform2D, WorldTransform2D>(
        world,
        report.propagated_2d,
        report.detached_2d,
        report.cycle_breaks_2d,
        [](const WorldTransform2D& parent, const LocalTransform2D& local) { return compose(parent, local); });

    propagate_dimension<LocalTransform3D, WorldTransform3D>(
        world,
        report.propagated_3d,
        report.detached_3d,
        report.cycle_breaks_3d,
        [](const WorldTransform3D& parent, const LocalTransform3D& local) { return compose(parent, local); });

    return report;
}

} // namespace reaktio::gameplay