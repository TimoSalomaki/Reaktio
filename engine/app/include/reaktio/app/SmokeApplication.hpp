#pragma once

#include "reaktio/foundation/Telemetry.hpp"
#include "reaktio/gameplay/GameModeRegistry.hpp"
#include "reaktio/gameplay/IModeHost.hpp"
#include "reaktio/platform/StackProbe.hpp"

#include <iosfwd>

namespace reaktio::app {

struct SmokeApplicationDependencies {
    foundation::RuntimeBudget runtime_budget;
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
    [[nodiscard]] foundation::TelemetryRecorder& telemetry() noexcept override;

  private:
    void log_startup() const;

    SmokeApplicationDependencies dependencies_;
    foundation::TelemetryRecorder telemetry_recorder_;
    std::ostream& log_stream_;
};

} // namespace reaktio::app