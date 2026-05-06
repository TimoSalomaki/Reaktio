#pragma once

#include "reaktio/gameplay/Transforms.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace reaktio::gameplay {

// Engine-layer rail/path primitive. Lives in the shared gameplay layer so
// every lane-, rail-, and runner-family mode (and eventually the spatial
// 3D slice) can drive player avatars, enemies, lane carriers, and chart cue
// presentation off the same deterministic geometry.
//
// Determinism rules:
// - Polyline geometry. No spline smoothing in v1; smoothing is a presentation
//   concern that can be layered on top without changing path identity.
// - Arc length and per-segment cumulative lengths are computed in double
//   precision once at rebuild() and never recomputed per query.
// - Sampling never throws and never asserts on out-of-range arc lengths;
//   it clamps deterministically. Looping is opt-in via wrap_arc_length().

struct RailPathControlPoint {
    Vector3 position{};
    Vector3 up_hint{0.0f, 1.0f, 0.0f};  // Used to build a stable orthonormal frame.
};

struct RailPathSample {
    Vector3 position{};
    Vector3 tangent{0.0f, 0.0f, 1.0f};   // Unit forward direction (segment direction).
    Vector3 normal{0.0f, 1.0f, 0.0f};    // Unit "up" relative to path.
    Vector3 binormal{1.0f, 0.0f, 0.0f};  // Unit "right"; right-handed: binormal = tangent x normal.
    double arc_length{};
    std::size_t segment_index{};
    double segment_alpha{};  // 0..1 along the segment that contains arc_length.
};

struct RailPathStatistics {
    std::size_t control_point_count{};
    std::size_t segment_count{};
    double total_length{};
    double shortest_segment_length{};
    double longest_segment_length{};
};

class RailPath {
  public:
    void clear() noexcept;
    [[nodiscard]] bool rebuild(std::vector<RailPathControlPoint> control_points);

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] double total_length() const noexcept;
    [[nodiscard]] std::size_t segment_count() const noexcept;
    [[nodiscard]] std::size_t control_point_count() const noexcept;
    [[nodiscard]] std::span<const RailPathControlPoint> control_points() const noexcept;
    [[nodiscard]] const RailPathStatistics& statistics() const noexcept;

    // Deterministic clamp [0, total_length].
    [[nodiscard]] RailPathSample sample_at_arc_length(double arc_length) const noexcept;
    // alpha clamped to [0, 1].
    [[nodiscard]] RailPathSample sample_at_alpha(double alpha) const noexcept;
    // Wrap helper for looped paths (closed circuits, infinite runners). Pure
    // arithmetic, no path data inspection — caller decides loop semantics.
    [[nodiscard]] double wrap_arc_length(double arc_length) const noexcept;

  private:
    [[nodiscard]] RailPathSample sample_segment(
        std::size_t segment_index, double segment_alpha, double arc_length) const noexcept;

    std::vector<RailPathControlPoint> control_points_;
    std::vector<double> cumulative_lengths_;  // size == segment_count + 1, [0]=0.
    RailPathStatistics statistics_{};
    bool valid_{false};
};

// Lateral rail offset for lane-style modes. Lane 0 is the path centerline;
// positive lanes lie along +binormal, negative along -binormal. Spacing is
// uniform; non-uniform layouts are a future extension that can subclass.
struct RailLaneLayout {
    std::int32_t lane_count{1};
    double lane_spacing{1.0};
    double vertical_offset{0.0};  // Along normal; useful for hover effects.
};

[[nodiscard]] Vector3 rail_lane_position(
    const RailPathSample& sample,
    std::int32_t signed_lane_index,
    const RailLaneLayout& layout) noexcept;

// 2.5D parallax layer: a presentation-only data structure that modes feed
// into render extraction. Speed scalar 1.0 = locked to camera, < 1.0 = drifts
// behind, > 1.0 = drifts ahead. Keeps presentation parameters out of cue
// identity, matching the architectural separation in Phase 0/Phase 5.
struct ParallaxLayer {
    double speed_scalar{1.0};
    double base_offset{0.0};
    double vertical_offset{0.0};
    Vector3 tint{1.0f, 1.0f, 1.0f};
};

struct ParallaxLayerStack {
    std::vector<ParallaxLayer> layers;
};

struct ParallaxLayerSample {
    double offset{};
    double vertical_offset{};
    Vector3 tint{1.0f, 1.0f, 1.0f};
};

// Resolve the runtime offset for each layer given a camera arc length along
// the rail. Output buffer is sized to layers.size(); pure function, no
// allocation when caller pre-sizes the output.
void sample_parallax_stack(
    const ParallaxLayerStack& stack,
    double camera_arc_length,
    std::vector<ParallaxLayerSample>& out_samples);

// Camera rig anchored to a rail. The eye sits at follow_distance behind the
// look-at target along the path; lateral and vertical offsets are applied in
// the path's local frame. Modes can author one rig per camera and let the
// rig do the rail math — no free-form camera math leaks into mode code.
struct RailCameraRig {
    double look_at_arc_length{0.0};
    double follow_distance{6.0};        // Distance behind look-at along path.
    double lateral_offset{0.0};         // Along binormal.
    double vertical_offset{1.5};        // Along normal.
    double field_of_view_radians{1.04719758};  // ~60 degrees.
    double near_plane{0.1};
    double far_plane{500.0};
    bool wrap_arc_length{false};
};

struct RailCameraSample {
    Vector3 eye{};
    Vector3 target{};
    Vector3 up{0.0f, 1.0f, 0.0f};
    double field_of_view_radians{1.04719758};
    double near_plane{0.1};
    double far_plane{500.0};
};

[[nodiscard]] RailCameraSample sample_rail_camera(
    const RailPath& path, const RailCameraRig& rig) noexcept;

} // namespace reaktio::gameplay
