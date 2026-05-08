#include "reaktio/gameplay/SpatialKinematics.hpp"

#include <cmath>

namespace reaktio::gameplay {

namespace {

constexpr float k_two_pi = 6.28318530717958647692f;

[[nodiscard]] inline float wrap_angle(float angle_radians) noexcept {
    // Wrap to [-pi, +pi]. Using fmod keeps the result deterministic for
    // any reasonable input magnitude; unrolling avoids a branch on the
    // hot path while staying faithful to IEEE 754 semantics.
    constexpr float k_pi = 3.14159265358979323846f;
    float wrapped = std::fmod(angle_radians + k_pi, k_two_pi);
    if (wrapped < 0.0f) {
        wrapped += k_two_pi;
    }
    return wrapped - k_pi;
}

[[nodiscard]] inline Vector3 add(const Vector3& a, const Vector3& b) noexcept {
    return Vector3{a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] inline Vector3 scale(const Vector3& a, float s) noexcept {
    return Vector3{a.x * s, a.y * s, a.z * s};
}

} // namespace

void advance_rotational_kinematic(
    RotationalKinematicState& state, double fixed_delta_seconds) noexcept {
    state.angle_radians = wrap_angle(
        state.angle_radians +
        static_cast<float>(state.angular_velocity_radians_per_second * fixed_delta_seconds));
}

Vector3 sample_rotational_position(
    const RotationalKinematicState& state, const RotationalSampleConfig& config) noexcept {
    const float c = std::cos(state.angle_radians);
    const float s = std::sin(state.angle_radians);
    const Vector3 right_component = scale(config.axis_orbit_plane_right, c * config.radius);
    const Vector3 up_component = scale(config.axis_orbit_plane_up, s * config.radius);
    return add(config.center, add(right_component, up_component));
}

void advance_radial_sweep(
    RadialSweepState& state, double fixed_delta_seconds) noexcept {
    state.radius += static_cast<float>(state.radial_velocity_per_second * fixed_delta_seconds);
    if (state.radius < 0.0f) {
        state.radius = 0.0f;  // Clamp; modes detect "reached center" via radius reaching their
                              // configured min and despawn the body explicitly.
    }
    state.heading_radians = wrap_angle(state.heading_radians);
}

Vector3 sample_radial_sweep_position(
    const RadialSweepState& state, const RotationalSampleConfig& config) noexcept {
    const float c = std::cos(state.heading_radians);
    const float s = std::sin(state.heading_radians);
    const Vector3 right_component = scale(config.axis_orbit_plane_right, c * state.radius);
    const Vector3 up_component = scale(config.axis_orbit_plane_up, s * state.radius);
    return add(config.center, add(right_component, up_component));
}

void advance_oscillating_state(
    OscillatingState& state, double fixed_delta_seconds) noexcept {
    state.phase_radians +=
        static_cast<float>(k_two_pi * state.frequency_hz * fixed_delta_seconds);
    if (state.phase_radians >= k_two_pi) {
        state.phase_radians = std::fmod(state.phase_radians, k_two_pi);
    } else if (state.phase_radians < 0.0f) {
        state.phase_radians = std::fmod(state.phase_radians, k_two_pi) + k_two_pi;
    }
}

float evaluate_oscillating_offset(const OscillatingState& state) noexcept {
    return state.offset + state.amplitude * std::sin(state.phase_radians);
}

Vector3 sample_oscillating_position(
    const OscillatingState& state,
    const Vector3& center,
    const Vector3& axis_direction) noexcept {
    return add(center, scale(axis_direction, evaluate_oscillating_offset(state)));
}

void advance_rotational_kinematic_bulk(
    std::span<RotationalKinematicState> states, double fixed_delta_seconds) noexcept {
    for (RotationalKinematicState& state : states) {
        advance_rotational_kinematic(state, fixed_delta_seconds);
    }
}

void advance_radial_sweep_bulk(
    std::span<RadialSweepState> states, double fixed_delta_seconds) noexcept {
    for (RadialSweepState& state : states) {
        advance_radial_sweep(state, fixed_delta_seconds);
    }
}

void advance_oscillating_state_bulk(
    std::span<OscillatingState> states, double fixed_delta_seconds) noexcept {
    for (OscillatingState& state : states) {
        advance_oscillating_state(state, fixed_delta_seconds);
    }
}

} // namespace reaktio::gameplay
