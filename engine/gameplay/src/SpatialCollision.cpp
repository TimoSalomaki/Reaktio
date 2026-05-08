#include "reaktio/gameplay/SpatialCollision.hpp"

#include <algorithm>
#include <cmath>

namespace reaktio::gameplay {

namespace {

[[nodiscard]] inline Vector3 sub(const Vector3& a, const Vector3& b) noexcept {
    return Vector3{a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] inline Vector3 add(const Vector3& a, const Vector3& b) noexcept {
    return Vector3{a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] inline Vector3 scale(const Vector3& a, float s) noexcept {
    return Vector3{a.x * s, a.y * s, a.z * s};
}

[[nodiscard]] inline float dot(const Vector3& a, const Vector3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] inline float length_squared(const Vector3& a) noexcept { return dot(a, a); }

[[nodiscard]] inline float clamp_f(float v, float lo, float hi) noexcept {
    return std::min(std::max(v, lo), hi);
}

[[nodiscard]] inline Vector3 obb_to_world(
    const OrientedBoxVolume& box, const Vector3& local) noexcept {
    return add(
        box.center,
        add(
            add(scale(box.axis_right, local.x), scale(box.axis_up, local.y)),
            scale(box.axis_forward, local.z)));
}

[[nodiscard]] inline Vector3 world_to_obb(
    const OrientedBoxVolume& box, const Vector3& world) noexcept {
    const Vector3 d = sub(world, box.center);
    return Vector3{dot(d, box.axis_right), dot(d, box.axis_up), dot(d, box.axis_forward)};
}

[[nodiscard]] float closest_point_on_segment_t(
    const Vector3& point, const Vector3& seg_a, const Vector3& seg_b) noexcept {
    const Vector3 ab = sub(seg_b, seg_a);
    const float ab_len_sq = length_squared(ab);
    if (ab_len_sq <= 1e-12f) {
        return 0.0f;
    }
    const float t = dot(sub(point, seg_a), ab) / ab_len_sq;
    return clamp_f(t, 0.0f, 1.0f);
}

} // namespace

std::string_view to_string(SpatialTriggerKind kind) noexcept {
    switch (kind) {
    case SpatialTriggerKind::Generic:
        return "generic";
    case SpatialTriggerKind::Hazard:
        return "hazard";
    case SpatialTriggerKind::Pickup:
        return "pickup";
    case SpatialTriggerKind::Goal:
        return "goal";
    case SpatialTriggerKind::Camera:
        return "camera";
    case SpatialTriggerKind::Music:
        return "music";
    }
    return "unknown";
}

Vector3 closest_point_on_segment(
    const Vector3& point, const Vector3& seg_a, const Vector3& seg_b) noexcept {
    const float t = closest_point_on_segment_t(point, seg_a, seg_b);
    return add(seg_a, scale(sub(seg_b, seg_a), t));
}

Vector3 closest_point_in_aabb(
    const Vector3& point, const AxisAlignedBoxVolume& box) noexcept {
    return Vector3{
        clamp_f(point.x, box.center.x - box.half_extents.x, box.center.x + box.half_extents.x),
        clamp_f(point.y, box.center.y - box.half_extents.y, box.center.y + box.half_extents.y),
        clamp_f(point.z, box.center.z - box.half_extents.z, box.center.z + box.half_extents.z),
    };
}

Vector3 closest_point_in_obb(
    const Vector3& point, const OrientedBoxVolume& box) noexcept {
    const Vector3 local = world_to_obb(box, point);
    const Vector3 clamped{
        clamp_f(local.x, -box.half_extents.x, box.half_extents.x),
        clamp_f(local.y, -box.half_extents.y, box.half_extents.y),
        clamp_f(local.z, -box.half_extents.z, box.half_extents.z),
    };
    return obb_to_world(box, clamped);
}

bool sphere_overlaps_sphere(const SphereVolume& a, const SphereVolume& b) noexcept {
    const float r = a.radius + b.radius;
    return length_squared(sub(a.center, b.center)) <= r * r;
}

bool sphere_overlaps_aabb(const SphereVolume& s, const AxisAlignedBoxVolume& b) noexcept {
    const Vector3 closest = closest_point_in_aabb(s.center, b);
    return length_squared(sub(s.center, closest)) <= s.radius * s.radius;
}

bool sphere_overlaps_obb(const SphereVolume& s, const OrientedBoxVolume& b) noexcept {
    const Vector3 closest = closest_point_in_obb(s.center, b);
    return length_squared(sub(s.center, closest)) <= s.radius * s.radius;
}

bool capsule_overlaps_sphere(const CapsuleVolume& c, const SphereVolume& s) noexcept {
    const Vector3 closest = closest_point_on_segment(s.center, c.point_a, c.point_b);
    const float r = c.radius + s.radius;
    return length_squared(sub(s.center, closest)) <= r * r;
}

bool capsule_overlaps_aabb(const CapsuleVolume& c, const AxisAlignedBoxVolume& b) noexcept {
    // Approximate: walk both endpoints + segment midpoint and test each as
    // a sphere against the AABB. Sufficient for the obstacle densities we
    // target; modes that need exact capsule-vs-AABB can layer a heavier
    // primitive themselves.
    const SphereVolume swept{c.point_a, c.radius};
    if (sphere_overlaps_aabb(swept, b)) {
        return true;
    }
    const SphereVolume tail{c.point_b, c.radius};
    if (sphere_overlaps_aabb(tail, b)) {
        return true;
    }
    const Vector3 mid = scale(add(c.point_a, c.point_b), 0.5f);
    return sphere_overlaps_aabb(SphereVolume{mid, c.radius}, b);
}

bool capsule_overlaps_obb(const CapsuleVolume& c, const OrientedBoxVolume& b) noexcept {
    const SphereVolume swept{c.point_a, c.radius};
    if (sphere_overlaps_obb(swept, b)) {
        return true;
    }
    const SphereVolume tail{c.point_b, c.radius};
    if (sphere_overlaps_obb(tail, b)) {
        return true;
    }
    const Vector3 mid = scale(add(c.point_a, c.point_b), 0.5f);
    return sphere_overlaps_obb(SphereVolume{mid, c.radius}, b);
}

void query_trigger_zones_against_sphere(
    const SphereVolume& player,
    std::span<const OrientedTriggerZone> zones,
    std::vector<std::size_t>& out_indices) {
    for (std::size_t i = 0; i < zones.size(); ++i) {
        if (sphere_overlaps_obb(player, zones[i].volume)) {
            out_indices.push_back(i);
        }
    }
}

void query_trigger_zones_against_capsule(
    const CapsuleVolume& player,
    std::span<const OrientedTriggerZone> zones,
    std::vector<std::size_t>& out_indices) {
    for (std::size_t i = 0; i < zones.size(); ++i) {
        if (capsule_overlaps_obb(player, zones[i].volume)) {
            out_indices.push_back(i);
        }
    }
}

} // namespace reaktio::gameplay
