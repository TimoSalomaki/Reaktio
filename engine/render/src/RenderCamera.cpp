#include "reaktio/render/RenderCamera.hpp"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace reaktio::render {

namespace {

bx::Vec3 to_bx(const Float3& vector) noexcept {
    return bx::Vec3{vector.x, vector.y, vector.z};
}

Float3 add(const Float3& left, const Float3& right) noexcept {
    return Float3{
        left.x + right.x,
        left.y + right.y,
        left.z + right.z,
    };
}

Float3 normalize(const Float3& vector, const Float3& fallback) noexcept {
    const float length_squared = vector.x * vector.x + vector.y * vector.y + vector.z * vector.z;
    if (length_squared <= 0.000001f) {
        return fallback;
    }

    const float inverse_length = 1.0f / std::sqrt(length_squared);
    return Float3{
        vector.x * inverse_length,
        vector.y * inverse_length,
        vector.z * inverse_length,
    };
}

float resolve_aspect_ratio(float override_aspect_ratio, const CameraProjectionContext& projection_context) noexcept {
    if (override_aspect_ratio > 0.0f) {
        return override_aspect_ratio;
    }

    const std::uint16_t width = std::max<std::uint16_t>(projection_context.backbuffer_width, 1);
    const std::uint16_t height = std::max<std::uint16_t>(projection_context.backbuffer_height, 1);
    return static_cast<float>(width) / static_cast<float>(height);
}

float resolve_far_plane(float near_plane, float far_plane) noexcept {
    return far_plane > near_plane ? far_plane : near_plane + 1.0f;
}

float resolve_perspective_near_plane(float near_plane) noexcept {
    return std::max(near_plane, 0.001f);
}

float resolve_vertical_fov_degrees(float vertical_fov_radians) noexcept {
    constexpr float k_min_fov_radians = 0.0174532925f;
    constexpr float k_max_fov_radians = 3.05432619f;
    return bx::toDeg(std::clamp(vertical_fov_radians, k_min_fov_radians, k_max_fov_radians));
}

CameraMatrices build_orthographic_camera_matrices(
    const OrthographicCamera2D& camera,
    const CameraProjectionContext& projection_context) noexcept {
    CameraMatrices matrices{};

    const float aspect_ratio = resolve_aspect_ratio(camera.aspect_ratio_override, projection_context);
    const float half_height = std::max(camera.virtual_height * 0.5f, 0.5f);
    const float half_width = half_height * aspect_ratio;
    const float eye_distance = std::max(camera.view_distance, 0.001f);

    const bx::Vec3 eye{camera.center.x, camera.center.y, -eye_distance};
    const bx::Vec3 target{camera.center.x, camera.center.y, 0.0f};
    bx::mtxLookAt(matrices.view.data(), eye, target);
    bx::mtxOrtho(
        matrices.projection.data(),
        camera.center.x - half_width,
        camera.center.x + half_width,
        camera.center.y - half_height,
        camera.center.y + half_height,
        camera.near_plane,
        resolve_far_plane(camera.near_plane, camera.far_plane),
        0.0f,
        projection_context.homogeneous_depth);

    return matrices;
}

CameraMatrices build_perspective_camera_matrices(
    const PerspectiveCamera25D& camera,
    const CameraProjectionContext& projection_context) noexcept {
    CameraMatrices matrices{};

    const float near_plane = resolve_perspective_near_plane(camera.near_plane);

    bx::mtxLookAt(matrices.view.data(), to_bx(camera.eye), to_bx(camera.target), to_bx(camera.up));
    bx::mtxProj(
        matrices.projection.data(),
        resolve_vertical_fov_degrees(camera.vertical_fov_radians),
        resolve_aspect_ratio(camera.aspect_ratio_override, projection_context),
        near_plane,
        resolve_far_plane(near_plane, camera.far_plane),
        projection_context.homogeneous_depth);

    return matrices;
}

CameraMatrices build_free_camera_matrices(
    const FreeCamera3D& camera,
    const CameraProjectionContext& projection_context) noexcept {
    CameraMatrices matrices{};

    const Float3 direction = normalize(camera.forward, Float3{0.0f, 0.0f, 1.0f});
    const Float3 up = normalize(camera.up, Float3{0.0f, 1.0f, 0.0f});
    const Float3 target = add(camera.position, direction);
    const float near_plane = resolve_perspective_near_plane(camera.near_plane);

    bx::mtxLookAt(matrices.view.data(), to_bx(camera.position), to_bx(target), to_bx(up));
    bx::mtxProj(
        matrices.projection.data(),
        resolve_vertical_fov_degrees(camera.vertical_fov_radians),
        resolve_aspect_ratio(camera.aspect_ratio_override, projection_context),
        near_plane,
        resolve_far_plane(near_plane, camera.far_plane),
        projection_context.homogeneous_depth);

    return matrices;
}

} // namespace

CameraMatrices build_camera_matrices(
    const CameraDefinition& camera,
    const CameraProjectionContext& projection_context) noexcept {
    return std::visit(
        [&projection_context](const auto& typed_camera) -> CameraMatrices {
            using CameraType = std::decay_t<decltype(typed_camera)>;

            if constexpr (std::is_same_v<CameraType, OrthographicCamera2D>) {
                return build_orthographic_camera_matrices(typed_camera, projection_context);
            } else if constexpr (std::is_same_v<CameraType, PerspectiveCamera25D>) {
                return build_perspective_camera_matrices(typed_camera, projection_context);
            } else {
                return build_free_camera_matrices(typed_camera, projection_context);
            }
        },
        camera);
}

OrthographicCamera2D make_default_orthographic_camera_2d(
    std::uint16_t backbuffer_width,
    std::uint16_t backbuffer_height) noexcept {
    OrthographicCamera2D camera{};
    camera.virtual_height = static_cast<float>(std::max<std::uint16_t>(backbuffer_height, 1));
    camera.aspect_ratio_override = static_cast<float>(std::max<std::uint16_t>(backbuffer_width, 1)) /
                                   static_cast<float>(std::max<std::uint16_t>(backbuffer_height, 1));
    return camera;
}

} // namespace reaktio::render