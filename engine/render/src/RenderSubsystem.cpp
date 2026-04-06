#include "reaktio/render/RenderExtraction.hpp"
#include "reaktio/render/RenderSubsystem.hpp"

#include "reaktio/foundation/CrashSafeLog.hpp"
#include "reaktio/foundation/Telemetry.hpp"
#include "reaktio/platform/ApplicationConfig.hpp"
#include "reaktio/platform/FrameClock.hpp"
#include "reaktio/platform/InputSnapshot.hpp"
#include "reaktio/platform/WindowState.hpp"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace reaktio::render {

namespace {

std::string make_message(std::string_view prefix, std::string_view suffix) {
    std::string message(prefix);
    message.append(suffix);
    return message;
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
          log(&sink) {}

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

        configure_views();
        bgfx::touch(to_view_id(RenderView::MainScene));
        debug_text_active_this_frame = false;
        debug_flags_this_frame = config.debug.enable_gpu_debug ? BGFX_DEBUG_STATS : BGFX_DEBUG_NONE;
        bgfx::setDebug(debug_flags_this_frame);
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

        if (!packets.debug_text_commands.empty()) {
            ensure_debug_text();
            for (const DebugTextCommand& command : packets.debug_text_commands) {
                bgfx::dbgTextPrintf(command.x, command.y, command.attribute, "%s", command.text.c_str());
            }
        }
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
    }

    void end_frame() {
        if (!stats.initialized) {
            return;
        }

        bgfx::frame();
        refresh_stats();
    }

    void shutdown() noexcept {
        if (stats.initialized) {
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
        configure_views();
        refresh_stats();
    }

    void configure_views() const {
        bgfx::setViewRect(to_view_id(RenderView::MainScene), 0, 0, backbuffer_width, backbuffer_height);
        bgfx::setViewClear(
            to_view_id(RenderView::MainScene),
            BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
            0x101820ff,
            1.0f,
            0);
        bgfx::setViewRect(to_view_id(RenderView::DebugOverlay), 0, 0, backbuffer_width, backbuffer_height);
        bgfx::setViewClear(to_view_id(RenderView::DebugOverlay), BGFX_CLEAR_NONE, 0x00000000, 1.0f, 0);
    }

    void refresh_stats() {
        if (!stats.initialized) {
            return;
        }

        const bgfx::RendererType::Enum renderer_type = bgfx::getRendererType();
        stats.renderer_name = bgfx::getRendererName(renderer_type);
        stats.backbuffer_width = backbuffer_width;
        stats.backbuffer_height = backbuffer_height;
        stats.reset_flags = reset_flags;
        stats.view_count = to_view_id(RenderView::Count);
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

    platform::ApplicationConfig config;
    foundation::CrashSafeLog* log;
    RenderStats stats;
    std::uint16_t backbuffer_width{};
    std::uint16_t backbuffer_height{};
    std::uint32_t reset_flags{};
    std::uint32_t debug_flags_this_frame{};
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