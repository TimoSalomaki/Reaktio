#include "reaktio/app/SmokeApplication.hpp"

#include "reaktio/foundation/BuildInfo.hpp"
#include "reaktio/render/RenderSubsystem.hpp"
#include "reaktio/platform/SdlApplicationShell.hpp"

#include <SDL3/SDL_init.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>

namespace {

double milliseconds_between(
    const std::chrono::steady_clock::time_point& start,
    const std::chrono::steady_clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

} // namespace

namespace reaktio::app {

SmokeApplication::SmokeApplication(SmokeApplicationDependencies dependencies)
    : dependencies_(std::move(dependencies)) {}

int SmokeApplication::run() {
    crash_safe_log_.attach_mirror_stream(dependencies_.log_stream);

    const std::filesystem::path log_path = std::filesystem::current_path() / "logs" /
                                           dependencies_.application_config.log_file_name;
    if (!crash_safe_log_.open_file(log_path)) {
        crash_safe_log_.write(
            foundation::LogLevel::Warning,
            "Unable to open persistent runtime log file; continuing with mirror stream only.");
    }

    const foundation::BuildInfo build_info = foundation::query_build_info();
    if (!SDL_SetAppMetadata(
            std::string(build_info.project_name).c_str(),
            std::string(build_info.project_version).c_str(),
            dependencies_.application_config.app_identifier.c_str())) {
        crash_safe_log_.write(
            foundation::LogLevel::Warning,
            "SDL_SetAppMetadata failed before SDL initialization.");
    }

    log_startup(build_info);

    platform::SdlApplicationShell platform_shell{
        dependencies_.application_config,
        crash_safe_log_,
    };
    if (!platform_shell.initialize()) {
        return 1;
    }

    active_shell_ = &platform_shell;
    render::RenderSubsystem render_subsystem{
        dependencies_.application_config,
        crash_safe_log_,
    };
    if (!render_subsystem.initialize(platform_shell.window_state())) {
        active_shell_ = nullptr;
        return 1;
    }

    if (dependencies_.application_config.debug.enable_startup_diagnostics) {
        log_window_state();

        std::ostringstream render_stream;
        render_stream << "  active renderer=" << render_subsystem.stats().renderer_name
                      << " backbuffer=" << render_subsystem.stats().backbuffer_width << 'x'
                      << render_subsystem.stats().backbuffer_height << " views="
                      << render_subsystem.stats().view_count << " headless-fallback="
                      << render_subsystem.stats().using_headless_fallback;
        crash_safe_log_.write(foundation::LogLevel::Info, render_stream.str());
    }

    std::unique_ptr<gameplay::IGameMode> mode = dependencies_.game_mode_registry.create_first();
    if (!mode) {
        crash_safe_log_.write(foundation::LogLevel::Error, "No game modes are registered.");
        active_shell_ = nullptr;
        return 1;
    }

    crash_safe_log_.write(
        foundation::LogLevel::Info,
        std::string("Running mode: ") + std::string(mode->descriptor().display_name) + " (" +
            std::string(mode->descriptor().id) + ")");

    mode->on_enter(*this);

    while (!platform_shell.should_quit()) {
        platform_shell.begin_frame();
        render_subsystem.begin_frame(platform_shell.window_state());
        render_extraction_context_.begin_frame();
        const auto frame_start = std::chrono::steady_clock::now();
        const std::size_t telemetry_count_before_frame = telemetry_recorder_.size();

        platform_shell.pump_events();

        double simulation_ms = 0.0;
        while (platform_shell.frame_clock().should_run_fixed_step()) {
            const auto simulation_start = std::chrono::steady_clock::now();
            mode->on_fixed_step(*this, platform_shell.frame_timing().fixed_step_seconds);
            const auto simulation_end = std::chrono::steady_clock::now();
            simulation_ms += milliseconds_between(simulation_start, simulation_end);
            platform_shell.frame_clock().consume_fixed_step();
        }

        if (telemetry_recorder_.size() == telemetry_count_before_frame) {
            telemetry_recorder_.record(foundation::TelemetrySnapshot{});
        }

        foundation::TelemetrySnapshot* frame_snapshot = telemetry_recorder_.last_mutable();

        const auto render_extract_start = std::chrono::steady_clock::now();
        mode->on_render_extract(*this, platform_shell.frame_timing().interpolation_alpha);
        render_subsystem.submit_extracted_frame(render_extraction_context_.packets());
        render_subsystem.draw_debug_overlay(
            platform_shell.frame_timing(),
            platform_shell.input_snapshot(),
            frame_snapshot);
        render_subsystem.end_frame();
        platform_shell.present();
        const auto render_extract_end = std::chrono::steady_clock::now();

        if (frame_snapshot != nullptr) {
            frame_snapshot->frame_ms = milliseconds_between(frame_start, render_extract_end);
            frame_snapshot->simulation_ms = simulation_ms;
            frame_snapshot->render_submission_ms = milliseconds_between(render_extract_start, render_extract_end);
            frame_snapshot->resident_memory_mib = platform::query_process_resident_memory_mib();
            frame_snapshot->draw_calls = render_subsystem.stats().draw_calls;
        }

        if (dependencies_.application_config.main_loop.max_frame_count > 0 &&
            platform_shell.frame_timing().frame_index >= dependencies_.application_config.main_loop.max_frame_count) {
            platform_shell.request_quit();
        }

        if (!platform_shell.should_quit()) {
            platform_shell.sleep_until_next_fixed_step();
        }
    }

    mode->on_exit(*this);
    active_shell_ = nullptr;

    if (const foundation::TelemetrySnapshot* snapshot = telemetry_recorder_.last()) {
        crash_safe_log_.write(
            foundation::LogLevel::Info,
            foundation::describe_budget_report(*snapshot, dependencies_.runtime_budget));
    } else {
        crash_safe_log_.write(
            foundation::LogLevel::Warning,
            "No telemetry snapshot was recorded by the smoke mode.");
    }

    return 0;
}

const foundation::RuntimeBudget& SmokeApplication::runtime_budget() const noexcept {
    return dependencies_.runtime_budget;
}

const platform::StackProbe& SmokeApplication::stack_probe() const noexcept {
    return dependencies_.stack_probe;
}

const platform::InputSnapshot& SmokeApplication::input_snapshot() const noexcept {
    assert(active_shell_ != nullptr);
    return active_shell_->input_snapshot();
}

const platform::FrameTiming& SmokeApplication::frame_timing() const noexcept {
    assert(active_shell_ != nullptr);
    return active_shell_->frame_timing();
}

const platform::WindowState& SmokeApplication::window_state() const noexcept {
    assert(active_shell_ != nullptr);
    return active_shell_->window_state();
}

render::RenderExtractionContext& SmokeApplication::render_extraction() noexcept {
    return render_extraction_context_;
}

foundation::TelemetryRecorder& SmokeApplication::telemetry() noexcept {
    return telemetry_recorder_;
}

void SmokeApplication::request_quit() noexcept {
    assert(active_shell_ != nullptr);
    active_shell_->request_quit();
}

bool SmokeApplication::toggle_fullscreen() noexcept {
    assert(active_shell_ != nullptr);
    return active_shell_->toggle_fullscreen();
}

void SmokeApplication::log_startup(const foundation::BuildInfo& build_info) {
    if (!dependencies_.application_config.debug.enable_startup_diagnostics) {
        return;
    }

    crash_safe_log_.write(foundation::LogLevel::Info, "Reaktio bootstrap");
    crash_safe_log_.write(
        foundation::LogLevel::Info,
        std::string("  project: ") + std::string(build_info.project_name));
    crash_safe_log_.write(
        foundation::LogLevel::Info,
        std::string("  version: ") + std::string(build_info.project_version));
    crash_safe_log_.write(
        foundation::LogLevel::Info,
        std::string("  configuration: ") + std::string(build_info.build_configuration));
    crash_safe_log_.write(
        foundation::LogLevel::Info,
        "  SDL linked version: " + std::to_string(dependencies_.stack_probe.linked_sdl_version));
    crash_safe_log_.write(
        foundation::LogLevel::Info,
        "  SDL compiled version: " + std::to_string(dependencies_.stack_probe.compiled_sdl_version));
    crash_safe_log_.write(
        foundation::LogLevel::Info,
        std::string("  SDL revision: ") + std::string(dependencies_.stack_probe.sdl_revision));
    crash_safe_log_.write(
        foundation::LogLevel::Info,
        std::string("  bgfx noop renderer: ") + std::string(dependencies_.stack_probe.bgfx_noop_renderer_name));
    crash_safe_log_.write(
        foundation::LogLevel::Info,
        "  window mode: " + std::string(platform::to_string(dependencies_.application_config.window.mode)));
    crash_safe_log_.write(
        foundation::LogLevel::Info,
        "  renderer backend preference: " +
            std::string(platform::to_string(dependencies_.application_config.renderer_backend)));
    crash_safe_log_.write(
        foundation::LogLevel::Info,
        std::string("  vsync requested: ") +
            (dependencies_.application_config.vsync_enabled ? "true" : "false"));
    crash_safe_log_.write(
        foundation::LogLevel::Info,
        "  fixed step: " + std::to_string(dependencies_.application_config.main_loop.fixed_step_seconds) + " s");
}

void SmokeApplication::log_window_state() {
    assert(active_shell_ != nullptr);
    const platform::WindowState& state = active_shell_->window_state();

    std::ostringstream stream;
    stream << "  window id=" << state.id << " logical=" << state.logical_width << 'x'
           << state.logical_height << " pixels=" << state.pixel_width << 'x' << state.pixel_height
           << " scale=" << state.display_scale << " visible=" << state.visible
           << " focus=" << state.input_focus << " native-platform="
           << platform::to_string(state.native_handle.platform) << " native-handle=0x" << std::hex
           << state.native_handle.primary_handle;
    crash_safe_log_.write(foundation::LogLevel::Info, stream.str());
}

} // namespace reaktio::app