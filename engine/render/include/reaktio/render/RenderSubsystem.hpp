#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

namespace reaktio::foundation {
class CrashSafeLog;
struct TelemetrySnapshot;
} // namespace reaktio::foundation

namespace reaktio::platform {
struct ApplicationConfig;
struct FrameTiming;
class InputSnapshot;
struct WindowState;
} // namespace reaktio::platform

namespace reaktio::render {

enum class RenderView : std::uint16_t {
    MainScene = 0,
    DebugOverlay = 1,
    Count = 2,
};

[[nodiscard]] constexpr std::uint16_t to_view_id(RenderView view) noexcept {
    return static_cast<std::uint16_t>(view);
}

struct RenderStats {
    bool initialized{false};
    bool using_headless_fallback{false};
    std::uint16_t backbuffer_width{};
    std::uint16_t backbuffer_height{};
    std::uint16_t view_count{};
    std::uint32_t draw_calls{};
    std::uint32_t compute_calls{};
    std::uint32_t blit_calls{};
    std::uint32_t reset_flags{};
    std::string_view renderer_name{"uninitialized"};
};

class RenderSubsystem {
  public:
    RenderSubsystem(const platform::ApplicationConfig& config, foundation::CrashSafeLog& log);
    ~RenderSubsystem();

    RenderSubsystem(const RenderSubsystem&) = delete;
    RenderSubsystem& operator=(const RenderSubsystem&) = delete;

    bool initialize(const platform::WindowState& window_state);
    void begin_frame(const platform::WindowState& window_state);
    void draw_debug_overlay(
        const platform::FrameTiming& frame_timing,
        const platform::InputSnapshot& input_snapshot,
        const foundation::TelemetrySnapshot* telemetry_snapshot) noexcept;
    void end_frame();

    [[nodiscard]] const RenderStats& stats() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace reaktio::render