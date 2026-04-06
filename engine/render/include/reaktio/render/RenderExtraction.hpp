#pragma once

#include "reaktio/render/RenderCamera.hpp"
#include "reaktio/render/RenderTypes.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace reaktio::render {

struct ViewClearCommand {
    RenderView view{RenderView::MainScene};
    bool clear_color{true};
    bool clear_depth{true};
    std::uint32_t rgba{0x101820ff};
    float depth{1.0f};
    std::uint8_t stencil{};
};

struct DebugTextCommand {
    std::uint16_t x{};
    std::uint16_t y{};
    std::uint8_t attribute{0x0f};
    std::string text;
};

struct RenderFramePackets {
    std::optional<ViewClearCommand> main_scene_clear;
    std::vector<ViewCameraCommand> camera_commands;
    std::vector<DebugTextCommand> debug_text_commands;
};

class RenderExtractionContext {
  public:
    void begin_frame() noexcept;
    void set_main_scene_clear(
        std::uint32_t rgba,
        float depth = 1.0f,
        std::uint8_t stencil = 0) noexcept;
    void add_debug_text(
        std::uint16_t x,
        std::uint16_t y,
        std::uint8_t attribute,
        std::string_view text);
    void set_view_camera(RenderView view, const OrthographicCamera2D& camera);
    void set_view_camera(RenderView view, const PerspectiveCamera25D& camera);
    void set_view_camera(RenderView view, const FreeCamera3D& camera);

    [[nodiscard]] const RenderFramePackets& packets() const noexcept;

  private:
    RenderFramePackets packets_;
};

} // namespace reaktio::render