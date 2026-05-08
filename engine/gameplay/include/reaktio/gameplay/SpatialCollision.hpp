#pragma once

#include "reaktio/gameplay/Transforms.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace reaktio::gameplay {

// Engine-layer 3D collision volumes and overlap helpers for spatial /
// obstacle-heavy game modes. Deliberately split from MotionCollision.hpp:
//
//   - MotionCollision.hpp drives flat 2D world-entity collision through the
//     WorldModel + EnTT registry. It is the right tool when entities live
//     in the registry and need broad-phase 2D contacts.
//   - SpatialCollision.hpp deals with 3D volumes that often live OUTSIDE
//     the registry (procedural tunnel geometry, ring hazards, transient
//     trigger zones). Volumes are plain data so modes can store them in
//     mode-private buffers without paying for entity allocation.
//
// All overlap tests are pure free functions, no hidden state, no
// allocations. Replay-safe: identical inputs always produce identical
// outputs in the same compilation. OBB-OBB is intentionally NOT included
// in v1 (SAT is non-trivial; we will add it only when a shipped mode
// proves it is necessary; modes wanting OBB-OBB approximation today can
// substitute a pair of capsules around the box's diagonals).

struct SphereVolume {
    Vector3 center{};
    float radius{0.5f};
};

struct AxisAlignedBoxVolume {
    Vector3 center{};
    Vector3 half_extents{0.5f, 0.5f, 0.5f};
};

// Right-handed orthonormal basis (right, up, forward). For replay-safe
// overlap tests modes are responsible for keeping the basis orthonormal;
// helpers below assume that property.
struct OrientedBoxVolume {
    Vector3 center{};
    Vector3 half_extents{0.5f, 0.5f, 0.5f};
    Vector3 axis_right{1.0f, 0.0f, 0.0f};
    Vector3 axis_up{0.0f, 1.0f, 0.0f};
    Vector3 axis_forward{0.0f, 0.0f, 1.0f};
};

// Capsule = swept sphere along the segment (point_a, point_b).
struct CapsuleVolume {
    Vector3 point_a{};
    Vector3 point_b{0.0f, 1.0f, 0.0f};
    float radius{0.5f};
};

// Trigger zones are tagged volumes. The tag is mode-defined so the
// engine layer doesn't enumerate a closed set of zone semantics.
enum class SpatialTriggerKind : std::uint8_t {
    Generic = 0,
    Hazard = 1,
    Pickup = 2,
    Goal = 3,
    Camera = 4,
    Music = 5,
};

struct OrientedTriggerZone {
    OrientedBoxVolume volume{};
    std::uint64_t trigger_id{};
    SpatialTriggerKind kind{SpatialTriggerKind::Generic};
    std::uint32_t tag_bits{};  // Mode-defined.
};

[[nodiscard]] std::string_view to_string(SpatialTriggerKind kind) noexcept;

// Closest-point helpers. Used by capsule tests but exported because some
// modes want them for AI / steering. All are O(1), no allocations.
[[nodiscard]] Vector3 closest_point_on_segment(
    const Vector3& point, const Vector3& seg_a, const Vector3& seg_b) noexcept;
[[nodiscard]] Vector3 closest_point_in_aabb(
    const Vector3& point, const AxisAlignedBoxVolume& box) noexcept;
[[nodiscard]] Vector3 closest_point_in_obb(
    const Vector3& point, const OrientedBoxVolume& box) noexcept;

// Overlap tests. Names are <A>_overlaps_<B> so calls read clearly.
[[nodiscard]] bool sphere_overlaps_sphere(const SphereVolume& a, const SphereVolume& b) noexcept;
[[nodiscard]] bool sphere_overlaps_aabb(const SphereVolume& s, const AxisAlignedBoxVolume& b) noexcept;
[[nodiscard]] bool sphere_overlaps_obb(const SphereVolume& s, const OrientedBoxVolume& b) noexcept;
[[nodiscard]] bool capsule_overlaps_sphere(const CapsuleVolume& c, const SphereVolume& s) noexcept;
[[nodiscard]] bool capsule_overlaps_aabb(const CapsuleVolume& c, const AxisAlignedBoxVolume& b) noexcept;
[[nodiscard]] bool capsule_overlaps_obb(const CapsuleVolume& c, const OrientedBoxVolume& b) noexcept;

// Trigger-zone aggregate query. Modes pass the player volume and the
// active trigger list; we append indices of triggers whose oriented box
// overlaps the player's sphere or capsule. Output buffer is not cleared
// by the query so callers control reuse.
void query_trigger_zones_against_sphere(
    const SphereVolume& player,
    std::span<const OrientedTriggerZone> zones,
    std::vector<std::size_t>& out_indices);
void query_trigger_zones_against_capsule(
    const CapsuleVolume& player,
    std::span<const OrientedTriggerZone> zones,
    std::vector<std::size_t>& out_indices);

} // namespace reaktio::gameplay
