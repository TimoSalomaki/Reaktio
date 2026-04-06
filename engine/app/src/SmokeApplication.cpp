#include "reaktio/app/SmokeApplication.hpp"

#include "reaktio/foundation/BuildInfo.hpp"

#include <chrono>
#include <iostream>

namespace reaktio::app {

SmokeApplication::SmokeApplication(SmokeApplicationDependencies dependencies)
    : dependencies_(std::move(dependencies)),
      log_stream_(dependencies_.log_stream != nullptr ? *dependencies_.log_stream : std::cout) {}

int SmokeApplication::run() {
    log_startup();

    std::unique_ptr<gameplay::IGameMode> mode = dependencies_.game_mode_registry.create_first();
    if (!mode) {
        log_stream_ << "No game modes are registered.\n";
        return 1;
    }

    log_stream_ << "Running mode: " << mode->descriptor().display_name << " (" << mode->descriptor().id
                << ")\n";

    const auto frame_start = std::chrono::steady_clock::now();
    mode->on_enter(*this);

    const auto simulation_start = std::chrono::steady_clock::now();
    mode->on_fixed_step(*this, 1.0 / 120.0);
    const auto simulation_end = std::chrono::steady_clock::now();

    mode->on_exit(*this);
    const auto frame_end = std::chrono::steady_clock::now();

    if (foundation::TelemetrySnapshot* snapshot = telemetry_recorder_.last_mutable()) {
        snapshot->frame_ms = std::chrono::duration<double, std::milli>(frame_end - frame_start).count();
        snapshot->simulation_ms =
            std::chrono::duration<double, std::milli>(simulation_end - simulation_start).count();
        snapshot->resident_memory_mib = platform::query_process_resident_memory_mib();
    }

    if (const foundation::TelemetrySnapshot* snapshot = telemetry_recorder_.last()) {
        log_stream_ << foundation::describe_budget_report(*snapshot, dependencies_.runtime_budget) << '\n';
    } else {
        log_stream_ << "No telemetry snapshot was recorded by the smoke mode.\n";
    }

    return 0;
}

const foundation::RuntimeBudget& SmokeApplication::runtime_budget() const noexcept {
    return dependencies_.runtime_budget;
}

const platform::StackProbe& SmokeApplication::stack_probe() const noexcept {
    return dependencies_.stack_probe;
}

foundation::TelemetryRecorder& SmokeApplication::telemetry() noexcept {
    return telemetry_recorder_;
}

void SmokeApplication::log_startup() const {
    const foundation::BuildInfo build_info = foundation::query_build_info();

    log_stream_ << "Reaktio bootstrap\n";
    log_stream_ << "  project: " << build_info.project_name << '\n';
    log_stream_ << "  version: " << build_info.project_version << '\n';
    log_stream_ << "  configuration: " << build_info.build_configuration << '\n';
    log_stream_ << "  SDL linked version: " << dependencies_.stack_probe.linked_sdl_version << '\n';
    log_stream_ << "  SDL compiled version: " << dependencies_.stack_probe.compiled_sdl_version << '\n';
    log_stream_ << "  SDL revision: " << dependencies_.stack_probe.sdl_revision << '\n';
    log_stream_ << "  bgfx noop renderer: " << dependencies_.stack_probe.bgfx_noop_renderer_name << '\n';
}

} // namespace reaktio::app