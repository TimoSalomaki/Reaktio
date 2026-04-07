#pragma once

namespace reaktio::foundation {
class DeterministicRandomService;
class TelemetryRecorder;
struct RuntimeBudget;
} // namespace reaktio::foundation

namespace reaktio::platform {
class InputSnapshot;
struct FrameTiming;
struct StackProbe;
struct WindowState;
} // namespace reaktio::platform

namespace reaktio::render {
class RenderExtractionContext;
} // namespace reaktio::render

namespace reaktio::gameplay {
class EventBus;
class ITransportControl;
class ReplayRecorder;
} // namespace reaktio::gameplay

namespace reaktio::gameplay {

class IModeHost {
  public:
    virtual ~IModeHost() = default;

    [[nodiscard]] virtual const foundation::RuntimeBudget& runtime_budget() const noexcept = 0;
    [[nodiscard]] virtual const platform::StackProbe& stack_probe() const noexcept = 0;
    [[nodiscard]] virtual const platform::InputSnapshot& input_snapshot() const noexcept = 0;
    [[nodiscard]] virtual const platform::FrameTiming& frame_timing() const noexcept = 0;
    [[nodiscard]] virtual const platform::WindowState& window_state() const noexcept = 0;
    [[nodiscard]] virtual foundation::DeterministicRandomService& random_service() noexcept = 0;
    [[nodiscard]] virtual EventBus& event_bus() noexcept = 0;
    [[nodiscard]] virtual ReplayRecorder& replay() noexcept = 0;
    [[nodiscard]] virtual ITransportControl& transport() noexcept = 0;
    [[nodiscard]] virtual render::RenderExtractionContext& render_extraction() noexcept = 0;
    [[nodiscard]] virtual foundation::TelemetryRecorder& telemetry() noexcept = 0;
    virtual void request_quit() noexcept = 0;
    [[nodiscard]] virtual bool toggle_fullscreen() noexcept = 0;
};

} // namespace reaktio::gameplay