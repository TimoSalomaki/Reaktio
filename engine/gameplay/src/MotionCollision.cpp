#include "reaktio/gameplay/MotionCollision.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace reaktio::gameplay {

namespace {

struct ResolvedCircleCollider2D {
    WorldEntity entity{};
    Vector2 center{};
    float radius{};
    CollisionFilter2D filter{};
};

struct ResolvedBoxCollider2D {
    WorldEntity entity{};
    Vector2 center{};
    Vector2 half_extents{};
    CollisionFilter2D filter{};
};

[[nodiscard]] Vector2 add(Vector2 lhs, Vector2 rhs) noexcept {
    return Vector2{.x = lhs.x + rhs.x, .y = lhs.y + rhs.y};
}

[[nodiscard]] Vector2 sub(Vector2 lhs, Vector2 rhs) noexcept {
    return Vector2{.x = lhs.x - rhs.x, .y = lhs.y - rhs.y};
}

[[nodiscard]] Vector2 mul(Vector2 value, float scalar) noexcept {
    return Vector2{.x = value.x * scalar, .y = value.y * scalar};
}

[[nodiscard]] Vector3 add(Vector3 lhs, Vector3 rhs) noexcept {
    return Vector3{.x = lhs.x + rhs.x, .y = lhs.y + rhs.y, .z = lhs.z + rhs.z};
}

[[nodiscard]] Vector3 mul(Vector3 value, float scalar) noexcept {
    return Vector3{.x = value.x * scalar, .y = value.y * scalar, .z = value.z * scalar};
}

[[nodiscard]] Vector2 component_mul(Vector2 lhs, Vector2 rhs) noexcept {
    return Vector2{.x = lhs.x * rhs.x, .y = lhs.y * rhs.y};
}

[[nodiscard]] Vector2 component_abs(Vector2 value) noexcept {
    return Vector2{.x = std::abs(value.x), .y = std::abs(value.y)};
}

[[nodiscard]] float dot(Vector2 lhs, Vector2 rhs) noexcept {
    return (lhs.x * rhs.x) + (lhs.y * rhs.y);
}

[[nodiscard]] float length(Vector2 value) noexcept {
    return std::sqrt(dot(value, value));
}

[[nodiscard]] Vector2 rotate(Vector2 value, float radians) noexcept {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return Vector2{
        .x = (value.x * cosine) - (value.y * sine),
        .y = (value.x * sine) + (value.y * cosine),
    };
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

[[nodiscard]] WorldTransform2D as_world(const LocalTransform2D& local) noexcept {
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

[[nodiscard]] bool contains_entity(const std::vector<WorldEntity>& ancestry, WorldEntity entity) noexcept {
    return std::find(ancestry.begin(), ancestry.end(), entity) != ancestry.end();
}

[[nodiscard]] bool resolve_transform_2d_recursive(
    const WorldModel& world,
    WorldEntity entity,
    WorldTransform2D& transform,
    std::vector<WorldEntity>& ancestry) noexcept {
    if (const WorldTransform2D* world_transform = world.try_get<WorldTransform2D>(entity)) {
        transform = *world_transform;
        return true;
    }

    const LocalTransform2D* local_transform = world.try_get<LocalTransform2D>(entity);
    if (local_transform == nullptr) {
        return false;
    }

    if (const TransformParent* parent = world.try_get<TransformParent>(entity); parent != nullptr && parent->parent.valid()) {
        if (!world.contains(parent->parent) || contains_entity(ancestry, parent->parent)) {
            return false;
        }

        ancestry.push_back(entity);

        WorldTransform2D parent_transform{};
        const bool resolved_parent = resolve_transform_2d_recursive(world, parent->parent, parent_transform, ancestry);

        ancestry.pop_back();
        if (!resolved_parent) {
            return false;
        }

        transform = compose(parent_transform, *local_transform);
        return true;
    }

    transform = as_world(*local_transform);
    return true;
}

[[nodiscard]] bool resolve_transform_2d(
    const WorldModel& world,
    WorldEntity entity,
    WorldTransform2D& transform) noexcept {
    std::vector<WorldEntity> ancestry;
    return resolve_transform_2d_recursive(world, entity, transform, ancestry);
}

[[nodiscard]] CollisionFilter2D resolve_filter(const WorldModel& world, WorldEntity entity) noexcept {
    if (const CollisionFilter2D* filter = world.try_get<CollisionFilter2D>(entity)) {
        return *filter;
    }

    return CollisionFilter2D{};
}

[[nodiscard]] bool can_collide(CollisionFilter2D lhs, CollisionFilter2D rhs) noexcept {
    return ((lhs.collides_with_bits & rhs.layer_bits) != 0u) && ((rhs.collides_with_bits & lhs.layer_bits) != 0u);
}

[[nodiscard]] bool resolve_circle(
    const WorldModel& world,
    WorldEntity entity,
    const CircleCollider2D& collider,
    ResolvedCircleCollider2D& resolved,
    std::size_t& skipped_missing_transforms) noexcept {
    WorldTransform2D transform{};
    if (!resolve_transform_2d(world, entity, transform)) {
        ++skipped_missing_transforms;
        return false;
    }

    const Vector2 scaled_offset = component_mul(collider.center_offset, transform.scale);
    const Vector2 abs_scale = component_abs(transform.scale);
    resolved = ResolvedCircleCollider2D{
        .entity = entity,
        .center = add(transform.translation, rotate(scaled_offset, transform.rotation_radians)),
        .radius = collider.radius * std::max(abs_scale.x, abs_scale.y),
        .filter = resolve_filter(world, entity),
    };
    return true;
}

[[nodiscard]] bool resolve_box(
    const WorldModel& world,
    WorldEntity entity,
    const AxisAlignedBoxCollider2D& collider,
    ResolvedBoxCollider2D& resolved,
    std::size_t& skipped_missing_transforms) noexcept {
    WorldTransform2D transform{};
    if (!resolve_transform_2d(world, entity, transform)) {
        ++skipped_missing_transforms;
        return false;
    }

    const Vector2 scaled_offset = component_mul(collider.center_offset, transform.scale);
    const Vector2 scaled_extents = component_abs(component_mul(collider.half_extents, transform.scale));
    const float cosine = std::abs(std::cos(transform.rotation_radians));
    const float sine = std::abs(std::sin(transform.rotation_radians));
    resolved = ResolvedBoxCollider2D{
        .entity = entity,
        .center = add(transform.translation, rotate(scaled_offset, transform.rotation_radians)),
        .half_extents = {
            .x = (scaled_extents.x * cosine) + (scaled_extents.y * sine),
            .y = (scaled_extents.x * sine) + (scaled_extents.y * cosine),
        },
        .filter = resolve_filter(world, entity),
    };
    return true;
}

template <typename Shape>
void sort_by_entity(std::vector<Shape>& shapes) {
    std::sort(shapes.begin(), shapes.end(), [](const Shape& lhs, const Shape& rhs) {
        return lhs.entity.value() < rhs.entity.value();
    });
}

void append_contact(CollisionDetectionReport& report, CollisionContact2D contact) {
    switch (contact.shape_pair) {
    case CollisionShapePair2D::CircleCircle:
        ++report.circle_circle_contacts;
        break;
    case CollisionShapePair2D::BoxBox:
        ++report.box_box_contacts;
        break;
    case CollisionShapePair2D::CircleBox:
        ++report.circle_box_contacts;
        break;
    }

    report.contacts.push_back(contact);
}

} // namespace

MotionIntegrationReport integrate_motion(WorldModel& world, double fixed_delta_seconds) {
    MotionIntegrationReport report{};
    const float delta_seconds = static_cast<float>(fixed_delta_seconds);

    world.for_each<LocalTransform2D, LinearVelocity2D>(
        [&](WorldEntity, LocalTransform2D& transform, const LinearVelocity2D& velocity) {
            transform.translation.x += velocity.units_per_second.x * delta_seconds;
            transform.translation.y += velocity.units_per_second.y * delta_seconds;
            ++report.linear_2d;
        });

    world.for_each<LocalTransform2D, AngularVelocity2D>(
        [&](WorldEntity, LocalTransform2D& transform, const AngularVelocity2D& velocity) {
            transform.rotation_radians += velocity.radians_per_second * delta_seconds;
            ++report.angular_2d;
        });

    world.for_each<LocalTransform3D, LinearVelocity3D>(
        [&](WorldEntity, LocalTransform3D& transform, const LinearVelocity3D& velocity) {
            transform.translation = add(transform.translation, mul(velocity.units_per_second, delta_seconds));
            ++report.linear_3d;
        });

    world.for_each<LocalTransform3D, AngularVelocity3D>(
        [&](WorldEntity, LocalTransform3D& transform, const AngularVelocity3D& velocity) {
            const Quaternion delta_rotation =
                make_axis_angle_rotation(velocity.axis, velocity.radians_per_second * delta_seconds);
            transform.rotation = multiply(transform.rotation, delta_rotation);
            ++report.angular_3d;
        });

    return report;
}

CollisionDetectionReport detect_collisions_2d(const WorldModel& world) {
    CollisionDetectionReport report{};
    std::vector<ResolvedCircleCollider2D> circles;
    std::vector<ResolvedBoxCollider2D> boxes;

    world.for_each<CircleCollider2D>([&](WorldEntity entity, const CircleCollider2D& collider) {
        ResolvedCircleCollider2D resolved{};
        if (resolve_circle(world, entity, collider, resolved, report.skipped_missing_transforms)) {
            circles.push_back(resolved);
        }
    });

    world.for_each<AxisAlignedBoxCollider2D>([&](WorldEntity entity, const AxisAlignedBoxCollider2D& collider) {
        ResolvedBoxCollider2D resolved{};
        if (resolve_box(world, entity, collider, resolved, report.skipped_missing_transforms)) {
            boxes.push_back(resolved);
        }
    });

    sort_by_entity(circles);
    sort_by_entity(boxes);

    for (std::size_t index = 0; index < circles.size(); ++index) {
        for (std::size_t other = index + 1; other < circles.size(); ++other) {
            const ResolvedCircleCollider2D& lhs = circles[index];
            const ResolvedCircleCollider2D& rhs = circles[other];
            if (!can_collide(lhs.filter, rhs.filter)) {
                continue;
            }

            const Vector2 delta = sub(rhs.center, lhs.center);
            const float distance = length(delta);
            const float combined_radius = lhs.radius + rhs.radius;
            if (distance > combined_radius) {
                continue;
            }

            const Vector2 normal = distance > 0.000001f ? mul(delta, 1.0f / distance) : Vector2{.x = 1.0f, .y = 0.0f};
            append_contact(
                report,
                CollisionContact2D{
                    .first = lhs.entity,
                    .second = rhs.entity,
                    .shape_pair = CollisionShapePair2D::CircleCircle,
                    .normal = normal,
                    .penetration = combined_radius - distance,
                });
        }
    }

    for (std::size_t index = 0; index < boxes.size(); ++index) {
        for (std::size_t other = index + 1; other < boxes.size(); ++other) {
            const ResolvedBoxCollider2D& lhs = boxes[index];
            const ResolvedBoxCollider2D& rhs = boxes[other];
            if (!can_collide(lhs.filter, rhs.filter)) {
                continue;
            }

            const Vector2 delta = sub(rhs.center, lhs.center);
            const float overlap_x = (lhs.half_extents.x + rhs.half_extents.x) - std::abs(delta.x);
            const float overlap_y = (lhs.half_extents.y + rhs.half_extents.y) - std::abs(delta.y);
            if (overlap_x <= 0.0f || overlap_y <= 0.0f) {
                continue;
            }

            const bool resolve_x = overlap_x <= overlap_y;
            append_contact(
                report,
                CollisionContact2D{
                    .first = lhs.entity,
                    .second = rhs.entity,
                    .shape_pair = CollisionShapePair2D::BoxBox,
                    .normal = resolve_x
                        ? Vector2{.x = delta.x >= 0.0f ? 1.0f : -1.0f, .y = 0.0f}
                        : Vector2{.x = 0.0f, .y = delta.y >= 0.0f ? 1.0f : -1.0f},
                    .penetration = resolve_x ? overlap_x : overlap_y,
                });
        }
    }

    for (const ResolvedCircleCollider2D& circle : circles) {
        for (const ResolvedBoxCollider2D& box : boxes) {
            if (!can_collide(circle.filter, box.filter)) {
                continue;
            }

            const Vector2 min_corner{
                .x = box.center.x - box.half_extents.x,
                .y = box.center.y - box.half_extents.y,
            };
            const Vector2 max_corner{
                .x = box.center.x + box.half_extents.x,
                .y = box.center.y + box.half_extents.y,
            };
            const Vector2 closest_point{
                .x = std::clamp(circle.center.x, min_corner.x, max_corner.x),
                .y = std::clamp(circle.center.y, min_corner.y, max_corner.y),
            };
            const Vector2 delta = sub(circle.center, closest_point);
            const float distance = length(delta);
            if (distance > circle.radius) {
                continue;
            }

            Vector2 normal{};
            float penetration = circle.radius - distance;
            if (distance > 0.000001f) {
                normal = mul(delta, 1.0f / distance);
            } else {
                const Vector2 box_delta = sub(circle.center, box.center);
                const float remaining_x = box.half_extents.x - std::abs(box_delta.x);
                const float remaining_y = box.half_extents.y - std::abs(box_delta.y);
                if (remaining_x <= remaining_y) {
                    normal = Vector2{.x = box_delta.x >= 0.0f ? 1.0f : -1.0f, .y = 0.0f};
                    penetration = circle.radius + remaining_x;
                } else {
                    normal = Vector2{.x = 0.0f, .y = box_delta.y >= 0.0f ? 1.0f : -1.0f};
                    penetration = circle.radius + remaining_y;
                }
            }

            append_contact(
                report,
                CollisionContact2D{
                    .first = circle.entity,
                    .second = box.entity,
                    .shape_pair = CollisionShapePair2D::CircleBox,
                    .normal = normal,
                    .penetration = penetration,
                });
        }
    }

    std::sort(report.contacts.begin(), report.contacts.end(), [](const CollisionContact2D& lhs, const CollisionContact2D& rhs) {
        if (lhs.first != rhs.first) {
            return lhs.first < rhs.first;
        }

        if (lhs.second != rhs.second) {
            return lhs.second < rhs.second;
        }

        return static_cast<std::uint32_t>(lhs.shape_pair) < static_cast<std::uint32_t>(rhs.shape_pair);
    });

    return report;
}

} // namespace reaktio::gameplay