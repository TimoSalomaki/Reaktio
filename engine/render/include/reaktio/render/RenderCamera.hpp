#pragma once

#include "reaktio/render/RenderTypes.hpp"

#include <array>
#include <cstdint>
#include <variant>

namespace reaktio::render {

struct Float2 {
    float x{};
    float y{};
};

struct Float3 {
    float x{};
    float y{};
    float z{};
};

struct OrthographicCamera2D {
    Float2 center{};
    float virtual_height{720.0f};
    float view_distance{10.0f};
    float aspect_ratio_override{};
    float near_plane{0.0f};
    float far_plane{100.0f};
};

struct PerspectiveCamera25D {
    Float3 eye{0.0f, 3.0f, -6.0f};
    Float3 target{0.0f, 0.0f, 0.0f};
    Float3 up{0.0f, 1.0f, 0.0f};
    float vertical_fov_radians{1.04719758f};
    float aspect_ratio_override{};
    float near_plane{0.1f};
    float far_plane{1000.0f};
};

struct FreeCamera3D {
    Float3 position{0.0f, 0.0f, -5.0f};
    Float3 forward{0.0f, 0.0f, 1.0f};
    Float3 up{0.0f, 1.0f, 0.0f};
    float vertical_fov_radians{1.04719758f};
    float aspect_ratio_override{};
    float near_plane{0.1f};
    float far_plane{1000.0f};
};

using CameraDefinition = std::variant<OrthographicCamera2D, PerspectiveCamera25D, FreeCamera3D>;

struct ViewCameraCommand {
    RenderView view{RenderView::MainScene};
    CameraDefinition camera;
};

struct CameraProjectionContext {
    std::uint16_t backbuffer_width{};
    std::uint16_t backbuffer_height{};
    bool homogeneous_depth{};
};

struct CameraMatrices {
    std::array<float, 16> view{};
    std::array<float, 16> projection{};
};

[[nodiscard]] CameraMatrices build_camera_matrices(
    const CameraDefinition& camera,
    const CameraProjectionContext& projection_context) noexcept;

[[nodiscard]] OrthographicCamera2D make_default_orthographic_camera_2d(
    std::uint16_t backbuffer_width,
    std::uint16_t backbuffer_height) noexcept;

} // namespace reaktio::render