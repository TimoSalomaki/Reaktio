#include "reaktio/render/RenderExtraction.hpp"

namespace reaktio::render {

namespace {

void upsert_camera_command(
    std::vector<ViewCameraCommand>& camera_commands,
    RenderView view,
    CameraDefinition camera) {
    for (ViewCameraCommand& command : camera_commands) {
        if (command.view == view) {
            command.camera = std::move(camera);
            return;
        }
    }

    camera_commands.push_back(ViewCameraCommand{
        .view = view,
        .camera = std::move(camera),
    });
}

} // namespace

void RenderExtractionContext::begin_frame() noexcept {
    packets_.main_scene_clear.reset();
    packets_.camera_commands.clear();
    packets_.debug_text_commands.clear();
    packets_.sprite_commands.clear();
    packets_.quad_batch_commands.clear();
    packets_.instanced_quad_batch_commands.clear();
    packets_.line_commands.clear();
    packets_.particle_batch_commands.clear();
    packets_.text_commands.clear();
    packets_.mesh_commands.clear();
    packets_.transient_geometry_commands.clear();
    packets_.debug_line_commands.clear();
    packets_.debug_rect_commands.clear();
    packets_.debug_circle_commands.clear();
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

void RenderExtractionContext::set_view_camera(RenderView view, const OrthographicCamera2D& camera) {
    upsert_camera_command(packets_.camera_commands, view, camera);
}

void RenderExtractionContext::set_view_camera(RenderView view, const PerspectiveCamera25D& camera) {
    upsert_camera_command(packets_.camera_commands, view, camera);
}

void RenderExtractionContext::set_view_camera(RenderView view, const FreeCamera3D& camera) {
    upsert_camera_command(packets_.camera_commands, view, camera);
}

void RenderExtractionContext::add_sprite(const SpriteCommand& command) {
    packets_.sprite_commands.push_back(command);
}

void RenderExtractionContext::add_quad_batch(const QuadBatchCommand& command) {
    packets_.quad_batch_commands.push_back(command);
}

void RenderExtractionContext::add_instanced_quad_batch(const InstancedQuadBatchCommand& command) {
    packets_.instanced_quad_batch_commands.push_back(command);
}

void RenderExtractionContext::add_line(const LineCommand& command) {
    packets_.line_commands.push_back(command);
}

void RenderExtractionContext::add_particle_batch(const ParticleBatchCommand& command) {
    packets_.particle_batch_commands.push_back(command);
}

void RenderExtractionContext::add_text(const TextCommand& command) {
    packets_.text_commands.push_back(command);
}

void RenderExtractionContext::add_mesh(const MeshCommand& command) {
    packets_.mesh_commands.push_back(command);
}

void RenderExtractionContext::add_transient_geometry(const TransientGeometryCommand& command) {
    packets_.transient_geometry_commands.push_back(command);
}

void RenderExtractionContext::add_debug_line(const DebugLineCommand& command) {
    packets_.debug_line_commands.push_back(command);
}

void RenderExtractionContext::add_debug_rect(const DebugRectCommand& command) {
    packets_.debug_rect_commands.push_back(command);
}

void RenderExtractionContext::add_debug_circle(const DebugCircleCommand& command) {
    packets_.debug_circle_commands.push_back(command);
}

const RenderFramePackets& RenderExtractionContext::packets() const noexcept {
    return packets_;
}

} // namespace reaktio::render