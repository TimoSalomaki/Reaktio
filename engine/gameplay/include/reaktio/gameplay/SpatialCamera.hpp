#pragma once

#include "reaktio/gameplay/Transforms.hpp"

#include <cstdint>

namespace reaktio::render {
struct FreeCamera3D;
} // namespace reaktio::render

namespace reaktio::gameplay {

// Engine-layer 3D camera rigs for spatial / obstacle-heavy modes. The
// rigs produce a render::FreeCamera3D from a small parameter pack so
// modes never compute eye/target/up triples by hand. Mirrors the
// rail-family RailCameraRig pattern: the rig is a plain-data struct
// holding policy, the helper is a pure function returning a camera.
//
// Three rigs cover the spatial archetypes Phase 10 calls out:
//   - TunnelCameraRig: looks down a tunnel along an axis. The camera
//     trails the player by a fixed back-distance along the tunnel axis
//     and orbits with the player around the tunnel axis. Used for
//     Super Hexagon-like radial fields and spaceship corridor modes.
//   - OrbitalCameraRig: stationary camera offset from a center point,
//     looking inward. Used for top-down arenas and god's-eye spatial
//     puzzles.
//   - HighSpeedFollowCameraRig: trails the player along the player's
//     forward direction with a short lerp. Used for runner / spaceship
//     modes where the camera should glide rather than snap.
//
// Each rig samples a render::FreeCamera3D; modes feed that to
// IRenderExtractionContext::set_view_camera(). Modes that want a 2.5D
// presentation can keep using PerspectiveCamera25D directly.

struct TunnelCameraRig {
    Vector3 tunnel_center{};
    Vector3 tunnel_axis_forward{0.0f, 0.0f, 1.0f};   // Down the tunnel.
    Vector3 tunnel_axis_up{0.0f, 1.0f, 0.0f};        // Defines orbit "up".
    // Player heading around the tunnel axis (radians) and orbital radius
    // (world units). The camera mirrors the heading so the player stays
    // centered horizontally.
    float player_heading_radians{0.0f};
    float player_orbit_radius{6.0f};
    // Camera offsets relative to the player.
    float follow_back_distance{8.0f};   // Behind the player along axis_forward.
    float follow_height_offset{1.5f};   // Lifted along tunnel_axis_up * orbit-up.
    float field_of_view_radians{1.04719758f};
    float near_plane{0.1f};
    float far_plane{500.0f};
};

struct OrbitalCameraRig {
    Vector3 target_center{};
    Vector3 axis_up{0.0f, 1.0f, 0.0f};
    float orbit_radius{12.0f};
    float orbit_heading_radians{0.0f};
    float orbit_pitch_radians{0.785f};   // ~45 degrees down by default.
    float field_of_view_radians{1.04719758f};
    float near_plane{0.1f};
    float far_plane{500.0f};
};

struct HighSpeedFollowCameraRig {
    Vector3 player_position{};
    Vector3 player_forward{0.0f, 0.0f, 1.0f};
    Vector3 world_up{0.0f, 1.0f, 0.0f};
    float follow_back_distance{6.0f};
    float follow_height_offset{1.5f};
    float look_ahead_distance{4.0f};
    float field_of_view_radians{1.04719758f};
    float near_plane{0.1f};
    float far_plane{1000.0f};
};

[[nodiscard]] render::FreeCamera3D sample_tunnel_camera(const TunnelCameraRig& rig) noexcept;
[[nodiscard]] render::FreeCamera3D sample_orbital_camera(const OrbitalCameraRig& rig) noexcept;
[[nodiscard]] render::FreeCamera3D sample_high_speed_follow_camera(
    const HighSpeedFollowCameraRig& rig) noexcept;

} // namespace reaktio::gameplay
