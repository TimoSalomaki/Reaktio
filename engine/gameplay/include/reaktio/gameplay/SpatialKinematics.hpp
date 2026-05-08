#pragma once

#include "reaktio/gameplay/Transforms.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace reaktio::gameplay {

// Engine-layer deterministic kinematic primitives for spatial / 3D modes.
// Three composable building blocks cover the motion archetypes Phase 10
// targets (Super Hexagon-like radial sweeps, Geometry Dash-like rotating
// gears, Thumper-like beat-synced wall pulses) without forcing a closed
// taxonomy on game modes:
//
//   - RotationalKinematicState: body that rotates around a fixed axis at
//     a constant angular velocity. Used for the player's orbital position
//     and for rotating hazard "gears".
//   - RadialSweepState: body that travels along a radial direction with a
//     fixed scalar velocity. Used for incoming ring slices that fly toward
//     the player.
//   - OscillatingState: body that oscillates along a fixed direction
//     using a phase + frequency pair. Used for moving walls.
//
// Each primitive is a plain-data struct and a pure free function. State
// is owned by the mode (or by a parent registry); helpers neither
// allocate nor mutate hidden globals. Replay-safe by construction.

struct RotationalKinematicState {
    // Body local angle, advanced by angular_velocity_radians_per_second
    // each fixed step. Wrapped into [-pi, +pi] to avoid float drift.
    float angle_radians{0.0f};
    float angular_velocity_radians_per_second{0.0f};
};

void advance_rotational_kinematic(
    RotationalKinematicState& state, double fixed_delta_seconds) noexcept;

// Sample the orbiting position around an axis. The axis is assumed
// orthogonal to the orbit plane; for the typical Super Hexagon-like
// setup with a vertical Y axis, the orbit lies in the XZ plane.
struct RotationalSampleConfig {
    Vector3 center{};
    Vector3 axis_orbit_plane_right{1.0f, 0.0f, 0.0f};   // Defines angle 0.
    Vector3 axis_orbit_plane_up{0.0f, 0.0f, 1.0f};      // Defines angle +pi/2.
    float radius{1.0f};
};

[[nodiscard]] Vector3 sample_rotational_position(
    const RotationalKinematicState& state, const RotationalSampleConfig& config) noexcept;

struct RadialSweepState {
    // Distance from the orbit center along the body's outgoing direction.
    // radial_velocity is signed: negative values pull the body inward.
    float radius{1.0f};
    float radial_velocity_per_second{0.0f};
    // The body's heading angle around the orbit axis (radians). Constant
    // for a single sweep; modes that want spiraling sweeps wrap the body
    // in a RotationalKinematicState whose state drives `heading_radians`.
    float heading_radians{0.0f};
};

void advance_radial_sweep(
    RadialSweepState& state, double fixed_delta_seconds) noexcept;

[[nodiscard]] Vector3 sample_radial_sweep_position(
    const RadialSweepState& state, const RotationalSampleConfig& config) noexcept;

// Oscillation primitive. phase wraps in [0, 2*pi). amplitude and offset
// are world units along axis_direction; the resulting position is
// center + axis_direction * (offset + amplitude * sin(phase)).
struct OscillatingState {
    float phase_radians{0.0f};
    float frequency_hz{1.0f};
    float amplitude{1.0f};
    float offset{0.0f};
};

void advance_oscillating_state(
    OscillatingState& state, double fixed_delta_seconds) noexcept;

[[nodiscard]] float evaluate_oscillating_offset(const OscillatingState& state) noexcept;

[[nodiscard]] Vector3 sample_oscillating_position(
    const OscillatingState& state,
    const Vector3& center,
    const Vector3& axis_direction) noexcept;

// Bulk advance helpers: most spatial modes carry hundreds or thousands of
// hazards; advancing in bulk avoids repeated parameter shuffling and lets
// the compiler vectorize trivially. These are pure functions over the
// state span and dt; modes own the buffers.
void advance_rotational_kinematic_bulk(
    std::span<RotationalKinematicState> states, double fixed_delta_seconds) noexcept;
void advance_radial_sweep_bulk(
    std::span<RadialSweepState> states, double fixed_delta_seconds) noexcept;
void advance_oscillating_state_bulk(
    std::span<OscillatingState> states, double fixed_delta_seconds) noexcept;

} // namespace reaktio::gameplay
