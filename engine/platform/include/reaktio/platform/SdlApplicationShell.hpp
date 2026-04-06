#pragma once

#include "reaktio/foundation/CrashSafeLog.hpp"
#include "reaktio/platform/ApplicationConfig.hpp"
#include "reaktio/platform/FrameClock.hpp"
#include "reaktio/platform/InputSnapshot.hpp"
#include "reaktio/platform/WindowState.hpp"

#include <memory>

namespace reaktio::platform {

class SdlApplicationShell {
  public:
    SdlApplicationShell(ApplicationConfig config, foundation::CrashSafeLog& log);
    ~SdlApplicationShell();

    SdlApplicationShell(const SdlApplicationShell&) = delete;
    SdlApplicationShell& operator=(const SdlApplicationShell&) = delete;

    bool initialize();
    void begin_frame();
    void pump_events();
    void present() noexcept;
    void sleep_until_next_fixed_step() noexcept;

    void request_quit() noexcept;
    [[nodiscard]] bool should_quit() const noexcept;
    [[nodiscard]] bool toggle_fullscreen();

    [[nodiscard]] const ApplicationConfig& config() const noexcept;
    [[nodiscard]] FrameClock& frame_clock() noexcept;
    [[nodiscard]] const FrameTiming& frame_timing() const noexcept;
    [[nodiscard]] const InputSnapshot& input_snapshot() const noexcept;
    [[nodiscard]] const WindowState& window_state() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace reaktio::platform