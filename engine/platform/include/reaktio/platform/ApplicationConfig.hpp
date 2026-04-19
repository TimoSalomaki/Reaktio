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

enum class AudioSampleFormat {
    Unknown,
    S16,
    F32,
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

struct AudioConfig {
    bool enable_playback_device{true};
    bool fail_if_unavailable{false};
    int preferred_sample_rate{48000};
    int preferred_channels{2};
    int preferred_buffer_frames{1024};
    AudioSampleFormat preferred_format{AudioSampleFormat::F32};
    bool start_paused{true};
    float device_gain{1.0f};
};

struct DebugOptions {
    bool enable_startup_diagnostics{true};
    bool enable_debug_overlay{true};
    bool enable_input_diagnostics{false};
    bool enable_gpu_debug{false};
};

struct PostProcessConfig {
    bool enabled{false};
    float bloom_threshold{0.72f};
    float bloom_intensity{0.35f};
    float bloom_blur_scale{1.0f};
    float exposure{1.0f};
    float saturation{1.04f};
    float contrast{1.05f};
    float vignette_intensity{0.18f};
    float feedback_mix{0.08f};
    float feedback_decay{0.96f};
    float feedback_scale{1.004f};
    float color_grade_r{1.03f};
    float color_grade_g{1.00f};
    float color_grade_b{0.97f};
};

struct ApplicationConfig {
    WindowConfig window;
    MainLoopConfig main_loop;
    AudioConfig audio;
    RendererBackendPreference renderer_backend{RendererBackendPreference::Automatic};
    bool vsync_enabled{true};
    DebugOptions debug;
    PostProcessConfig post_process;
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

[[nodiscard]] inline constexpr std::string_view to_string(AudioSampleFormat format) noexcept {
    switch (format) {
    case AudioSampleFormat::Unknown:
        return "unknown";
    case AudioSampleFormat::S16:
        return "s16";
    case AudioSampleFormat::F32:
        return "f32";
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
    config.main_loop.max_frame_count = 16;
    config.audio.enable_playback_device = true;
    config.audio.fail_if_unavailable = false;
    config.audio.preferred_sample_rate = 48000;
    config.audio.preferred_channels = 2;
    config.audio.preferred_buffer_frames = 1024;
    config.audio.preferred_format = AudioSampleFormat::F32;
    config.audio.start_paused = true;
    config.audio.device_gain = 1.0f;
    config.renderer_backend = RendererBackendPreference::Noop;
    config.vsync_enabled = false;
    config.debug.enable_debug_overlay = false;
    return config;
}

} // namespace reaktio::platform