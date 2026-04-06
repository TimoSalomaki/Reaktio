#pragma once

namespace reaktio::foundation {
class TelemetryRecorder;
struct RuntimeBudget;
} // namespace reaktio::foundation

namespace reaktio::platform {
class InputSnapshot;
struct FrameTiming;
struct StackProbe;
struct WindowState;
} // namespace reaktio::platform

namespace reaktio::gameplay {

class IModeHost {
  public:
    virtual ~IModeHost() = default;

    [[nodiscard]] virtual const foundation::RuntimeBudget& runtime_budget() const noexcept = 0;
    [[nodiscard]] virtual const platform::StackProbe& stack_probe() const noexcept = 0;
    [[nodiscard]] virtual const platform::InputSnapshot& input_snapshot() const noexcept = 0;
    [[nodiscard]] virtual const platform::FrameTiming& frame_timing() const noexcept = 0;
    [[nodiscard]] virtual const platform::WindowState& window_state() const noexcept = 0;
    [[nodiscard]] virtual foundation::TelemetryRecorder& telemetry() noexcept = 0;
    virtual void request_quit() noexcept = 0;
    [[nodiscard]] virtual bool toggle_fullscreen() noexcept = 0;
};

} // namespace reaktio::gameplay