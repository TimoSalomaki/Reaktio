#include "reaktio/gameplay/SpatialCamera.hpp"

#include "reaktio/render/RenderCamera.hpp"

#include <cmath>

namespace reaktio::gameplay {

namespace {

[[nodiscard]] inline render::Float3 to_render(const Vector3& v) noexcept {
    return render::Float3{v.x, v.y, v.z};
}

[[nodiscard]] inline Vector3 add(const Vector3& a, const Vector3& b) noexcept {
    return Vector3{a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] inline Vector3 sub(const Vector3& a, const Vector3& b) noexcept {
    return Vector3{a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] inline Vector3 scale(const Vector3& a, float s) noexcept {
    return Vector3{a.x * s, a.y * s, a.z * s};
}

[[nodiscard]] inline Vector3 cross(const Vector3& a, const Vector3& b) noexcept {
    return Vector3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

[[nodiscard]] inline Vector3 normalize(const Vector3& v) noexcept {
    const float len_sq = v.x * v.x + v.y * v.y + v.z * v.z;
    if (len_sq <= 1e-12f) {
        return Vector3{0.0f, 0.0f, 1.0f};
    }
    const float inv = 1.0f / std::sqrt(len_sq);
    return Vector3{v.x * inv, v.y * inv, v.z * inv};
}

} // namespace

render::FreeCamera3D sample_tunnel_camera(const TunnelCameraRig& rig) noexcept {
    // Build orbit basis from the supplied axes. Forward is the tunnel
    // axis; up is whatever the rig says (assumed orthogonal); right is
    // the cross product. Player position rides on the orbit ring.
    const Vector3 forward = normalize(rig.tunnel_axis_forward);
    const Vector3 orbit_up = normalize(rig.tunnel_axis_up);
    const Vector3 orbit_right = normalize(cross(orbit_up, forward));

    const float c = std::cos(rig.player_heading_radians);
    const float s = std::sin(rig.player_heading_radians);
    const Vector3 player_offset = add(
        scale(orbit_right, c * rig.player_orbit_radius),
        scale(orbit_up, s * rig.player_orbit_radius));
    const Vector3 player_world = add(rig.tunnel_center, player_offset);

    // Camera trails the player BACK along forward (i.e. -forward) and is
    // lifted along the orbit up direction so the playfield is visible.
    const Vector3 camera_world = add(
        sub(player_world, scale(forward, rig.follow_back_distance)),
        scale(orbit_up, rig.follow_height_offset));
    const Vector3 look_target = add(player_world, scale(forward, 1.0f));
    const Vector3 view_dir = normalize(sub(look_target, camera_world));

    render::FreeCamera3D camera{};
    camera.position = to_render(camera_world);
    camera.forward = to_render(view_dir);
    camera.up = to_render(orbit_up);
    camera.vertical_fov_radians = rig.field_of_view_radians;
    camera.near_plane = rig.near_plane;
    camera.far_plane = rig.far_plane;
    return camera;
}

render::FreeCamera3D sample_orbital_camera(const OrbitalCameraRig& rig) noexcept {
    const Vector3 axis_up = normalize(rig.axis_up);
    // Build a right vector in the horizontal plane. If axis_up == world Y
    // we can lift the standard X axis; otherwise pick any vector not
    // parallel to up.
    Vector3 reference{1.0f, 0.0f, 0.0f};
    if (std::fabs(axis_up.x) > 0.9f) {
        reference = Vector3{0.0f, 0.0f, 1.0f};
    }
    const Vector3 right = normalize(cross(axis_up, reference));
    const Vector3 forward = normalize(cross(right, axis_up));

    const float c = std::cos(rig.orbit_heading_radians);
    const float s = std::sin(rig.orbit_heading_radians);
    const float p = std::cos(rig.orbit_pitch_radians);
    const float h = std::sin(rig.orbit_pitch_radians);
    // Spherical to cartesian using (right, forward, up) basis.
    const Vector3 ground = add(scale(right, c * p), scale(forward, s * p));
    const Vector3 orbit_offset = add(scale(ground, rig.orbit_radius), scale(axis_up, h * rig.orbit_radius));
    const Vector3 camera_world = add(rig.target_center, orbit_offset);
    const Vector3 view_dir = normalize(sub(rig.target_center, camera_world));

    render::FreeCamera3D camera{};
    camera.position = to_render(camera_world);
    camera.forward = to_render(view_dir);
    camera.up = to_render(axis_up);
    camera.vertical_fov_radians = rig.field_of_view_radians;
    camera.near_plane = rig.near_plane;
    camera.far_plane = rig.far_plane;
    return camera;
}

render::FreeCamera3D sample_high_speed_follow_camera(
    const HighSpeedFollowCameraRig& rig) noexcept {
    const Vector3 forward = normalize(rig.player_forward);
    const Vector3 up = normalize(rig.world_up);
    const Vector3 camera_world = add(
        sub(rig.player_position, scale(forward, rig.follow_back_distance)),
        scale(up, rig.follow_height_offset));
    const Vector3 look_target = add(rig.player_position, scale(forward, rig.look_ahead_distance));
    const Vector3 view_dir = normalize(sub(look_target, camera_world));

    render::FreeCamera3D camera{};
    camera.position = to_render(camera_world);
    camera.forward = to_render(view_dir);
    camera.up = to_render(up);
    camera.vertical_fov_radians = rig.field_of_view_radians;
    camera.near_plane = rig.near_plane;
    camera.far_plane = rig.far_plane;
    return camera;
}

} // namespace reaktio::gameplay
