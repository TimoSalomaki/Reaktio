#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace reaktio::platform {

enum class RendererBackendPreference {
    Automatic,
    Noop,
    Direct3D11,
    Direct3D12,
    Vulkan,
    OpenGL,
    OpenGLES,
    Metal,
    WebGPU,
};

enum class WindowMode {
    Windowed,
    BorderlessWindowed,
    Fullscreen,
};

struct WindowConfig {
    std::string title{"Reaktio"};
    int width{1280};
    int height{720};
    WindowMode mode{WindowMode::Windowed};
    bool resizable{true};
    bool high_pixel_density{true};
    bool start_hidden{true};
    bool enable_text_input{true};
};

struct MainLoopConfig {
    double fixed_step_seconds{1.0 / 120.0};
    double max_frame_delta_seconds{0.25};
    std::uint32_t max_fixed_steps_per_frame{8};
    std::uint64_t max_frame_count{4};
};

struct DebugOptions {
    bool enable_startup_diagnostics{true};
    bool enable_input_diagnostics{false};
    bool enable_gpu_debug{false};
};

struct ApplicationConfig {
    WindowConfig window;
    MainLoopConfig main_loop;
    RendererBackendPreference renderer_backend{RendererBackendPreference::Automatic};
    bool vsync_enabled{true};
    DebugOptions debug;
    std::string app_identifier{"fi.reaktio.engine"};
    std::string log_file_name{"reaktio-runtime.log"};
};

[[nodiscard]] inline constexpr std::string_view to_string(RendererBackendPreference preference) noexcept {
    switch (preference) {
    case RendererBackendPreference::Automatic:
        return "automatic";
    case RendererBackendPreference::Noop:
        return "noop";
    case RendererBackendPreference::Direct3D11:
        return "direct3d11";
    case RendererBackendPreference::Direct3D12:
        return "direct3d12";
    case RendererBackendPreference::Vulkan:
        return "vulkan";
    case RendererBackendPreference::OpenGL:
        return "opengl";
    case RendererBackendPreference::OpenGLES:
        return "opengles";
    case RendererBackendPreference::Metal:
        return "metal";
    case RendererBackendPreference::WebGPU:
        return "webgpu";
    }

    return "unknown";
}

[[nodiscard]] inline constexpr std::string_view to_string(WindowMode mode) noexcept {
    switch (mode) {
    case WindowMode::Windowed:
        return "windowed";
    case WindowMode::BorderlessWindowed:
        return "borderless-windowed";
    case WindowMode::Fullscreen:
        return "fullscreen";
    }

    return "unknown";
}

[[nodiscard]] inline ApplicationConfig make_smoke_application_config() {
    ApplicationConfig config{};
    config.window.title = "Reaktio Platform Smoke";
    config.window.width = 1280;
    config.window.height = 720;
    config.window.mode = WindowMode::Windowed;
    config.window.start_hidden = true;
    config.window.enable_text_input = true;
    config.main_loop.max_frame_count = 4;
    config.renderer_backend = RendererBackendPreference::Automatic;
    config.vsync_enabled = false;
    return config;
}

} // namespace reaktio::platform