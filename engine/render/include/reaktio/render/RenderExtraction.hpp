#pragma once

#include "reaktio/render/MaterialRegistry.hpp"
#include "reaktio/render/RenderCamera.hpp"
#include "reaktio/render/TransientBufferAllocator.hpp"
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

struct Color4 {
    float r{1.0f};
    float g{1.0f};
    float b{1.0f};
    float a{1.0f};
};

struct SpriteCommand {
    RenderView view{RenderView::MainScene};
    Float2 position{};
    Float2 size{1.0f, 1.0f};
    float rotation_radians{};
    Color4 color{};
    MaterialHandle material{};
    std::uint16_t sort_key{};
};

struct QuadBatchInstance {
    Float2 position{};
    Float2 size{1.0f, 1.0f};
    float rotation_radians{};
    Color4 color{};
};

struct QuadBatchCommand {
    RenderView view{RenderView::MainScene};
    std::vector<QuadBatchInstance> quads;
    MaterialHandle material{};
    std::uint16_t sort_key{};
};

struct InstancedQuadInstance {
    Float2 position{};
    Float2 size{1.0f, 1.0f};
    float rotation_radians{};
    Color4 color{};
};

struct InstancedQuadBatchCommand {
    RenderView view{RenderView::MainScene};
    std::vector<InstancedQuadInstance> quads;
    MaterialHandle material{};
    std::uint16_t sort_key{};
};

struct LineCommand {
    RenderView view{RenderView::MainScene};
    Float2 start{};
    Float2 end{};
    Color4 color{};
    std::uint16_t sort_key{};
};

struct ParticleInstance {
    Float2 position{};
    Float2 size{8.0f, 8.0f};
    float rotation_radians{};
    Color4 color{};
};

struct ParticleBatchCommand {
    RenderView view{RenderView::MainScene};
    std::vector<ParticleInstance> particles;
    MaterialHandle material{};
    std::uint16_t sort_key{};
};

struct TextCommand {
    RenderView view{RenderView::MainScene};
    Float2 position{};
    float scale{1.0f};
    Color4 color{};
    std::string text;
    std::uint16_t sort_key{};
};

struct MeshCommand {
    RenderView view{RenderView::MainScene};
    Float3 position{};
    Float3 scale{1.0f, 1.0f, 1.0f};
    Float3 rotation_euler_radians{};
    Color4 color{};
    MaterialHandle material{};
    std::uint16_t sort_key{};
};

struct TransientGeometryCommand {
    RenderView view{RenderView::MainScene};
    BufferPrimitive primitive{BufferPrimitive::Triangles};
    BufferBlendMode blend_mode{BufferBlendMode::Alpha};
    bool write_depth{};
    std::vector<TransientColorVertex> vertices;
    std::uint16_t sort_key{};
};

struct DebugLineCommand {
    Float2 start{};
    Float2 end{};
    std::uint32_t rgba{0xffffffff};
};

struct DebugRectCommand {
    Float2 position{};
    Float2 half_extents{};
    std::uint32_t rgba{0xffffffff};
};

struct DebugCircleCommand {
    Float2 center{};
    float radius{};
    std::uint32_t rgba{0xffffffff};
    std::uint16_t segments{16};
};

struct RenderFramePackets {
    std::optional<ViewClearCommand> main_scene_clear;
    std::vector<ViewCameraCommand> camera_commands;
    std::vector<DebugTextCommand> debug_text_commands;
    std::vector<SpriteCommand> sprite_commands;
    std::vector<QuadBatchCommand> quad_batch_commands;
    std::vector<InstancedQuadBatchCommand> instanced_quad_batch_commands;
    std::vector<LineCommand> line_commands;
    std::vector<ParticleBatchCommand> particle_batch_commands;
    std::vector<TextCommand> text_commands;
    std::vector<MeshCommand> mesh_commands;
    std::vector<TransientGeometryCommand> transient_geometry_commands;
    std::vector<DebugLineCommand> debug_line_commands;
    std::vector<DebugRectCommand> debug_rect_commands;
    std::vector<DebugCircleCommand> debug_circle_commands;
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

    void add_sprite(const SpriteCommand& command);
    void add_quad_batch(const QuadBatchCommand& command);
    void add_instanced_quad_batch(const InstancedQuadBatchCommand& command);
    void add_line(const LineCommand& command);
    void add_particle_batch(const ParticleBatchCommand& command);
    void add_text(const TextCommand& command);
    void add_mesh(const MeshCommand& command);
    void add_transient_geometry(const TransientGeometryCommand& command);
    void add_debug_line(const DebugLineCommand& command);
    void add_debug_rect(const DebugRectCommand& command);
    void add_debug_circle(const DebugCircleCommand& command);

    [[nodiscard]] const RenderFramePackets& packets() const noexcept;

  private:
    RenderFramePackets packets_;
};

} // namespace reaktio::render