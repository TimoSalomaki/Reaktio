#pragma once

#include "reaktio/foundation/CrashSafeLog.hpp"
#include "reaktio/foundation/Telemetry.hpp"
#include "reaktio/gameplay/GameModeRegistry.hpp"
#include "reaktio/gameplay/IModeHost.hpp"
#include "reaktio/platform/ApplicationConfig.hpp"
#include "reaktio/platform/FrameClock.hpp"
#include "reaktio/platform/InputSnapshot.hpp"
#include "reaktio/render/RenderExtraction.hpp"
#include "reaktio/platform/StackProbe.hpp"
#include "reaktio/platform/WindowState.hpp"

#include <iosfwd>

namespace reaktio::foundation {
struct BuildInfo;
} // namespace reaktio::foundation

namespace reaktio::platform {
class SdlApplicationShell;
} // namespace reaktio::platform

namespace reaktio::app {

struct SmokeApplicationDependencies {
    foundation::RuntimeBudget runtime_budget;
    platform::ApplicationConfig application_config;
    platform::StackProbe stack_probe;
    gameplay::GameModeRegistry game_mode_registry;
    std::ostream* log_stream{};
};

class SmokeApplication final : public gameplay::IModeHost {
  public:
    explicit SmokeApplication(SmokeApplicationDependencies dependencies);

    [[nodiscard]] int run();

    [[nodiscard]] const foundation::RuntimeBudget& runtime_budget() const noexcept override;
    [[nodiscard]] const platform::StackProbe& stack_probe() const noexcept override;
    [[nodiscard]] const platform::InputSnapshot& input_snapshot() const noexcept override;
    [[nodiscard]] const platform::FrameTiming& frame_timing() const noexcept override;
    [[nodiscard]] const platform::WindowState& window_state() const noexcept override;
    [[nodiscard]] render::RenderExtractionContext& render_extraction() noexcept override;
    [[nodiscard]] foundation::TelemetryRecorder& telemetry() noexcept override;
    void request_quit() noexcept override;
    [[nodiscard]] bool toggle_fullscreen() noexcept override;

  private:
    void log_startup(const foundation::BuildInfo& build_info);
    void log_window_state();

    SmokeApplicationDependencies dependencies_;
    foundation::TelemetryRecorder telemetry_recorder_;
    render::RenderExtractionContext render_extraction_context_;
    foundation::CrashSafeLog crash_safe_log_;
    platform::SdlApplicationShell* active_shell_{};
};

} // namespace reaktio::app