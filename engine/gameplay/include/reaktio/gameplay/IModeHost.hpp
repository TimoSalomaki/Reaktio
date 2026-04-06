#pragma once

namespace reaktio::foundation {
class TelemetryRecorder;
struct RuntimeBudget;
} // namespace reaktio::foundation

namespace reaktio::platform {
struct StackProbe;
} // namespace reaktio::platform

namespace reaktio::gameplay {

class IModeHost {
  public:
    virtual ~IModeHost() = default;

    [[nodiscard]] virtual const foundation::RuntimeBudget& runtime_budget() const noexcept = 0;
    [[nodiscard]] virtual const platform::StackProbe& stack_probe() const noexcept = 0;
    [[nodiscard]] virtual foundation::TelemetryRecorder& telemetry() noexcept = 0;
};

} // namespace reaktio::gameplay