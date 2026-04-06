#include "reaktio/render/RenderExtraction.hpp"

namespace reaktio::render {

void RenderExtractionContext::begin_frame() noexcept {
    packets_.main_scene_clear.reset();
    packets_.debug_text_commands.clear();
}

void RenderExtractionContext::set_main_scene_clear(
    std::uint32_t rgba,
    float depth,
    std::uint8_t stencil) noexcept {
    packets_.main_scene_clear = ViewClearCommand{
        .view = RenderView::MainScene,
        .clear_color = true,
        .clear_depth = true,
        .rgba = rgba,
        .depth = depth,
        .stencil = stencil,
    };
}

void RenderExtractionContext::add_debug_text(
    std::uint16_t x,
    std::uint16_t y,
    std::uint8_t attribute,
    std::string_view text) {
    packets_.debug_text_commands.push_back(DebugTextCommand{
        .x = x,
        .y = y,
        .attribute = attribute,
        .text = std::string(text),
    });
}

const RenderFramePackets& RenderExtractionContext::packets() const noexcept {
    return packets_;
}

} // namespace reaktio::render