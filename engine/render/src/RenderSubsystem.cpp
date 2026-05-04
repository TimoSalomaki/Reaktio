#include "reaktio/render/CookedAssetLibrary.hpp"
#include "reaktio/render/InstanceBufferAllocator.hpp"
#include "PostProcessChain.hpp"
#include "reaktio/render/RenderCamera.hpp"
#include "reaktio/render/RenderExtraction.hpp"
#include "reaktio/render/RenderSubsystem.hpp"
#include "reaktio/render/TransientBufferAllocator.hpp"

#include "reaktio/foundation/CrashSafeLog.hpp"
#include "reaktio/foundation/ResourceRegistry.hpp"
#include "reaktio/foundation/Telemetry.hpp"
#include "reaktio/platform/ApplicationConfig.hpp"
#include "reaktio/platform/FrameClock.hpp"
#include "reaktio/platform/InputSnapshot.hpp"
#include "reaktio/platform/WindowState.hpp"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace reaktio::render {

namespace {

std::string make_message(std::string_view prefix, std::string_view suffix) {
    std::string message(prefix);
    message.append(suffix);
    return message;
}

std::uint32_t color4_to_abgr(const Color4& c) noexcept {
    const auto clamp_byte = [](float v) -> std::uint8_t {
        return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return (static_cast<std::uint32_t>(clamp_byte(c.a)) << 24u) |
           (static_cast<std::uint32_t>(clamp_byte(c.b)) << 16u) |
           (static_cast<std::uint32_t>(clamp_byte(c.g)) << 8u) |
           static_cast<std::uint32_t>(clamp_byte(c.r));
}

void append_rotated_quad(
    float center_x,
    float center_y,
    float half_width,
    float half_height,
    float rotation_radians,
    std::uint32_t abgr,
    std::vector<TransientColorVertex>& vertices) {
    const float cos_r = std::cos(rotation_radians);
    const float sin_r = std::sin(rotation_radians);
    const float corners[4][2] = {
        {-half_width, -half_height},
        {half_width, -half_height},
        {half_width, half_height},
        {-half_width, half_height},
    };

    float rotated[4][2];
    for (int i = 0; i < 4; ++i) {
        rotated[i][0] = center_x + corners[i][0] * cos_r - corners[i][1] * sin_r;
        rotated[i][1] = center_y + corners[i][0] * sin_r + corners[i][1] * cos_r;
    }

    const int indices[] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; ++i) {
        vertices.push_back(TransientColorVertex{
            .x = rotated[indices[i]][0],
            .y = rotated[indices[i]][1],
            .z = 0.0f,
            .abgr = abgr,
        });
    }
}

void append_instanced_quad_fallback(
    const QuadInstanceData& instance,
    std::vector<TransientColorVertex>& vertices) {
    append_rotated_quad(
        instance.position_x,
        instance.position_y,
        instance.size_x * 0.5f,
        instance.size_y * 0.5f,
        instance.rotation_radians,
        instance.abgr,
        vertices);
}

void append_line(
    float start_x,
    float start_y,
    float end_x,
    float end_y,
    std::uint32_t abgr,
    std::vector<TransientColorVertex>& vertices) {
    vertices.push_back(TransientColorVertex{.x = start_x, .y = start_y, .z = 0.0f, .abgr = abgr});
    vertices.push_back(TransientColorVertex{.x = end_x, .y = end_y, .z = 0.0f, .abgr = abgr});
}

void submit_vertices(
    TransientBufferAllocator& allocator,
    RenderView view,
    BufferPrimitive primitive,
    BufferBlendMode blend_mode,
    std::span<const TransientColorVertex> vertices,
    bool write_depth = false) {
    (void)allocator.submit(TransientBufferSubmission{
        .view = view,
        .primitive = primitive,
        .blend_mode = blend_mode,
        .write_depth = write_depth,
        .vertices = vertices,
    });
}

void submit_sprites(
    const std::vector<SpriteCommand>& commands,
    TransientBufferAllocator& allocator) {
    constexpr std::size_t k_view_count = static_cast<std::size_t>(RenderView::Count);
    std::array<std::vector<TransientColorVertex>, k_view_count> vertices_by_view;
    for (const SpriteCommand& command : commands) {
        append_rotated_quad(
            command.position.x,
            command.position.y,
            command.size.x * 0.5f,
            command.size.y * 0.5f,
            command.rotation_radians,
            color4_to_abgr(command.color),
            vertices_by_view[to_view_id(command.view)]);
    }

    for (std::size_t view_index = 0; view_index < vertices_by_view.size(); ++view_index) {
        submit_vertices(
            allocator,
            static_cast<RenderView>(view_index),
            BufferPrimitive::Triangles,
            BufferBlendMode::Alpha,
            vertices_by_view[view_index]);
    }
}

void submit_quad_batches(
    const std::vector<QuadBatchCommand>& commands,
    TransientBufferAllocator& allocator) {
    constexpr std::size_t k_view_count = static_cast<std::size_t>(RenderView::Count);
    std::array<std::vector<TransientColorVertex>, k_view_count> vertices_by_view;
    for (const QuadBatchCommand& command : commands) {
        std::vector<TransientColorVertex>& view_vertices = vertices_by_view[to_view_id(command.view)];
        for (const QuadBatchInstance& quad : command.quads) {
            append_rotated_quad(
                quad.position.x,
                quad.position.y,
                quad.size.x * 0.5f,
                quad.size.y * 0.5f,
                quad.rotation_radians,
                color4_to_abgr(quad.color),
                view_vertices);
        }
    }

    for (std::size_t view_index = 0; view_index < vertices_by_view.size(); ++view_index) {
        submit_vertices(
            allocator,
            static_cast<RenderView>(view_index),
            BufferPrimitive::Triangles,
            BufferBlendMode::Alpha,
            vertices_by_view[view_index]);
    }
}

void submit_instanced_quad_batches(
    const std::vector<InstancedQuadBatchCommand>& commands,
    InstanceBufferAllocator& instance_allocator,
    TransientBufferAllocator& transient_allocator,
    RenderStats& stats) {
    constexpr std::size_t k_view_count = static_cast<std::size_t>(RenderView::Count);
    std::array<std::vector<QuadInstanceData>, k_view_count> instances_by_view;
    std::array<std::vector<TransientColorVertex>, k_view_count> fallback_vertices_by_view;

    for (const InstancedQuadBatchCommand& command : commands) {
        std::vector<QuadInstanceData>& view_instances = instances_by_view[to_view_id(command.view)];
        view_instances.reserve(view_instances.size() + command.quads.size());
        for (const InstancedQuadInstance& quad : command.quads) {
            view_instances.push_back(QuadInstanceData{
                .position_x = quad.position.x,
                .position_y = quad.position.y,
                .size_x = quad.size.x,
                .size_y = quad.size.y,
                .rotation_radians = quad.rotation_radians,
                .abgr = color4_to_abgr(quad.color),
            });
        }
    }

    for (std::size_t view_index = 0; view_index < instances_by_view.size(); ++view_index) {
        std::vector<QuadInstanceData>& instances = instances_by_view[view_index];
        if (instances.empty()) {
            continue;
        }

        if (instance_allocator.submit_quads(static_cast<RenderView>(view_index), instances)) {
            ++stats.instanced_batches;
            stats.instanced_instances += static_cast<std::uint32_t>(instances.size());
            continue;
        }

        ++stats.instancing_fallback_batches;
        std::vector<TransientColorVertex>& fallback_vertices = fallback_vertices_by_view[view_index];
        fallback_vertices.reserve(instances.size() * 6u);
        for (const QuadInstanceData& instance : instances) {
            append_instanced_quad_fallback(instance, fallback_vertices);
        }
    }

    for (std::size_t view_index = 0; view_index < fallback_vertices_by_view.size(); ++view_index) {
        submit_vertices(
            transient_allocator,
            static_cast<RenderView>(view_index),
            BufferPrimitive::Triangles,
            BufferBlendMode::Alpha,
            fallback_vertices_by_view[view_index]);
    }
}

void submit_lines(
    const std::vector<LineCommand>& commands,
    TransientBufferAllocator& allocator) {
    constexpr std::size_t k_view_count = static_cast<std::size_t>(RenderView::Count);
    std::array<std::vector<TransientColorVertex>, k_view_count> vertices_by_view;
    for (const LineCommand& command : commands) {
        append_line(
            command.start.x,
            command.start.y,
            command.end.x,
            command.end.y,
            color4_to_abgr(command.color),
            vertices_by_view[to_view_id(command.view)]);
    }

    for (std::size_t view_index = 0; view_index < vertices_by_view.size(); ++view_index) {
        submit_vertices(
            allocator,
            static_cast<RenderView>(view_index),
            BufferPrimitive::Lines,
            BufferBlendMode::Alpha,
            vertices_by_view[view_index]);
    }
}

void submit_particle_batches(
    const std::vector<ParticleBatchCommand>& commands,
    TransientBufferAllocator& allocator) {
    constexpr std::size_t k_view_count = static_cast<std::size_t>(RenderView::Count);
    std::array<std::vector<TransientColorVertex>, k_view_count> vertices_by_view;
    for (const ParticleBatchCommand& command : commands) {
        std::vector<TransientColorVertex>& view_vertices = vertices_by_view[to_view_id(command.view)];
        for (const ParticleInstance& particle : command.particles) {
            append_rotated_quad(
                particle.position.x,
                particle.position.y,
                particle.size.x * 0.5f,
                particle.size.y * 0.5f,
                particle.rotation_radians,
                color4_to_abgr(particle.color),
                view_vertices);
        }
    }

    for (std::size_t view_index = 0; view_index < vertices_by_view.size(); ++view_index) {
        submit_vertices(
            allocator,
            static_cast<RenderView>(view_index),
            BufferPrimitive::Triangles,
            BufferBlendMode::Additive,
            vertices_by_view[view_index]);
    }
}

void submit_transient_geometry(
    const std::vector<TransientGeometryCommand>& commands,
    TransientBufferAllocator& allocator) {
    for (const TransientGeometryCommand& command : commands) {
        submit_vertices(
            allocator,
            command.view,
            command.primitive,
            command.blend_mode,
            command.vertices,
            command.write_depth);
    }
}

void submit_debug_lines(
    const std::vector<DebugLineCommand>& commands,
    TransientBufferAllocator& allocator) {
    std::vector<TransientColorVertex> vertices;
    vertices.reserve(commands.size() * 2u);
    for (const DebugLineCommand& command : commands) {
        append_line(command.start.x, command.start.y, command.end.x, command.end.y, command.rgba, vertices);
    }

    submit_vertices(allocator, RenderView::MainScene, BufferPrimitive::Lines, BufferBlendMode::Alpha, vertices);
}

void submit_debug_rects(
    const std::vector<DebugRectCommand>& commands,
    TransientBufferAllocator& allocator) {
    std::vector<TransientColorVertex> vertices;
    vertices.reserve(commands.size() * 8u);
    for (const DebugRectCommand& command : commands) {
        const float x0 = command.position.x - command.half_extents.x;
        const float y0 = command.position.y - command.half_extents.y;
        const float x1 = command.position.x + command.half_extents.x;
        const float y1 = command.position.y + command.half_extents.y;

        append_line(x0, y0, x1, y0, command.rgba, vertices);
        append_line(x1, y0, x1, y1, command.rgba, vertices);
        append_line(x1, y1, x0, y1, command.rgba, vertices);
        append_line(x0, y1, x0, y0, command.rgba, vertices);
    }

    submit_vertices(allocator, RenderView::MainScene, BufferPrimitive::Lines, BufferBlendMode::Alpha, vertices);
}

void submit_debug_circles(
    const std::vector<DebugCircleCommand>& commands,
    TransientBufferAllocator& allocator) {
    std::vector<TransientColorVertex> vertices;
    for (const DebugCircleCommand& command : commands) {
        const std::uint16_t segments = std::max<std::uint16_t>(command.segments, 3u);
        const float step = 6.28318531f / static_cast<float>(segments);
        vertices.reserve(vertices.size() + static_cast<std::size_t>(segments) * 2u);
        for (std::uint16_t segment = 0; segment < segments; ++segment) {
            const float angle0 = step * static_cast<float>(segment);
            const float angle1 = step * static_cast<float>(segment + 1u);
            append_line(
                command.center.x + std::cos(angle0) * command.radius,
                command.center.y + std::sin(angle0) * command.radius,
                command.center.x + std::cos(angle1) * command.radius,
                command.center.y + std::sin(angle1) * command.radius,
                command.rgba,
                vertices);
        }
    }

    submit_vertices(allocator, RenderView::MainScene, BufferPrimitive::Lines, BufferBlendMode::Alpha, vertices);
}

bgfx::RendererType::Enum map_renderer_backend(platform::RendererBackendPreference preference) noexcept {
    switch (preference) {
    case platform::RendererBackendPreference::Automatic:
        return bgfx::RendererType::Count;
    case platform::RendererBackendPreference::Noop:
        return bgfx::RendererType::Noop;
    case platform::RendererBackendPreference::Direct3D11:
        return bgfx::RendererType::Direct3D11;
    case platform::RendererBackendPreference::Direct3D12:
        return bgfx::RendererType::Direct3D12;
    case platform::RendererBackendPreference::Vulkan:
        return bgfx::RendererType::Vulkan;
    case platform::RendererBackendPreference::OpenGL:
        return bgfx::RendererType::OpenGL;
    case platform::RendererBackendPreference::OpenGLES:
        return bgfx::RendererType::OpenGLES;
    case platform::RendererBackendPreference::Metal:
        return bgfx::RendererType::Metal;
    case platform::RendererBackendPreference::WebGPU:
        return bgfx::RendererType::WebGPU;
    }

    return bgfx::RendererType::Count;
}

bgfx::NativeWindowHandleType::Enum map_handle_type(platform::NativeWindowPlatform platform) noexcept {
    switch (platform) {
    case platform::NativeWindowPlatform::Wayland:
        return bgfx::NativeWindowHandleType::Wayland;
    case platform::NativeWindowPlatform::Unknown:
    case platform::NativeWindowPlatform::Win32:
    case platform::NativeWindowPlatform::Cocoa:
    case platform::NativeWindowPlatform::X11:
        return bgfx::NativeWindowHandleType::Default;
    }

    return bgfx::NativeWindowHandleType::Default;
}

std::uint32_t make_reset_flags(const platform::ApplicationConfig& config) noexcept {
    std::uint32_t flags = BGFX_RESET_NONE;
    if (config.vsync_enabled) {
        flags |= BGFX_RESET_VSYNC;
    }

    return flags;
}

std::uint16_t clamp_dimension(int dimension) noexcept {
    return static_cast<std::uint16_t>(std::clamp(dimension, 1, 65535));
}

} // namespace

struct RenderSubsystem::Impl {
    Impl(const platform::ApplicationConfig& app_config, foundation::CrashSafeLog& sink)
        : config(app_config),
                    log(&sink),
                    post_process(app_config.post_process, sink) {}

    ~Impl() {
        shutdown();
    }

    bool initialize(const platform::WindowState& window_state) {
        if (stats.initialized) {
            return true;
        }

        reset_flags = make_reset_flags(config);
        backbuffer_width = clamp_dimension(window_state.pixel_width);
        backbuffer_height = clamp_dimension(window_state.pixel_height);

        if (!render_frame_primed) {
            // The bootstrap runtime keeps bgfx on the API thread for predictable frame pacing
            // and to avoid carrying a render-thread lifecycle before the renderer matures.
            (void)bgfx::renderFrame();
            render_frame_primed = true;
        }

        const bgfx::RendererType::Enum preferred_renderer = map_renderer_backend(config.renderer_backend);
        const bool use_native_window = preferred_renderer != bgfx::RendererType::Noop;
        if (try_initialize(window_state, preferred_renderer, use_native_window, false)) {
            finalize_initialization();
            return true;
        }

        log->write(
            foundation::LogLevel::Warning,
            make_message("bgfx initialization failed for preferred renderer, falling back to headless noop backend: ",
                bgfx::getRendererName(bgfx::RendererType::Noop)));

        if (!try_initialize(window_state, bgfx::RendererType::Noop, false, true)) {
            log->write(foundation::LogLevel::Error, "bgfx initialization failed for both preferred and noop renderers.");
            return false;
        }

        finalize_initialization();
        return true;
    }

    void begin_frame(const platform::WindowState& window_state) {
        if (!stats.initialized) {
            return;
        }

        const std::uint16_t requested_width = clamp_dimension(window_state.pixel_width);
        const std::uint16_t requested_height = clamp_dimension(window_state.pixel_height);
        if (requested_width != backbuffer_width || requested_height != backbuffer_height) {
            backbuffer_width = requested_width;
            backbuffer_height = requested_height;
            bgfx::reset(backbuffer_width, backbuffer_height, reset_flags);
        }

        post_process.begin_frame(backbuffer_width, backbuffer_height);
        configure_views();
        apply_camera_command(ViewCameraCommand{
            .view = RenderView::MainScene,
            .camera = make_default_orthographic_camera_2d(backbuffer_width, backbuffer_height),
        });
        bgfx::touch(to_view_id(RenderView::MainScene));
        transient_buffers.begin_frame();
        instance_buffers.begin_frame();
        debug_text_active_this_frame = false;
        debug_flags_this_frame = config.debug.enable_gpu_debug ? BGFX_DEBUG_STATS : BGFX_DEBUG_NONE;
        bgfx::setDebug(debug_flags_this_frame);
        stats.instanced_batches = 0;
        stats.instanced_instances = 0;
        stats.instancing_fallback_batches = 0;
        stats.instance_failed_allocations = 0;
        stats.post_process_enabled = post_process.active();
        stats.post_process_pass_count = post_process.pass_count();
    }

    bool load_cooked_assets(foundation::ResourceRegistry& resource_registry) {
        if (!cooked_assets.load(resource_registry, *log)) {
            return false;
        }

        const CookedAssetLibrarySummary& summary = cooked_assets.summary();
        stats.loaded_textures = static_cast<std::uint32_t>(summary.texture_count);
        stats.loaded_meshes = static_cast<std::uint32_t>(summary.mesh_count);
        stats.loaded_fonts = static_cast<std::uint32_t>(summary.font_count);
        stats.loaded_asset_bytes = summary.total_payload_bytes;
        cooked_asset_source_storage = summary.loaded_from_manifest
            ? summary.manifest_path.string()
            : std::string("<none>");
        stats.cooked_asset_source = cooked_asset_source_storage;
        return true;
    }

    bool load_cooked_assets(const std::filesystem::path& manifest_path, foundation::ResourceRegistry& resource_registry) {
        if (!cooked_assets.load(manifest_path, resource_registry, *log)) {
            return false;
        }

        const CookedAssetLibrarySummary& summary = cooked_assets.summary();
        stats.loaded_textures = static_cast<std::uint32_t>(summary.texture_count);
        stats.loaded_meshes = static_cast<std::uint32_t>(summary.mesh_count);
        stats.loaded_fonts = static_cast<std::uint32_t>(summary.font_count);
        stats.loaded_asset_bytes = summary.total_payload_bytes;
        cooked_asset_source_storage = summary.loaded_from_manifest
            ? summary.manifest_path.string()
            : std::string("<none>");
        stats.cooked_asset_source = cooked_asset_source_storage;
        return true;
    }

    void submit_extracted_frame(const RenderFramePackets& packets) noexcept {
        if (!stats.initialized) {
            return;
        }

        if (packets.main_scene_clear.has_value()) {
            const ViewClearCommand& clear = *packets.main_scene_clear;
            std::uint16_t clear_flags = BGFX_CLEAR_NONE;
            if (clear.clear_color) {
                clear_flags |= BGFX_CLEAR_COLOR;
            }
            if (clear.clear_depth) {
                clear_flags |= BGFX_CLEAR_DEPTH;
            }

            bgfx::setViewClear(
                to_view_id(clear.view),
                clear_flags,
                clear.rgba,
                clear.depth,
                clear.stencil);
        }

        for (const ViewCameraCommand& command : packets.camera_commands) {
            apply_camera_command(command);
        }

        if (!packets.debug_text_commands.empty()) {
            ensure_debug_text();
            for (const DebugTextCommand& command : packets.debug_text_commands) {
                bgfx::dbgTextPrintf(command.x, command.y, command.attribute, "%s", command.text.c_str());
            }
        }

        submit_sprites(packets.sprite_commands, transient_buffers);
        submit_quad_batches(packets.quad_batch_commands, transient_buffers);
        submit_instanced_quad_batches(
            packets.instanced_quad_batch_commands,
            instance_buffers,
            transient_buffers,
            stats);
        submit_lines(packets.line_commands, transient_buffers);
        submit_particle_batches(packets.particle_batch_commands, transient_buffers);
        submit_transient_geometry(packets.transient_geometry_commands, transient_buffers);
        submit_debug_lines(packets.debug_line_commands, transient_buffers);
        submit_debug_rects(packets.debug_rect_commands, transient_buffers);
        submit_debug_circles(packets.debug_circle_commands, transient_buffers);

        const TransientBufferBudget& transient_budget = transient_buffers.budget();
        stats.transient_vertices = transient_budget.allocated_vertices;
        stats.transient_allocations = transient_budget.allocation_count;
        stats.transient_failed_allocations = transient_budget.failed_allocations;
        stats.instance_failed_allocations = instance_buffers.budget().failed_allocations;
    }

    void draw_debug_overlay(
        const platform::FrameTiming& frame_timing,
        const platform::InputSnapshot& input_snapshot,
        const foundation::TelemetrySnapshot* telemetry_snapshot) noexcept {
        if (!stats.initialized || !config.debug.enable_debug_overlay) {
            return;
        }

        ensure_debug_text();
        bgfx::dbgTextPrintf(0, 0, 0x4f, "Reaktio Debug Overlay");
        bgfx::dbgTextPrintf(0, 1, 0x0f, "renderer: %s", stats.renderer_name.data());
        bgfx::dbgTextPrintf(
            0,
            2,
            0x0f,
            "frame %llu dt=%.2fms fixed_steps=%u alpha=%.2f",
            static_cast<unsigned long long>(frame_timing.frame_index),
            frame_timing.frame_delta_seconds * 1000.0,
            frame_timing.fixed_steps_this_frame,
            frame_timing.interpolation_alpha);

        if (telemetry_snapshot != nullptr) {
            bgfx::dbgTextPrintf(
                0,
                3,
                0x0f,
                "cpu frame=%.2fms sim=%.2fms render=%.2fms mem=%zuMiB",
                telemetry_snapshot->frame_ms,
                telemetry_snapshot->simulation_ms,
                telemetry_snapshot->render_submission_ms,
                telemetry_snapshot->resident_memory_mib);
        }

        bgfx::dbgTextPrintf(
            0,
            4,
            0x0f,
            "draw=%u compute=%u blit=%u backbuffer=%ux%u",
            stats.draw_calls,
            stats.compute_calls,
            stats.blit_calls,
            stats.backbuffer_width,
            stats.backbuffer_height);

        bgfx::dbgTextPrintf(
            0,
            5,
            0x0f,
            "mouse=(%.1f, %.1f) buttons=0x%08x wheel=(%.1f, %.1f)",
            input_snapshot.mouse_x(),
            input_snapshot.mouse_y(),
            input_snapshot.mouse_button_mask(),
            input_snapshot.mouse_wheel_x(),
            input_snapshot.mouse_wheel_y());

        bgfx::dbgTextPrintf(
            0,
            6,
            0x0f,
            "keys=%zu text=%zu edit=%zu gamepads=%zu",
            input_snapshot.keyboard_events().size(),
            input_snapshot.text_input_events().size(),
            input_snapshot.text_editing_events().size(),
            input_snapshot.connected_gamepads().size());

        bgfx::dbgTextPrintf(
            0,
            7,
            0x0f,
            "transient verts=%u alloc=%u failed=%u",
            stats.transient_vertices,
            stats.transient_allocations,
            stats.transient_failed_allocations);

            bgfx::dbgTextPrintf(
                0,
                8,
                0x0f,
                "instanced batches=%u instances=%u fallback=%u failed=%u",
                stats.instanced_batches,
                stats.instanced_instances,
                stats.instancing_fallback_batches,
                stats.instance_failed_allocations);

            bgfx::dbgTextPrintf(
                0,
                9,
                0x0f,
                "assets tex=%u mesh=%u font=%u bytes=%zu",
                stats.loaded_textures,
                stats.loaded_meshes,
                stats.loaded_fonts,
                stats.loaded_asset_bytes);

            bgfx::dbgTextPrintf(
                0,
                10,
                0x0f,
                "post enabled=%u passes=%u",
                stats.post_process_enabled ? 1u : 0u,
                stats.post_process_pass_count);
    }

    void end_frame() {
        if (!stats.initialized) {
            return;
        }

        post_process.submit();
        bgfx::frame();
        refresh_stats();
    }

    void shutdown() noexcept {
        if (stats.initialized) {
                post_process.shutdown();
            instance_buffers.shutdown();
            bgfx::shutdown();
            stats = RenderStats{};
            backbuffer_width = 0;
            backbuffer_height = 0;
            reset_flags = 0;
            using_headless_fallback = false;
            render_frame_primed = false;
        }
    }

    bool try_initialize(
        const platform::WindowState& window_state,
        bgfx::RendererType::Enum renderer_type,
        bool use_native_window,
        bool headless_fallback) {
        bgfx::Init init{};
        init.type = renderer_type;
        init.debug = config.debug.enable_gpu_debug;
        init.profile = config.debug.enable_gpu_debug;
        init.fallback = true;
        init.resolution.width = backbuffer_width;
        init.resolution.height = backbuffer_height;
        init.resolution.reset = reset_flags;

        if (use_native_window) {
            init.platformData.ndt = window_state.native_handle.display_connection;
            init.platformData.nwh = reinterpret_cast<void*>(window_state.native_handle.primary_handle);
            init.platformData.type = map_handle_type(window_state.native_handle.platform);
        }

        using_headless_fallback = headless_fallback;
        return bgfx::init(init);
    }

    void finalize_initialization() {
        stats.initialized = true;
        stats.using_headless_fallback = using_headless_fallback;
        transient_buffers.set_vertex_budget(65536u);
        instance_buffers.set_instance_budget(4096u);
        (void)instance_buffers.initialize();
        if (!post_process.initialize()) {
            log->write(
                foundation::LogLevel::Warning,
                "Renderer post-process chain could not initialize; continuing without fullscreen post-processing.");
        }
        configure_views();
        refresh_stats();
    }

    void configure_views() const {
        bgfx::setViewName(to_view_id(RenderView::MainScene), "MainScene");
        bgfx::setViewRect(to_view_id(RenderView::MainScene), 0, 0, backbuffer_width, backbuffer_height);
        bgfx::setViewFrameBuffer(to_view_id(RenderView::MainScene), post_process.scene_frame_buffer());
        bgfx::setViewClear(
            to_view_id(RenderView::MainScene),
            BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
            0x101820ff,
            1.0f,
            0);
        post_process.configure_views();
        bgfx::setViewName(to_view_id(RenderView::DebugOverlay), "DebugOverlay");
        bgfx::setViewRect(to_view_id(RenderView::DebugOverlay), 0, 0, backbuffer_width, backbuffer_height);
        bgfx::setViewFrameBuffer(to_view_id(RenderView::DebugOverlay), BGFX_INVALID_HANDLE);
        bgfx::setViewClear(to_view_id(RenderView::DebugOverlay), BGFX_CLEAR_NONE, 0x00000000, 1.0f, 0);
    }

    void refresh_stats() {
        if (!stats.initialized) {
            return;
        }

        if (const bgfx::Caps* caps = bgfx::getCaps()) {
            homogeneous_depth = caps->homogeneousDepth;
        }

        const bgfx::RendererType::Enum renderer_type = bgfx::getRendererType();
        stats.renderer_name = bgfx::getRendererName(renderer_type);
        stats.backbuffer_width = backbuffer_width;
        stats.backbuffer_height = backbuffer_height;
        stats.reset_flags = reset_flags;
        stats.view_count = to_view_id(RenderView::Count);
        stats.post_process_enabled = post_process.active();
        stats.post_process_pass_count = post_process.pass_count();
        stats.draw_calls = 0;
        stats.compute_calls = 0;
        stats.blit_calls = 0;

        if (renderer_type == bgfx::RendererType::Noop) {
            return;
        }

        if (const bgfx::Stats* bgfx_stats = bgfx::getStats()) {
            stats.draw_calls = bgfx_stats->numDraw;
            stats.compute_calls = bgfx_stats->numCompute;
            stats.blit_calls = bgfx_stats->numBlit;
            stats.backbuffer_width = bgfx_stats->width;
            stats.backbuffer_height = bgfx_stats->height;
            stats.view_count = std::max<std::uint16_t>(bgfx_stats->numViews, to_view_id(RenderView::Count));
        }
    }

    void ensure_debug_text() noexcept {
        if (debug_text_active_this_frame) {
            return;
        }

        debug_flags_this_frame |= BGFX_DEBUG_TEXT;
        bgfx::setDebug(debug_flags_this_frame);
        bgfx::dbgTextClear(0x00, false);
        debug_text_active_this_frame = true;
    }

    void apply_camera_command(const ViewCameraCommand& command) noexcept {
        const CameraMatrices matrices = build_camera_matrices(command.camera, CameraProjectionContext{
            .backbuffer_width = backbuffer_width,
            .backbuffer_height = backbuffer_height,
            .homogeneous_depth = homogeneous_depth,
        });
        bgfx::setViewTransform(
            to_view_id(command.view),
            matrices.view.data(),
            matrices.projection.data());
    }

    platform::ApplicationConfig config;
    foundation::CrashSafeLog* log;
    RenderStats stats;
    CookedAssetLibrary cooked_assets;
    PostProcessChain post_process;
    std::string cooked_asset_source_storage{"<none>"};
    TransientBufferAllocator transient_buffers;
    InstanceBufferAllocator instance_buffers;
    std::uint16_t backbuffer_width{};
    std::uint16_t backbuffer_height{};
    std::uint32_t reset_flags{};
    std::uint32_t debug_flags_this_frame{};
    bool homogeneous_depth{false};
    bool using_headless_fallback{false};
    bool debug_text_active_this_frame{false};
    bool render_frame_primed{false};
};

RenderSubsystem::RenderSubsystem(const platform::ApplicationConfig& config, foundation::CrashSafeLog& log)
    : impl_(std::make_unique<Impl>(config, log)) {}

RenderSubsystem::~RenderSubsystem() = default;

bool RenderSubsystem::initialize(const platform::WindowState& window_state) {
    return impl_->initialize(window_state);
}

bool RenderSubsystem::load_cooked_assets(foundation::ResourceRegistry& resource_registry) {
    return impl_->load_cooked_assets(resource_registry);
}

bool RenderSubsystem::load_cooked_assets(
    const std::filesystem::path& manifest_path,
    foundation::ResourceRegistry& resource_registry) {
    return impl_->load_cooked_assets(manifest_path, resource_registry);
}

void RenderSubsystem::begin_frame(const platform::WindowState& window_state) {
    impl_->begin_frame(window_state);
}

void RenderSubsystem::submit_extracted_frame(const RenderFramePackets& packets) noexcept {
    impl_->submit_extracted_frame(packets);
}

void RenderSubsystem::draw_debug_overlay(
    const platform::FrameTiming& frame_timing,
    const platform::InputSnapshot& input_snapshot,
    const foundation::TelemetrySnapshot* telemetry_snapshot) noexcept {
    impl_->draw_debug_overlay(frame_timing, input_snapshot, telemetry_snapshot);
}

void RenderSubsystem::end_frame() {
    impl_->end_frame();
}

const RenderStats& RenderSubsystem::stats() const noexcept {
    return impl_->stats;
}

} // namespace reaktio::render
