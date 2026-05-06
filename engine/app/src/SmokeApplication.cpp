#include "reaktio/app/ContentHotReloadController.hpp"
#include "reaktio/app/SmokeApplication.hpp"

#include "reaktio/gameplay/RailChart.hpp"
#include "reaktio/gameplay/RailObstacles.hpp"
#include "reaktio/gameplay/RailPath.hpp"
#include "reaktio/gameplay/ReplayInspection.hpp"
#include "reaktio/gameplay/ReplayPlayback.hpp"
#include "reaktio/gameplay/TypingAnalyticsExtensions.hpp"
#include "reaktio/gameplay/TypingLesson.hpp"
#include "reaktio/gameplay/TypingPrompt.hpp"

#include "reaktio/app/AuthoritativeAudioTransport.hpp"
#include "reaktio/audio/AudioClipLibrary.hpp"
#include "reaktio/content/CookedChartLibrary.hpp"
#include "reaktio/foundation/BuildInfo.hpp"
#include "reaktio/platform/InputBindingQueries.hpp"
#include "reaktio/platform/SdlAudioDevice.hpp"
#include "reaktio/render/RenderSubsystem.hpp"
#include "reaktio/platform/SdlApplicationShell.hpp"

#include <SDL3/SDL_init.h>

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace {

double milliseconds_between(
    const std::chrono::steady_clock::time_point& start,
    const std::chrono::steady_clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

float normalize_axis_value(std::int16_t value) noexcept {
    if (value >= 0) {
        return static_cast<float>(value) / static_cast<float>(std::numeric_limits<std::int16_t>::max());
    }
    return static_cast<float>(value) / -static_cast<float>(std::numeric_limits<std::int16_t>::min());
}

void rebuild_mode_input_frame(
    reaktio::gameplay::ModeInputFrame& mode_input_frame,
    const reaktio::platform::InputSnapshot& input_snapshot,
    const reaktio::gameplay::InputActionMapStore& input_action_maps) {
    mode_input_frame.clear();

    reaktio::gameplay::ActionInputSurface& actions = mode_input_frame.mutable_actions();
    for (const reaktio::gameplay::InputActionBinding& binding : input_action_maps.bindings()) {
        if (!input_action_maps.is_context_active(binding.context_id) ||
            binding.device_profile_id != input_action_maps.active_device_profile()) {
            continue;
        }

        const reaktio::platform::InputActionState state = reaktio::platform::query_binding_state(
            input_snapshot,
            binding.primary,
            binding.secondary);
        actions.set_action_state(reaktio::gameplay::InputActionState{
            .context_id = binding.context_id,
            .action_id = binding.action_id,
            .down = state.down,
            .pressed = state.pressed,
            .released = state.released,
        });
    }

    reaktio::gameplay::TextInputSurface& text = mode_input_frame.mutable_text();
    for (const reaktio::platform::TextInputEvent& event : input_snapshot.text_input_events()) {
        text.add_text_event(reaktio::gameplay::TextInputEvent{
            .timestamp_ns = event.timestamp_ns,
            .text = event.text,
        });
    }

    reaktio::gameplay::TextCompositionState composition{
        .text = input_snapshot.composition_text(),
        .start = input_snapshot.composition_start(),
        .length = input_snapshot.composition_length(),
    };
    if (!input_snapshot.text_editing_candidates_events().empty()) {
        const reaktio::platform::TextEditingCandidatesEvent& candidates =
            input_snapshot.text_editing_candidates_events().back();
        composition.candidates = candidates.candidates;
        composition.selected_candidate = candidates.selected_candidate;
        composition.candidates_horizontal = candidates.horizontal;
    }
    text.set_composition(std::move(composition));

    float pointer_delta_x = 0.0f;
    float pointer_delta_y = 0.0f;
    for (const reaktio::platform::MouseMotionEvent& event : input_snapshot.mouse_motion_events()) {
        pointer_delta_x += event.delta_x;
        pointer_delta_y += event.delta_y;
    }

    reaktio::gameplay::AnalogInputSurface& analog = mode_input_frame.mutable_analog();
    analog.set_pointer(reaktio::gameplay::PointerAnalogState{
        .x = input_snapshot.mouse_x(),
        .y = input_snapshot.mouse_y(),
        .delta_x = pointer_delta_x,
        .delta_y = pointer_delta_y,
        .wheel_x = input_snapshot.mouse_wheel_x(),
        .wheel_y = input_snapshot.mouse_wheel_y(),
        .wheel_ticks_x = input_snapshot.mouse_wheel_ticks_x(),
        .wheel_ticks_y = input_snapshot.mouse_wheel_ticks_y(),
        .button_mask = input_snapshot.mouse_button_mask(),
        .moved = !input_snapshot.mouse_motion_events().empty(),
        .wheeled = !input_snapshot.mouse_wheel_events().empty(),
    });
    analog.set_connected_gamepad_count(input_snapshot.connected_gamepads().size());
    for (const reaktio::platform::GamepadAxisEvent& event : input_snapshot.gamepad_axis_events()) {
        analog.add_axis(reaktio::gameplay::AnalogAxisState{
            .device_id = event.instance_id,
            .axis = event.axis,
            .raw_value = event.value,
            .normalized_value = normalize_axis_value(event.value),
        });
    }
}

} // namespace

namespace reaktio::app {

SmokeApplication::SmokeApplication(SmokeApplicationDependencies dependencies)
        : dependencies_(std::move(dependencies)),
            random_service_(dependencies_.random_seed) {}

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
    platform::SdlAudioDevice audio_device{dependencies_.application_config.audio};
    const bool audio_ready = audio_device.initialize();
    if (!audio_ready && dependencies_.application_config.audio.fail_if_unavailable) {
        crash_safe_log_.write(
            foundation::LogLevel::Error,
            "Failed to initialize required audio playback device: " + audio_device.info().status_message);
        active_shell_ = nullptr;
        return 1;
    }

    if (!audio_ready) {
        crash_safe_log_.write(
            foundation::LogLevel::Warning,
            "Audio playback device unavailable; continuing without audio output: " +
                audio_device.info().status_message);
    }

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
        log_audio_device(audio_device.info());
    }

    std::unique_ptr<gameplay::IGameMode> mode;
    if (!dependencies_.startup_mode_id.empty()) {
        mode = dependencies_.game_mode_registry.create_by_id(dependencies_.startup_mode_id);
        if (!mode) {
            crash_safe_log_.write(
                foundation::LogLevel::Error,
                "Requested startup mode is not registered: " + dependencies_.startup_mode_id);
            active_shell_ = nullptr;
            return 1;
        }
    } else {
        mode = dependencies_.game_mode_registry.create_first();
    }

    if (!mode) {
        crash_safe_log_.write(foundation::LogLevel::Error, "No game modes are registered.");
        active_shell_ = nullptr;
        return 1;
    }

    crash_safe_log_.write(
        foundation::LogLevel::Info,
        std::string("Running mode: ") + std::string(mode->descriptor().display_name) + " (" +
            std::string(mode->descriptor().id) + ")");

    resource_registry_.clear();
    audio::AudioClipLibrary audio_clip_library;
    if (!audio_clip_library.load(resource_registry_, crash_safe_log_)) {
        crash_safe_log_.write(foundation::LogLevel::Error, "Failed to load authoring audio clips.");
        active_shell_ = nullptr;
        return 1;
    }

    if (!render_subsystem.load_cooked_assets(resource_registry_)) {
        crash_safe_log_.write(foundation::LogLevel::Error, "Failed to load cooked render assets.");
        active_shell_ = nullptr;
        return 1;
    }

    content::CookedChartLibrary cooked_chart_library;
    if (!cooked_chart_library.load(crash_safe_log_)) {
        crash_safe_log_.write(
            foundation::LogLevel::Warning,
            "Failed to load cooked chart library at startup; continuing with hot-reload hooks only.");
    }

    ContentHotReloadController hot_reload_controller{dependencies_.hot_reload};
    if (!hot_reload_controller.initialize(crash_safe_log_)) {
        crash_safe_log_.write(
            foundation::LogLevel::Warning,
            "Content hot reload could not be initialized; continuing without development reload hooks.");
    }

    if (dependencies_.application_config.debug.enable_startup_diagnostics) {
        std::ostringstream asset_stream;
        asset_stream << "  cooked assets: textures=" << render_subsystem.stats().loaded_textures
                     << " meshes=" << render_subsystem.stats().loaded_meshes
                     << " fonts=" << render_subsystem.stats().loaded_fonts
                     << " bytes=" << render_subsystem.stats().loaded_asset_bytes
                     << " source=" << render_subsystem.stats().cooked_asset_source;
        crash_safe_log_.write(foundation::LogLevel::Info, asset_stream.str());

        const audio::AudioClipLibrarySummary& audio_summary = audio_clip_library.summary();
        std::ostringstream audio_asset_stream;
        audio_asset_stream << std::fixed << std::setprecision(2)
                           << "  authoring audio: clips=" << audio_summary.clip_count
                           << " frames=" << audio_summary.total_frames
                           << " seconds=" << audio_summary.total_duration_seconds
                           << " bytes=" << audio_summary.total_sample_bytes
                           << " source="
                           << (audio_summary.manifest_path.empty() ? std::string("<none>") : audio_summary.manifest_path.string());
        crash_safe_log_.write(foundation::LogLevel::Info, audio_asset_stream.str());

        const content::CookedChartLibrarySummary& chart_summary = cooked_chart_library.summary();
        std::ostringstream chart_stream;
        chart_stream << "  cooked charts: count=" << chart_summary.chart_count
                     << " events=" << chart_summary.total_event_count
                     << " interactive=" << chart_summary.total_interactive_cue_count
                     << " source="
                     << (chart_summary.manifest_path.empty() ? std::string("<none>") : chart_summary.manifest_path.string());
        crash_safe_log_.write(foundation::LogLevel::Info, chart_stream.str());

        if (hot_reload_controller.summary().enabled) {
            const content::HotReloadWatcherSummary& watcher_summary = hot_reload_controller.watcher_summary();
            std::ostringstream hot_reload_stream;
            hot_reload_stream << "  hot reload: rev=" << watcher_summary.revision
                              << " charts=" << watcher_summary.watched_chart_file_count
                              << " shaders=" << watcher_summary.watched_shader_file_count
                              << " materials=" << watcher_summary.watched_material_file_count
                              << " selected=" << watcher_summary.watched_selected_content_file_count
                              << " poll=" << hot_reload_controller.config().poll_interval_seconds << 's';
            crash_safe_log_.write(foundation::LogLevel::Info, hot_reload_stream.str());
        }
    }

    AuthoritativeAudioTransport transport;
    if (audio_ready) {
        if (const audio::AudioClipRecord* clip = audio_clip_library.first_clip(); clip != nullptr) {
            if (!transport.bind_audio_clip(*clip, audio_device, crash_safe_log_)) {
                crash_safe_log_.write(
                    foundation::LogLevel::Warning,
                    "Audio-authoritative transport could not bind the decoded clip; falling back to simulation transport.");
            }
        } else {
            crash_safe_log_.write(
                foundation::LogLevel::Warning,
                "Audio-authoritative transport could not start because no decoded clips were available.");
        }
    }

    active_transport_ = &transport;
    active_mode_id_ = std::string(mode->descriptor().id);

    event_bus_.reset();
    presentation_event_bus_.reset();
    mode_flow_.reset();
    mode_flow_.set_clock(0, frame_timing().frame_index);
    mode_flow_.update_modifier_flags(
        dependencies_.modifiers.view(active_mode_id_).no_fail_enabled(),
        dependencies_.modifiers.view(active_mode_id_).autoplay_enabled());
    published_flow_transition_count_ = 0;
    world_model_.reset();
    replay_recorder_.begin_session(gameplay::ReplaySessionMetadata{
        .mode_id = std::string(mode->descriptor().id),
        .mode_display_name = std::string(mode->descriptor().display_name),
        .root_random_seed = random_service_.root_seed(),
    });

    {
        gameplay::SaveDataDocument loaded{};
        std::string save_data_error;
        if (!save_data_backend_.load(loaded, &save_data_error)) {
            crash_safe_log_.write(
                foundation::LogLevel::Warning,
                "Save data load failed: " + save_data_error);
            save_data_store_.reset(gameplay::SaveDataMetadata{});
        } else {
            save_data_store_.load_document(std::move(loaded));
        }
    }

    std::uint64_t fixed_step_index = 0;
    const gameplay::ModeEnterContext enter_context{
        .reason = gameplay::ModeLifecycleReason::Startup,
        .frame_index = frame_timing().frame_index,
        .fixed_step_index = fixed_step_index,
    };

    event_bus_.publish(
        "app.smoke",
        enter_context.frame_index,
        enter_context.fixed_step_index,
        gameplay::ModeLifecycleEvent{
            .mode_id = std::string(mode->descriptor().id),
            .phase = gameplay::ModeLifecyclePhase::Entering,
            .reason = enter_context.reason,
            .api_version = mode->descriptor().api_version,
        });
    mode->on_enter(*this, enter_context);
    event_bus_.publish(
        "app.smoke",
        enter_context.frame_index,
        enter_context.fixed_step_index,
        gameplay::ModeLifecycleEvent{
            .mode_id = std::string(mode->descriptor().id),
            .phase = gameplay::ModeLifecyclePhase::Entered,
            .reason = enter_context.reason,
            .api_version = mode->descriptor().api_version,
        });

    bool render_validation_logged = false;
    while (!platform_shell.should_quit()) {
        platform_shell.begin_frame();
        hot_reload_controller.tick(
            platform_shell.frame_timing().frame_delta_seconds,
            platform_shell.frame_timing().frame_index,
            0,
            resource_registry_,
            render_subsystem,
            cooked_chart_library,
            event_bus_,
            crash_safe_log_);
        render_subsystem.begin_frame(platform_shell.window_state());
        if (!render_validation_logged && dependencies_.application_config.debug.enable_startup_diagnostics) {
            const render::RenderStats& render_stats = render_subsystem.stats();
            std::ostringstream render_validation_stream;
            render_validation_stream << "  render validation: renderer=" << render_stats.renderer_name
                                     << " backbuffer=" << render_stats.backbuffer_width << 'x'
                                     << render_stats.backbuffer_height
                                     << " headless-fallback=" << render_stats.using_headless_fallback
                                     << " post-enabled=" << render_stats.post_process_enabled
                                     << " post-passes=" << render_stats.post_process_pass_count;
            crash_safe_log_.write(foundation::LogLevel::Info, render_validation_stream.str());
            render_validation_logged = true;
        }
        render_extraction_context_.begin_frame();
        const auto frame_start = std::chrono::steady_clock::now();
        const std::size_t telemetry_count_before_frame = telemetry_recorder_.size();

        platform_shell.pump_events();
        replay_recorder_.record_input_frame(platform_shell.frame_timing(), platform_shell.input_snapshot());
        rebuild_mode_input_frame(mode_input_frame_, platform_shell.input_snapshot(), dependencies_.input_action_maps);

        mode->on_frame_begin(
            *this,
            gameplay::ModeFrameContext{
                .frame_index = platform_shell.frame_timing().frame_index,
                .fixed_step_index = fixed_step_index,
                .frame_delta_seconds = platform_shell.frame_timing().frame_delta_seconds,
                .interpolation_alpha = platform_shell.frame_timing().interpolation_alpha,
            });

        double simulation_ms = 0.0;
        while (platform_shell.frame_clock().should_run_fixed_step()) {
            const auto simulation_start = std::chrono::steady_clock::now();
            ++fixed_step_index;
            mode_flow_.set_clock(fixed_step_index, platform_shell.frame_timing().frame_index);
            transport.tick(platform_shell.frame_timing().fixed_step_seconds);
            mode->on_fixed_step(
                *this,
                gameplay::ModeFixedStepContext{
                    .frame_index = platform_shell.frame_timing().frame_index,
                    .fixed_step_index = fixed_step_index,
                    .fixed_delta_seconds = platform_shell.frame_timing().fixed_step_seconds,
                });
            const auto simulation_end = std::chrono::steady_clock::now();
            simulation_ms += milliseconds_between(simulation_start, simulation_end);
            platform_shell.frame_clock().consume_fixed_step();
        }

        transport.tick(0.0);

        if (telemetry_recorder_.size() == telemetry_count_before_frame) {
            telemetry_recorder_.record(foundation::TelemetrySnapshot{});
        }

        foundation::TelemetrySnapshot* frame_snapshot = telemetry_recorder_.last_mutable();

        const auto render_extract_start = std::chrono::steady_clock::now();
        mode->on_render_extract(
            *this,
            gameplay::ModeRenderContext{
                .frame_index = platform_shell.frame_timing().frame_index,
                .fixed_step_index = fixed_step_index,
                .interpolation_alpha = platform_shell.frame_timing().interpolation_alpha,
            });
        render_subsystem.submit_extracted_frame(render_extraction_context_.packets());
        render_subsystem.draw_debug_overlay(
            platform_shell.frame_timing(),
            platform_shell.input_snapshot(),
            frame_snapshot);
        render_subsystem.end_frame();
        platform_shell.present();
        presentation_event_bus_.clear();

        if (mode_flow_.snapshot().transition_count != published_flow_transition_count_) {
            if (const gameplay::ModeFlowTransitionRecord* transition = mode_flow_.last_transition()) {
                const gameplay::ModeFlowSnapshot& flow_snapshot = mode_flow_.snapshot();
                event_bus_.publish(
                    "app.smoke",
                    transition->frame_index,
                    transition->simulation_step,
                    gameplay::ModeFlowEvent{
                        .transition = transition->transition,
                        .from = transition->from,
                        .to = transition->to,
                        .reason = transition->reason,
                        .practice_active = flow_snapshot.flags.practice_active,
                        .no_fail_active = flow_snapshot.flags.no_fail_active,
                        .autoplay_active = flow_snapshot.flags.autoplay_active,
                    });
            }
            published_flow_transition_count_ = mode_flow_.snapshot().transition_count;
        }
        const auto render_extract_end = std::chrono::steady_clock::now();

        if (frame_snapshot != nullptr) {
            frame_snapshot->frame_ms = milliseconds_between(frame_start, render_extract_end);
            frame_snapshot->simulation_ms = simulation_ms;
            frame_snapshot->render_submission_ms = milliseconds_between(render_extract_start, render_extract_end);
            frame_snapshot->resident_memory_mib = platform::query_process_resident_memory_mib();
            frame_snapshot->draw_calls = render_subsystem.stats().draw_calls;
            frame_snapshot->audio_drift_ms = transport.diagnostics().drift_seconds * 1000.0;
        }

        if (dependencies_.application_config.main_loop.max_frame_count > 0 &&
            platform_shell.frame_timing().frame_index >= dependencies_.application_config.main_loop.max_frame_count) {
            platform_shell.request_quit();
        }

        if (!platform_shell.should_quit()) {
            platform_shell.sleep_until_next_fixed_step();
        }
    }

    transport.tick(0.0);
    const bool had_audio_authority = transport.using_audio_authority();
    const gameplay::TransportSnapshot final_transport_snapshot = transport.snapshot();
    const gameplay::TransportDiagnostics final_transport_diagnostics = transport.diagnostics();
    const platform::AudioPlaybackProgress final_playback_progress = had_audio_authority
        ? transport.playback_progress()
        : platform::AudioPlaybackProgress{};
    const TransportDrivenAudioSnapshot final_clip_snapshot = had_audio_authority
        ? transport.clip_snapshot()
        : TransportDrivenAudioSnapshot{};
    const gameplay::ModeExitContext exit_context{
        .reason = gameplay::ModeLifecycleReason::Shutdown,
        .frame_index = frame_timing().frame_index,
        .fixed_step_index = fixed_step_index,
    };

    event_bus_.publish(
        "app.smoke",
        exit_context.frame_index,
        exit_context.fixed_step_index,
        gameplay::ModeLifecycleEvent{
            .mode_id = std::string(mode->descriptor().id),
            .phase = gameplay::ModeLifecyclePhase::Exiting,
            .reason = exit_context.reason,
            .api_version = mode->descriptor().api_version,
        });
    mode->on_exit(*this, exit_context);
    event_bus_.publish(
        "app.smoke",
        exit_context.frame_index,
        exit_context.fixed_step_index,
        gameplay::ModeLifecycleEvent{
            .mode_id = std::string(mode->descriptor().id),
            .phase = gameplay::ModeLifecyclePhase::Exited,
            .reason = exit_context.reason,
            .api_version = mode->descriptor().api_version,
        });

    {
        // Phase 8/9 generic post-shutdown mode dry run. SmokeApplication has
        // no knowledge of any specific game mode here; it simply drives the
        // real IGameMode lifecycle on each injected mode using this host.
        // main.cpp constructs the modes (typing slice, rail slice, ...) and
        // feeds the scripted input frames that exercise them. The vector
        // form lets multiple slice modes plug into the same hook without
        // forcing main.cpp to grow a custom verifier per mode family.
        // Runs while shell + transport are still alive (before the cleanup
        // below nulls them out) so each mode sees the same shared engine
        // surfaces it would see during a normal run.
        std::uint64_t step_index = 0;
        std::uint64_t frame_index = frame_timing().frame_index;
        const double fixed_delta_seconds = frame_timing().fixed_step_seconds > 0.0
            ? frame_timing().fixed_step_seconds
            : 1.0 / 120.0;

        for (const auto& entry : dependencies_.post_shutdown_dry_runs) {
            if (entry.mode == nullptr) {
                continue;
            }
            gameplay::IGameMode& dry_run_mode = *entry.mode;
            const std::uint64_t baseline_save_revision =
                save_data_store_.statistics().document_revision;
            const std::uint64_t baseline_screen_effects =
                presentation_event_bus_.statistics().screen_effect_count;

            // Modes assume an Idle flow on on_enter (begin() requires Idle),
            // so the host resets mode_flow_ between dry-run entries.
            mode_flow_.reset();
            const gameplay::ModeFlowSnapshot flow_before_dry_run = mode_flow_.snapshot();

            gameplay::ModeEnterContext enter_ctx{};
            enter_ctx.reason = gameplay::ModeLifecycleReason::ModeSwitch;
            enter_ctx.frame_index = frame_index;
            dry_run_mode.on_enter(*this, enter_ctx);

            for (const auto& scripted : entry.scripted_frames) {
                mode_input_frame_.clear();
                if (!scripted.utf8_text.empty()) {
                    mode_input_frame_.mutable_text().add_text_event(
                        gameplay::TextInputEvent{
                            .timestamp_ns = 0,
                            .text = scripted.utf8_text,
                        });
                }
                if (!scripted.action_id.empty()) {
                    mode_input_frame_.mutable_actions().set_action_state(
                        gameplay::InputActionState{
                            .context_id = scripted.action_context_id,
                            .action_id = scripted.action_id,
                            .down = scripted.action_down,
                            .pressed = scripted.action_pressed,
                            .released = scripted.action_released,
                        });
                }
                gameplay::ModeFixedStepContext step_ctx{};
                step_ctx.frame_index = ++frame_index;
                step_ctx.fixed_step_index = ++step_index;
                step_ctx.fixed_delta_seconds = fixed_delta_seconds;
                dry_run_mode.on_fixed_step(*this, step_ctx);
            }

            gameplay::ModeRenderContext render_ctx{};
            render_ctx.frame_index = frame_index;
            render_ctx.fixed_step_index = step_index;
            dry_run_mode.on_render_extract(*this, render_ctx);

            gameplay::ModeExitContext exit_ctx{};
            exit_ctx.reason = gameplay::ModeLifecycleReason::Shutdown;
            exit_ctx.frame_index = frame_index;
            exit_ctx.fixed_step_index = step_index;
            dry_run_mode.on_exit(*this, exit_ctx);

            const auto save_delta =
                save_data_store_.statistics().document_revision - baseline_save_revision;
            const auto screen_effect_delta =
                presentation_event_bus_.statistics().screen_effect_count -
                baseline_screen_effects;
            const gameplay::ModeFlowSnapshot& flow_after_dry_run = mode_flow_.snapshot();
            const std::uint64_t flow_transition_delta =
                flow_after_dry_run.transition_count - flow_before_dry_run.transition_count;

            std::ostringstream slice_stream;
            slice_stream << "Post-shutdown dry run: label=" << entry.label
                         << " mode=" << dry_run_mode.descriptor().id
                         << " family=" << dry_run_mode.descriptor().family
                         << " scripted-frames=" << entry.scripted_frames.size()
                         << " save-mutations=" << save_delta
                         << " screen-fx=" << screen_effect_delta
                         << " flow-transitions=" << flow_transition_delta
                         << " flow-final=" << gameplay::to_string(flow_after_dry_run.state);
            if (entry.verifier) {
                const std::string verifier_extra = entry.verifier(dry_run_mode);
                if (!verifier_extra.empty()) {
                    slice_stream << verifier_extra;
                }
            }
            crash_safe_log_.write(foundation::LogLevel::Info, slice_stream.str());
        }
    }

    active_shell_ = nullptr;

    {
        std::ostringstream world_stream;
        world_stream << "World model: entities=" << world_model_.entity_count();
        crash_safe_log_.write(foundation::LogLevel::Info, world_stream.str());
    }

    {
        const foundation::ResourceRegistrySummary summary = resource_registry_.summary();
        std::ostringstream resource_stream;
        resource_stream << "Resource registry: resources=" << summary.resource_count << " revision=" << summary.revision
                        << " textures=" << summary.counts_by_kind[foundation::to_index(foundation::ResourceKind::Texture)]
                        << " materials=" << summary.counts_by_kind[foundation::to_index(foundation::ResourceKind::Material)]
                        << " shaders=" << summary.counts_by_kind[foundation::to_index(foundation::ResourceKind::ShaderProgram)]
                        << " meshes=" << summary.counts_by_kind[foundation::to_index(foundation::ResourceKind::Mesh)]
                        << " fonts=" << summary.counts_by_kind[foundation::to_index(foundation::ResourceKind::Font)]
                        << " audio=" << summary.counts_by_kind[foundation::to_index(foundation::ResourceKind::AudioClip)];
        crash_safe_log_.write(foundation::LogLevel::Info, resource_stream.str());
    }

    {
        std::ostringstream replay_stream;
        replay_stream << "Replay capture: inputs=" << replay_recorder_.input_frame_count() << " checkpoints="
                      << replay_recorder_.checkpoint_count();
        if (const gameplay::ReplayCheckpoint* checkpoint = replay_recorder_.last_checkpoint()) {
            replay_stream << " last=" << checkpoint->label;
        }
        crash_safe_log_.write(foundation::LogLevel::Info, replay_stream.str());
    }

    {
        const gameplay::ReplaySession session = gameplay::make_replay_session(replay_recorder_);
        const gameplay::ReplayValidator validator{};
        const gameplay::ReplayValidationReport validation_report = validator.validate(session);
        std::ostringstream validation_stream;
        validation_stream << "Replay validation: ok=" << validation_report.ok
                          << " inputs=" << validation_report.input_frame_count
                          << "/" << validation_report.total_input_frames_recorded
                          << " checkpoints=" << validation_report.checkpoint_count
                          << "/" << validation_report.total_checkpoints_recorded
                          << " input-truncated=" << validation_report.input_frames_truncated
                          << " ckpt-truncated=" << validation_report.checkpoints_truncated
                          << " mono-frame-violations=" << validation_report.monotonic_frame_violations
                          << " mono-step-violations=" << validation_report.monotonic_step_violations
                          << " duplicate-steps=" << validation_report.duplicate_step_count
                          << " issues=" << validation_report.issues.size();
        crash_safe_log_.write(
            validation_report.ok ? foundation::LogLevel::Info : foundation::LogLevel::Warning,
            validation_stream.str());

        std::vector<gameplay::ReplayObservedHash> observed;
        observed.reserve(session.checkpoints.size());
        for (const gameplay::ReplayCheckpoint& checkpoint : session.checkpoints) {
            observed.push_back(gameplay::ReplayObservedHash{
                .simulation_step = checkpoint.simulation_step,
                .state_hash = checkpoint.authoritative_state_hash,
            });
        }

        const gameplay::ReplayDivergenceReport divergence_report =
            validator.compare_observations(session, observed);
        std::ostringstream divergence_stream;
        divergence_stream << "Replay determinism check: matched=" << divergence_report.matched_count
                          << "/" << divergence_report.observed_count
                          << " mismatched=" << divergence_report.mismatched_count
                          << " missing=" << divergence_report.missing_observation_count
                          << " unexpected=" << divergence_report.unexpected_observation_count;
        crash_safe_log_.write(
            divergence_report.mismatched_count == 0 ? foundation::LogLevel::Info : foundation::LogLevel::Error,
            divergence_stream.str());

        const gameplay::ReplayInspectionView inspection_view =
            gameplay::build_replay_inspection_view(session);
        crash_safe_log_.write(
            foundation::LogLevel::Info,
            gameplay::format_replay_inspection_view(inspection_view));
    }

    {
        std::ostringstream event_stream;
        event_stream << "Event bus: messages=" << event_bus_.published_count()
                     << " retained=" << event_bus_.count();
        if (const gameplay::EventRecord* event = event_bus_.last()) {
            event_stream << " last=" << gameplay::describe_event(*event);
        }
        crash_safe_log_.write(foundation::LogLevel::Info, event_stream.str());
    }

    {
        const gameplay::PresentationEventStatistics& presentation_stats = presentation_event_bus_.statistics();
        std::ostringstream presentation_stream;
        presentation_stream << "Presentation hooks: camera=" << presentation_stats.camera_event_count
                            << " screen=" << presentation_stats.screen_effect_count
                            << " haptics=" << presentation_stats.haptics_event_count
                            << " dropped=" << presentation_stats.dropped_event_count;
        crash_safe_log_.write(foundation::LogLevel::Info, presentation_stream.str());
    }

    {
        const gameplay::ModeFlowSnapshot& flow_snapshot = mode_flow_.snapshot();
        std::ostringstream flow_stream;
        flow_stream << "Mode flow: state=" << gameplay::to_string(flow_snapshot.state)
                    << " reason=" << gameplay::to_string(flow_snapshot.last_reason)
                    << " transitions=" << flow_snapshot.transition_count
                    << " practice=" << flow_snapshot.flags.practice_active
                    << " no-fail=" << flow_snapshot.flags.no_fail_active
                    << " autoplay=" << flow_snapshot.flags.autoplay_active
                    << " results=" << flow_snapshot.results.present;
        if (flow_snapshot.results.present) {
            flow_stream << " results-label=" << flow_snapshot.results.label
                        << " score=" << flow_snapshot.results.score_summary.score
                        << " combo=" << flow_snapshot.results.score_summary.max_combo;
        }
        crash_safe_log_.write(foundation::LogLevel::Info, flow_stream.str());
    }

    {
        std::string save_error;
        const bool save_ok = save_data_backend_.save(save_data_store_.document(), &save_error);
        const gameplay::SaveDataStatistics stats = save_data_store_.statistics();
        std::ostringstream save_stream;
        save_stream << "Save data: backend=" << save_data_backend_.backend_id()
                    << " saved=" << save_ok
                    << " serialized-bytes=" << save_data_backend_.serialized().size()
                    << " saves=" << save_data_backend_.save_count()
                    << " loads=" << save_data_backend_.load_count()
                    << " revision=" << stats.document_revision
                    << " mutations=" << stats.mutation_count
                    << " rejected=" << stats.rejected_mutation_count
                    << " categories=" << stats.settings_category_count
                    << " settings=" << stats.setting_count
                    << " unlocks=" << stats.unlock_count
                    << " mode-stats=" << stats.mode_stats_count;
        crash_safe_log_.write(
            save_ok ? foundation::LogLevel::Info : foundation::LogLevel::Warning,
            save_stream.str());

        gameplay::SaveDataDocument round_trip{};
        std::string round_trip_error;
        const bool round_trip_ok =
            gameplay::parse_save_data(save_data_backend_.serialized(), round_trip, &round_trip_error) &&
            gameplay::save_data_documents_equal(round_trip, save_data_store_.document());
        std::ostringstream round_trip_stream;
        round_trip_stream << "Save data round-trip: ok=" << round_trip_ok
                          << " bytes=" << save_data_backend_.serialized().size();
        if (!round_trip_ok && !round_trip_error.empty()) {
            round_trip_stream << " error=" << round_trip_error;
        }
        crash_safe_log_.write(
            round_trip_ok ? foundation::LogLevel::Info : foundation::LogLevel::Error,
            round_trip_stream.str());
    }

    {
        // Phase 8 typing-primitive sanity check. This is a developer-time round
        // trip that exercises every TypingCursor outcome. The actual typing
        // slice mode will replace this with a real prompt-driven scenario.
        const gameplay::TypingPrompt prompt = gameplay::make_typing_prompt(
            gameplay::TypingPromptMetadata{
                .id = "smoke.typing.sanity",
                .display_name = "Smoke Typing Sanity",
                .layout_hint = "en-US",
            },
            "Hi  there\xC3\xA9!");
        gameplay::TypingCursor cursor{};
        cursor.reset(prompt);
        gameplay::TypingAnalytics analytics{};
        const gameplay::TypingLeniency lenient_policy{
            .case_insensitive = true,
            .collapse_whitespace_runs = true,
            .ignore_leading_whitespace = false,
            .ignore_trailing_whitespace = false,
            .skip_on_mismatch = false,
            .preserve_combo_on_mismatch = false,
        };
        const std::array<std::string_view, 9> typed{
            "H",            // exact
            "i",            // exact
            " ",            // matches the first whitespace
            " ",            // collapsed -> ignored-whitespace
            "T",            // case-insensitive lenient match for 't'
            "h", "e", "r",  // exact
            "X",            // mismatch (resets combo)
        };
        for (std::string_view chunk : typed) {
            const gameplay::TypingJudgementResult result = cursor.accept(chunk, lenient_policy);
            analytics.record(result);
        }
        // Final corrective character to verify combo restarts and lookups work.
        analytics.record(cursor.accept("e", lenient_policy));

        const gameplay::TypingAnalyticsSummary summary = analytics.summary();
        std::ostringstream typing_stream;
        typing_stream << std::fixed << std::setprecision(3)
                      << "Typing sanity: prompt=" << prompt.metadata.id
                      << " graphemes=" << prompt.graphemes.size()
                      << " cursor=" << cursor.cursor() << "/" << prompt.graphemes.size()
                      << " hits=" << cursor.hit_count()
                      << " misses=" << cursor.miss_count()
                      << " skips=" << cursor.skip_count()
                      << " lenient=" << cursor.lenient_match_count()
                      << " combo=" << cursor.combo() << " max-combo=" << cursor.max_combo()
                      << " ignored-ws=" << summary.total_ignored_whitespace
                      << " unique=" << summary.unique_grapheme_count
                      << " accuracy=" << summary.accuracy_ratio
                      << " last=" << gameplay::to_string(cursor.last_result().judgement);
        crash_safe_log_.write(foundation::LogLevel::Info, typing_stream.str());
    }

    {
        // Phase 8 leniency edge-case verifier. Confirms the trailing-ws
        // fast-forward actually fires (catches the dead-flag regression that
        // would silently let the option be ignored again).
        const gameplay::TypingPrompt trailing_prompt = gameplay::make_typing_prompt(
            gameplay::TypingPromptMetadata{.id = "smoke.typing.trailing-ws"},
            "hi  ");
        gameplay::TypingCursor trailing_cursor{};
        trailing_cursor.reset(trailing_prompt);
        const gameplay::TypingLeniency trailing_policy{
            .ignore_trailing_whitespace = true,
        };
        (void)trailing_cursor.accept("h", trailing_policy);
        const gameplay::TypingJudgementResult final_match =
            trailing_cursor.accept("i", trailing_policy);

        std::ostringstream trailing_stream;
        trailing_stream << "Typing trailing-ws: prompt-graphemes=" << trailing_prompt.graphemes.size()
                        << " cursor=" << trailing_cursor.cursor()
                        << " finished=" << trailing_cursor.finished()
                        << " hits=" << trailing_cursor.hit_count()
                        << " misses=" << trailing_cursor.miss_count()
                        << " skips=" << trailing_cursor.skip_count()
                        << " last=" << gameplay::to_string(final_match.judgement);
        crash_safe_log_.write(foundation::LogLevel::Info, trailing_stream.str());
    }

    {
        // Phase 8 lesson + extension analytics sanity check. Builds a minimal
        // lesson programmatically, validates it, runs a deterministic typing
        // sequence, feeds error pairs and per-grapheme press latencies into
        // the extension trackers, and submits a result to the progression.
        gameplay::TypingLesson lesson{};
        lesson.id = "smoke.lesson.basic";
        lesson.display_name = "Smoke Basic Lesson";
        lesson.locale_tag = "en-US";
        lesson.word_groups.push_back(gameplay::TypingWordGroup{
            .id = "smoke.words.greetings",
            .display_name = "Greetings",
            .layout_hint = "en-US",
            .locale_tag = "en-US",
            .entries = {"hi", "hello", "hey"},
        });
        lesson.exercises.push_back(gameplay::TypingExercise{
            .id = "smoke.exercise.greet",
            .display_name = "Greet",
            .prompt_text = "hi there",
            .leniency = gameplay::TypingLeniency{.case_insensitive = true},
            .word_group_ids = {"smoke.words.greetings"},
            .target_combo = 4,
            .target_accuracy = 0.5,
            .layout_hint = "en-US",
            .locale_tag = "en-US",
        });
        lesson.progressions.push_back(gameplay::TypingProgression{
            .id = "smoke.progression.intro",
            .display_name = "Intro",
            .steps = {
                gameplay::TypingProgressionStep{
                    .exercise_id = "smoke.exercise.greet",
                    .minimum_accuracy_to_advance = 0.5,
                    .minimum_combo_to_advance = 4,
                    .required = true,
                },
            },
        });

        const gameplay::TypingLessonValidationReport validation =
            gameplay::validate_typing_lesson(lesson);
        gameplay::TypingLessonStore store;
        const gameplay::TypingLessonStore::AddResult add_result =
            store.add_lesson(std::move(lesson));
        const gameplay::TypingLesson* stored = store.find("smoke.lesson.basic");
        const gameplay::TypingExercise& exercise = stored->exercises.front();

        gameplay::TypingPrompt prompt = gameplay::make_prompt_from_exercise(exercise);
        gameplay::TypingCursor cursor{};
        cursor.reset(prompt);
        gameplay::TypingErrorPatternTracker error_tracker;
        gameplay::TypingTimingTracker timing_tracker(gameplay::TypingTimingTrackerOptions{
            .capacity = 32,
            .histogram_bucket_count = 6,
            .histogram_bucket_microseconds = 50000,
        });

        struct ScriptedKey {
            std::string_view text;
            rhythm::TimelineMicroseconds latency_microseconds;
        };
        const std::array<ScriptedKey, 9> scripted{{
            {"h", 80000},   // match
            {"i", 90000},   // match
            {" ", 110000},  // match (space)
            {"x", 250000},  // mismatch -> 'x' for 't'
            {"t", 120000},  // match
            {"h", 100000},  // match
            {"e", 95000},   // match
            {"r", 105000},  // match
            {"e", 130000},  // match -> completes "hi there"
        }};

        std::uint64_t simulation_step = 0;
        for (const ScriptedKey& key : scripted) {
            const gameplay::TypingJudgementResult result =
                cursor.accept(key.text, exercise.leniency);
            error_tracker.record(result);
            timing_tracker.record(gameplay::TypingTimingSample{
                .simulation_step = simulation_step++,
                .frame_index = simulation_step,
                .latency_microseconds = key.latency_microseconds,
                .judgement = result.judgement,
                .advanced_cursor = result.advanced_cursor,
            });
        }

        const gameplay::TypingAnalyticsSummary cursor_view{
            .total_judgements = cursor.hit_count() + cursor.miss_count() + cursor.skip_count(),
            .total_hits = cursor.hit_count(),
            .total_misses = cursor.miss_count(),
            .total_skips = cursor.skip_count(),
            .total_lenient_hits = cursor.lenient_match_count(),
            .accuracy_ratio = (cursor.hit_count() + cursor.miss_count() + cursor.skip_count()) > 0
                ? static_cast<double>(cursor.hit_count()) /
                      static_cast<double>(cursor.hit_count() + cursor.miss_count() + cursor.skip_count())
                : 0.0,
        };
        const gameplay::TypingExerciseResult exercise_result{
            .exercise_id = exercise.id,
            .hit_count = cursor.hit_count(),
            .miss_count = cursor.miss_count(),
            .skip_count = cursor.skip_count(),
            .lenient_match_count = cursor.lenient_match_count(),
            .max_combo = cursor.max_combo(),
            .accuracy_ratio = cursor_view.accuracy_ratio,
            .prompt_completed = cursor.finished(),
        };
        gameplay::TypingProgressionTracker progression_tracker;
        progression_tracker.reset(&stored->progressions.front());
        const gameplay::TypingProgressionTracker::StepOutcome progression_outcome =
            progression_tracker.submit_result(exercise_result);

        const gameplay::TypingErrorPatternSummary errors = error_tracker.summarize();
        const gameplay::TypingTimingSummary timing = timing_tracker.summarize();
        const gameplay::TypingTimingHistogram histogram = timing_tracker.build_histogram();

        std::ostringstream lesson_stream;
        lesson_stream << "Typing lesson: id=" << "smoke.lesson.basic"
                      << " validation-ok=" << validation.ok
                      << " add=" << static_cast<int>(add_result)
                      << " store=" << store.lesson_count()
                      << " exercise=" << exercise.id
                      << " prompt-graphemes=" << prompt.graphemes.size()
                      << " hits=" << cursor.hit_count()
                      << " misses=" << cursor.miss_count()
                      << " max-combo=" << cursor.max_combo()
                      << " completed=" << cursor.finished()
                      << " progression-outcome=" << static_cast<int>(progression_outcome);
        crash_safe_log_.write(foundation::LogLevel::Info, lesson_stream.str());

        std::ostringstream errors_stream;
        errors_stream << "Typing error patterns: mismatches=" << errors.total_mismatches
                      << " skips=" << errors.total_skips
                      << " unique=" << error_tracker.unique_pair_count()
                      << " top=";
        for (const gameplay::TypingErrorPatternEntry& entry : errors.top_pairs) {
            errors_stream << "[" << entry.pair.expected << "->" << entry.pair.observed
                          << " x" << entry.count << "]";
        }
        crash_safe_log_.write(foundation::LogLevel::Info, errors_stream.str());

        std::ostringstream timing_stream;
        timing_stream << "Typing timing: samples=" << timing.sample_count
                      << " min-us=" << timing.min_latency_microseconds
                      << " mean-us=" << timing.mean_latency_microseconds
                      << " median-us=" << timing.median_latency_microseconds
                      << " max-us=" << timing.max_latency_microseconds
                      << " buckets[" << histogram.first_bucket_min_microseconds / 1000 << "..."
                      << histogram.last_bucket_max_microseconds / 1000 << "ms,bucket="
                      << histogram.bucket_microseconds / 1000 << "ms]=";
        timing_stream << "<" << histogram.below_range_count;
        for (std::uint32_t count : histogram.bucket_counts) {
            timing_stream << " " << count;
        }
        timing_stream << " >" << histogram.above_range_count;
        crash_safe_log_.write(foundation::LogLevel::Info, timing_stream.str());
    }

    {
        // Phase 9 rail-primitive sanity check. Builds a deterministic L-shaped
        // rail (straight 10m, right turn, straight 10m), validates frame
        // construction across the corner, drives a CueScheduler against a
        // tempo-mapped chart, and confirms the RailChart adapter resolves
        // cue positions that move toward the judge line as the simulation
        // tick advances. This proves the lane/rail family modes can reuse
        // the existing scheduler+scoring contracts before the rail slice
        // mode lands in a later turn.
        gameplay::RailPath rail_path{};
        const bool rail_built = rail_path.rebuild({
            gameplay::RailPathControlPoint{.position = {0.0f, 0.0f, 0.0f}},
            gameplay::RailPathControlPoint{.position = {0.0f, 0.0f, 5.0f}},
            gameplay::RailPathControlPoint{.position = {0.0f, 0.0f, 10.0f}},
            gameplay::RailPathControlPoint{.position = {5.0f, 0.0f, 10.0f}},
            gameplay::RailPathControlPoint{.position = {10.0f, 0.0f, 10.0f}},
        });
        const gameplay::RailPathSample sample_start = rail_path.sample_at_arc_length(0.0);
        const gameplay::RailPathSample sample_mid = rail_path.sample_at_arc_length(10.0);
        const gameplay::RailPathSample sample_end = rail_path.sample_at_arc_length(rail_path.total_length());
        const gameplay::RailPathSample sample_alpha = rail_path.sample_at_alpha(0.5);
        const double wrap_negative = rail_path.wrap_arc_length(-3.0);
        const double wrap_positive = rail_path.wrap_arc_length(rail_path.total_length() + 7.5);

        std::ostringstream rail_stream;
        rail_stream << std::fixed << std::setprecision(3)
                    << "Rail path: built=" << rail_built
                    << " segments=" << rail_path.segment_count()
                    << " total=" << rail_path.total_length()
                    << " start-pos=(" << sample_start.position.x << "," << sample_start.position.y
                    << "," << sample_start.position.z << ")"
                    << " corner-pos=(" << sample_mid.position.x << "," << sample_mid.position.y
                    << "," << sample_mid.position.z << ")"
                    << " end-pos=(" << sample_end.position.x << "," << sample_end.position.y
                    << "," << sample_end.position.z << ")"
                    << " alpha-mid-arc=" << sample_alpha.arc_length
                    << " wrap[-3.0]=" << wrap_negative
                    << " wrap[+L+7.5]=" << wrap_positive;
        crash_safe_log_.write(foundation::LogLevel::Info, rail_stream.str());

        // Drive a four-channel chart at 120 BPM through the shared
        // CueScheduler + RailChart adapter, advancing the transport in
        // discrete ticks and recording how cues flow toward the judge line.
        rhythm::TempoMap rail_tempo{};
        rhythm::TempoMapDefinition rail_tempo_definition{};
        rail_tempo_definition.config.ticks_per_quarter_note = 480;
        rail_tempo_definition.config.sample_rate_hz = 48000;
        rail_tempo_definition.tempo_changes.push_back(
            rhythm::TempoChange{.start_tick = 0, .microseconds_per_quarter_note = 500000});
        rail_tempo_definition.time_signature_changes.push_back(
            rhythm::TimeSignatureChange{.start_tick = 0, .numerator = 4, .denominator = 4});
        rail_tempo.rebuild(std::move(rail_tempo_definition));

        const std::array<rhythm::ScheduledCue, 4> rail_schedule{{
            {.hit_tick = 1920, .channel_index = 0},
            {.hit_tick = 2400, .channel_index = 1},
            {.hit_tick = 2880, .channel_index = 2},
            {.hit_tick = 3360, .channel_index = 3},
        }};

        gameplay::RailChartConfig rail_chart_config{};
        rail_chart_config.lane_layout = gameplay::RailLaneLayout{
            .lane_count = 5,
            .lane_spacing = 1.5,
            .vertical_offset = 0.0,
        };
        rail_chart_config.arc_length_per_tick = 0.005;
        rail_chart_config.travel_lead_ticks = 1920;
        rail_chart_config.judge_arc_length = rail_path.total_length();

        const gameplay::RailChart rail_chart =
            gameplay::make_rail_chart(rail_schedule, rail_chart_config);

        gameplay::CueScheduler rail_scheduler{};
        std::vector<gameplay::SpatialCueSample> spatial_samples;

        const auto step_scheduler = [&](rhythm::ChartTick tick) {
            gameplay::TransportSnapshot snapshot{};
            snapshot.playback_state = gameplay::TransportPlaybackState::Playing;
            snapshot.position_seconds =
                static_cast<double>(rail_tempo.microseconds_from_tick(tick)) / 1'000'000.0;
            gameplay::CueSchedulerUpdateInput input{};
            input.tempo_map = &rail_tempo;
            input.transport = &snapshot;
            input.schedule = std::span<const rhythm::ScheduledCue>(rail_schedule);
            input.rules = gameplay::CueSchedulerRules{};
            rail_scheduler.update(input);
            gameplay::resolve_spatial_cues(
                rail_chart, rail_path, tick, rail_scheduler.active_cues(), spatial_samples);
        };

        step_scheduler(0);
        const std::size_t active_at_t0 = rail_scheduler.summary().active_cue_count;
        step_scheduler(1920);
        const std::size_t active_at_first_judge = rail_scheduler.summary().active_cue_count;
        const double first_distance =
            spatial_samples.empty() ? 0.0 : spatial_samples.front().distance_to_judge;
        step_scheduler(2880);
        const std::size_t active_at_third_judge = rail_scheduler.summary().active_cue_count;
        const double third_distance =
            spatial_samples.empty() ? 0.0 : spatial_samples.front().distance_to_judge;

        std::ostringstream chart_stream;
        chart_stream << std::fixed << std::setprecision(3)
                     << "Rail chart: cues=" << rail_chart.cues.size()
                     << " judge-arc=" << rail_chart_config.judge_arc_length
                     << " active@t0=" << active_at_t0
                     << " active@first-judge=" << active_at_first_judge
                     << " first-cue-dist@first-judge=" << first_distance
                     << " active@third-judge=" << active_at_third_judge
                     << " first-cue-dist@third-judge=" << third_distance
                     << " spatial-samples=" << spatial_samples.size();
        crash_safe_log_.write(foundation::LogLevel::Info, chart_stream.str());

        // Camera rig + parallax sanity. Anchor a rig at the corner and
        // confirm both the eye and target move along the rail correctly
        // when the look-at arc length advances. Resolve a 3-layer parallax
        // stack and confirm the speed scalars produce monotonic offsets.
        gameplay::RailCameraRig rail_rig{};
        rail_rig.look_at_arc_length = 12.0;
        rail_rig.follow_distance = 4.0;
        rail_rig.lateral_offset = 0.0;
        rail_rig.vertical_offset = 1.5;
        const gameplay::RailCameraSample camera = gameplay::sample_rail_camera(rail_path, rail_rig);

        gameplay::ParallaxLayerStack parallax{};
        parallax.layers = {
            gameplay::ParallaxLayer{.speed_scalar = 0.25, .base_offset = 0.0},
            gameplay::ParallaxLayer{.speed_scalar = 0.50, .base_offset = 0.0},
            gameplay::ParallaxLayer{.speed_scalar = 1.00, .base_offset = 0.0},
        };
        std::vector<gameplay::ParallaxLayerSample> parallax_samples;
        gameplay::sample_parallax_stack(parallax, 8.0, parallax_samples);

        std::ostringstream camera_stream;
        camera_stream << std::fixed << std::setprecision(3)
                      << "Rail camera: eye=(" << camera.eye.x << "," << camera.eye.y << ","
                      << camera.eye.z << ")"
                      << " target=(" << camera.target.x << "," << camera.target.y << ","
                      << camera.target.z << ")"
                      << " up=(" << camera.up.x << "," << camera.up.y << "," << camera.up.z << ")"
                      << " parallax-layers=" << parallax_samples.size();
        if (!parallax_samples.empty()) {
            camera_stream << " parallax-offsets=";
            for (std::size_t i = 0; i < parallax_samples.size(); ++i) {
                if (i > 0) {
                    camera_stream << ",";
                }
                camera_stream << parallax_samples[i].offset;
            }
        }
        crash_safe_log_.write(foundation::LogLevel::Info, camera_stream.str());
    }

    {
        // Phase 9 rail-obstacle/hit-scan/projectile/env-trigger sanity check.
        // Builds a small deterministic obstacle field on the same arc-length
        // axis as the rail above, then exercises every interaction primitive
        // with closed-form expected results: an overlap query at the player
        // position, a hit-scan that should pierce one shootable but stop on
        // the solid behind it, a projectile sweep that should consume one
        // shootable, and an env-trigger advance that should fire two of three
        // triggers.
        gameplay::RailObstacleField obstacle_field{};
        obstacle_field.rebuild({
            // Hazard at arc 5, lanes [-1..+1], half-extent 1m.
            gameplay::RailObstacle{
                .obstacle_id = 100,
                .arc_length = 5.0,
                .arc_length_half_extent = 1.0,
                .signed_lane_min = -1,
                .signed_lane_max = 1,
                .flags = static_cast<std::uint32_t>(gameplay::RailObstacleFlag::Hazard) |
                         gameplay::RailObstacleFlag::Solid,
            },
            // Shootable target at arc 12, lane 0 only.
            gameplay::RailObstacle{
                .obstacle_id = 101,
                .arc_length = 12.0,
                .arc_length_half_extent = 0.5,
                .signed_lane_min = 0,
                .signed_lane_max = 0,
                .flags = static_cast<std::uint32_t>(gameplay::RailObstacleFlag::Shootable),
            },
            // Solid wall at arc 16, lane 0, blocks pierce.
            gameplay::RailObstacle{
                .obstacle_id = 102,
                .arc_length = 16.0,
                .arc_length_half_extent = 0.75,
                .signed_lane_min = 0,
                .signed_lane_max = 0,
                .flags = static_cast<std::uint32_t>(gameplay::RailObstacleFlag::Solid) |
                         gameplay::RailObstacleFlag::Shootable,
            },
            // Pickup off to the right side at arc 20, lane +2.
            gameplay::RailObstacle{
                .obstacle_id = 103,
                .arc_length = 20.0,
                .arc_length_half_extent = 0.5,
                .signed_lane_min = 2,
                .signed_lane_max = 2,
                .flags = static_cast<std::uint32_t>(gameplay::RailObstacleFlag::Pickup),
            },
        });

        // Player overlap query at arc 5.5 lane 0 — should hit the hazard at
        // arc 5 (extent 1m -> covers [4..6]) but nothing else.
        std::vector<std::size_t> overlap_at_5p5;
        obstacle_field.query_overlap_point(5.5, 0, overlap_at_5p5);
        // And at arc 5.5 lane +3 — out of any obstacle's lane range.
        std::vector<std::size_t> overlap_at_5p5_lane3;
        obstacle_field.query_overlap_point(5.5, 3, overlap_at_5p5_lane3);

        // Hit-scan from arc 8 lane 0, range 20m, pierce 2: should hit the
        // shootable at arc 12 (distance 3.5 from leading edge) and then be
        // stopped by the solid wall at arc 16 even though pierce was set.
        gameplay::RailHitScanProbe probe{};
        probe.origin_arc_length = 8.0;
        probe.origin_signed_lane = 0;
        probe.max_distance = 20.0;
        probe.pierce_count = 2;
        probe.required_flag_mask =
            static_cast<std::uint32_t>(gameplay::RailObstacleFlag::Shootable);
        std::vector<gameplay::RailHitScanHit> hit_scan_hits;
        gameplay::resolve_hit_scan(obstacle_field, probe, hit_scan_hits);

        // Projectile sweep: one projectile starting at arc 9, lane 0,
        // travelling at 60 m/s for one fixed step of 1/120s = 0.5m. Should
        // not yet reach the shootable at arc 12. Bump speed and step again.
        std::vector<gameplay::RailProjectile> projectiles{
            gameplay::RailProjectile{
                .projectile_id = 9000,
                .arc_length = 9.0,
                .signed_lane = 0,
                .speed = 6000.0,             // Force a wide sweep this step.
                .remaining_lifetime = 1.0,
                .pierce_remaining = 0,
                .damage = 1,
                .active = true,
            },
        };
        std::vector<gameplay::RailProjectileHit> projectile_hits;
        gameplay::advance_projectiles(projectiles, obstacle_field, 1.0 / 120.0, projectile_hits);

        // Env triggers: three triggers at arcs 4, 10, 18. Advancing the
        // player to arc 12 should fire the first two; advancing to arc 25
        // should fire the third.
        gameplay::RailEnvTriggerStream env_stream{};
        env_stream.rebuild({
            gameplay::RailEnvTrigger{.trigger_id = 1, .arc_length = 4.0, .kind_tag = 1},
            gameplay::RailEnvTrigger{.trigger_id = 2, .arc_length = 10.0, .kind_tag = 2},
            gameplay::RailEnvTrigger{.trigger_id = 3, .arc_length = 18.0, .kind_tag = 1},
        });
        std::vector<gameplay::RailEnvTrigger> fired_first_pass;
        env_stream.advance_to_arc_length(12.0, fired_first_pass);
        const std::size_t fired_after_first = env_stream.fired_count();
        std::vector<gameplay::RailEnvTrigger> fired_second_pass;
        env_stream.advance_to_arc_length(25.0, fired_second_pass);

        std::ostringstream obstacle_stream;
        obstacle_stream << std::fixed << std::setprecision(3)
                        << "Rail obstacles: count=" << obstacle_field.size()
                        << " overlap@5.5/lane0=" << overlap_at_5p5.size()
                        << " overlap@5.5/lane3=" << overlap_at_5p5_lane3.size()
                        << " hit-scan=" << hit_scan_hits.size();
        if (!hit_scan_hits.empty()) {
            obstacle_stream << " first-hit-id=" << hit_scan_hits.front().obstacle_id
                            << " first-hit-dist=" << hit_scan_hits.front().distance;
        }
        if (hit_scan_hits.size() >= 2) {
            obstacle_stream << " second-hit-id=" << hit_scan_hits[1].obstacle_id;
        }
        obstacle_stream << " projectile-hits=" << projectile_hits.size();
        if (!projectile_hits.empty()) {
            obstacle_stream << " proj-first-id=" << projectile_hits.front().obstacle_id;
        }
        obstacle_stream << " env-fired-pass1=" << fired_first_pass.size()
                        << " env-cursor-after-pass1=" << fired_after_first
                        << " env-fired-pass2=" << fired_second_pass.size()
                        << " env-cursor-final=" << env_stream.fired_count();
        crash_safe_log_.write(foundation::LogLevel::Info, obstacle_stream.str());
    }

    {
        // Phase 9 stress test for the rail-family engine primitives. Builds
        // a large obstacle field (1024 obstacles fanned across 5 lanes), a
        // burst of projectiles, and a long ordered env-trigger stream, then
        // drives a fixed number of simulation steps and reports timing +
        // counts. Verifies the shared engine stack handles dense patterns
        // and rapid camera/state transitions without correctness regressions.
        constexpr std::size_t k_stress_obstacles = 1024;
        constexpr std::size_t k_stress_projectiles = 64;
        constexpr std::size_t k_stress_env_triggers = 256;
        constexpr std::size_t k_stress_steps = 1000;
        constexpr double k_stress_player_velocity = 24.0;        // m/s.
        constexpr double k_stress_fixed_delta = 1.0 / 240.0;     // 240 Hz sim.
        constexpr double k_stress_arc_per_obstacle = 0.5;        // 1024 * 0.5 = 512m.

        std::vector<gameplay::RailObstacle> stress_obstacles;
        stress_obstacles.reserve(k_stress_obstacles);
        for (std::size_t i = 0; i < k_stress_obstacles; ++i) {
            const std::int32_t lane = static_cast<std::int32_t>(i % 5u) - 2;
            const std::uint32_t flags = (i % 4u == 0u)
                ? static_cast<std::uint32_t>(gameplay::RailObstacleFlag::Hazard) |
                      gameplay::RailObstacleFlag::Solid
                : (i % 4u == 1u)
                      ? static_cast<std::uint32_t>(gameplay::RailObstacleFlag::Pickup)
                      : static_cast<std::uint32_t>(gameplay::RailObstacleFlag::Shootable);
            stress_obstacles.push_back(gameplay::RailObstacle{
                .obstacle_id = static_cast<std::uint64_t>(0x10000u + i),
                .arc_length = 1.0 + static_cast<double>(i) * k_stress_arc_per_obstacle,
                .arc_length_half_extent = 0.20,
                .signed_lane_min = lane,
                .signed_lane_max = lane,
                .flags = flags,
                .hit_points = 1u,
            });
        }
        gameplay::RailObstacleField stress_field{};
        stress_field.rebuild(std::move(stress_obstacles));

        std::vector<gameplay::RailProjectile> stress_projectiles;
        stress_projectiles.reserve(k_stress_projectiles);
        for (std::size_t i = 0; i < k_stress_projectiles; ++i) {
            stress_projectiles.push_back(gameplay::RailProjectile{
                .projectile_id = static_cast<std::uint64_t>(0x20000u + i),
                .arc_length = 1.0,
                .signed_lane = static_cast<std::int32_t>(i % 5u) - 2,
                .speed = 80.0,
                .remaining_lifetime = 5.0,
                .pierce_remaining = 1u,
                .damage = 1u,
                .active = true,
            });
        }

        std::vector<gameplay::RailEnvTrigger> stress_triggers;
        stress_triggers.reserve(k_stress_env_triggers);
        for (std::size_t i = 0; i < k_stress_env_triggers; ++i) {
            stress_triggers.push_back(gameplay::RailEnvTrigger{
                .trigger_id = static_cast<std::uint64_t>(0x30000u + i),
                .arc_length = 2.0 + static_cast<double>(i) * 2.0,
                .kind_tag = static_cast<std::uint32_t>(i % 3u),
            });
        }
        gameplay::RailEnvTriggerStream stress_stream{};
        stress_stream.rebuild(std::move(stress_triggers));

        std::vector<gameplay::RailProjectileHit> projectile_hit_buffer;
        std::vector<std::size_t> overlap_buffer;
        std::vector<gameplay::RailEnvTrigger> trigger_buffer;
        std::uint64_t total_projectile_hits = 0;
        std::uint64_t total_obstacles_destroyed = 0;
        std::uint64_t total_overlap_returns = 0;
        std::uint64_t total_triggers_fired = 0;
        std::uint64_t total_alive_projectiles = 0;
        double player_arc = 0.0;

        const auto stress_start = std::chrono::steady_clock::now();
        for (std::size_t step = 0; step < k_stress_steps; ++step) {
            player_arc += k_stress_player_velocity * k_stress_fixed_delta;

            projectile_hit_buffer.clear();
            gameplay::advance_projectiles(
                stress_projectiles, stress_field, k_stress_fixed_delta, projectile_hit_buffer);
            total_projectile_hits += projectile_hit_buffer.size();
            // Simulate consumption of shootable hits through the field's
            // HP API so the field state evolves through the run, not a
            // frozen layout. register_hit returns true only on the
            // alive->destroyed transition; total_obstacles_destroyed
            // therefore counts unique kills, not chip damage.
            for (const gameplay::RailProjectileHit& hit : projectile_hit_buffer) {
                if (stress_field.register_hit(hit.obstacle_index, hit.damage)) {
                    ++total_obstacles_destroyed;
                }
            }
            // Sweep the player's lane for overlap (any of the 5 lanes,
            // rotating each step to model rapid lane swaps).
            const std::int32_t player_lane = static_cast<std::int32_t>(step % 5u) - 2;
            overlap_buffer.clear();
            stress_field.query_overlap_point(player_arc, player_lane, overlap_buffer);
            total_overlap_returns += overlap_buffer.size();

            trigger_buffer.clear();
            stress_stream.advance_to_arc_length(player_arc, trigger_buffer);
            total_triggers_fired += trigger_buffer.size();

            for (const gameplay::RailProjectile& projectile : stress_projectiles) {
                if (projectile.active) {
                    ++total_alive_projectiles;
                }
            }
        }
        const auto stress_end = std::chrono::steady_clock::now();
        const auto stress_duration_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(stress_end - stress_start).count();
        const double stress_duration_ms = static_cast<double>(stress_duration_ns) / 1'000'000.0;
        const double stress_per_step_us =
            (static_cast<double>(stress_duration_ns) / static_cast<double>(k_stress_steps)) / 1000.0;

        std::ostringstream stress_stream_log;
        stress_stream_log << std::fixed << std::setprecision(3)
                          << "Rail stress: obstacles=" << k_stress_obstacles
                          << " projectiles=" << k_stress_projectiles
                          << " triggers=" << k_stress_env_triggers
                          << " steps=" << k_stress_steps
                          << " duration-ms=" << stress_duration_ms
                          << " per-step-us=" << stress_per_step_us
                          << " projectile-hits=" << total_projectile_hits
                          << " obstacles-destroyed=" << total_obstacles_destroyed
                          << " overlap-returns=" << total_overlap_returns
                          << " triggers-fired=" << total_triggers_fired
                          << " final-cursor=" << stress_stream.fired_count()
                          << " alive-projectile-samples=" << total_alive_projectiles;
        crash_safe_log_.write(foundation::LogLevel::Info, stress_stream_log.str());
    }

    if (hot_reload_controller.summary().enabled) {
        std::ostringstream hot_reload_stream;
        hot_reload_stream << "Hot reload summary: applied=" << hot_reload_controller.summary().applied_reload_count
                          << " pending=" << hot_reload_controller.summary().pending_reload_count
                          << " failed=" << hot_reload_controller.summary().failed_reload_count
                          << " rev=" << hot_reload_controller.summary().revision;
        crash_safe_log_.write(foundation::LogLevel::Info, hot_reload_stream.str());
    }

    if (had_audio_authority) {
        std::ostringstream timing_stream;
        timing_stream << std::fixed << std::setprecision(3)
                      << "Audio authoritative transport: transport=" << final_transport_snapshot.position_seconds
                      << "s stream=" << final_playback_progress.stream_consumed_seconds
                      << "s reported-output=" << final_playback_progress.authoritative_position_seconds
                      << "s sim=" << final_transport_diagnostics.simulation_position_seconds
                      << "s mode=" << platform::to_string(final_playback_progress.authoritative_position_mode)
                      << " queued=" << final_playback_progress.queued_input_seconds
                      << "s latency=" << final_playback_progress.total_output_latency_seconds
                      << "s clip=" << final_clip_snapshot.rendered_input_frames << '/' << final_clip_snapshot.total_frames
                      << " drift-ms=" << (final_transport_diagnostics.drift_seconds * 1000.0)
                      << " corrections=" << final_transport_diagnostics.correction_count;
        if (final_transport_diagnostics.recent_correction_count > 0u) {
            const gameplay::TransportCorrectionEvent& last_correction = final_transport_diagnostics.recent_corrections[0];
            timing_stream << " last=" << gameplay::to_string(last_correction.correction_type)
                          << '@' << last_correction.authoritative_position_seconds
                          << "s apply-ms=" << (last_correction.correction_applied_seconds * 1000.0);
        }
        crash_safe_log_.write(foundation::LogLevel::Info, timing_stream.str());
    }

    transport.unbind_audio_clip();
    active_transport_ = nullptr;
    active_mode_id_.clear();

    world_model_.reset();
    resource_registry_.clear();

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

const gameplay::ModeInputFrame& SmokeApplication::input() const noexcept {
    return mode_input_frame_;
}

const gameplay::InputActionMapStore& SmokeApplication::input_action_maps() const noexcept {
    return dependencies_.input_action_maps;
}

gameplay::InputActionMapStore& SmokeApplication::input_action_maps() noexcept {
    return dependencies_.input_action_maps;
}

const platform::InputSnapshot& SmokeApplication::input_snapshot() const noexcept {
    assert(active_shell_ != nullptr);
    return active_shell_->input_snapshot();
}

const platform::InputBindingsConfig& SmokeApplication::input_bindings() const noexcept {
    return dependencies_.input_bindings;
}

const platform::FrameTiming& SmokeApplication::frame_timing() const noexcept {
    assert(active_shell_ != nullptr);
    return active_shell_->frame_timing();
}

const platform::WindowState& SmokeApplication::window_state() const noexcept {
    assert(active_shell_ != nullptr);
    return active_shell_->window_state();
}

foundation::DeterministicRandomService& SmokeApplication::random_service() noexcept {
    return random_service_;
}

foundation::ResourceRegistry& SmokeApplication::resource_registry() noexcept {
    return resource_registry_;
}

const gameplay::ModeConfigurationStore& SmokeApplication::mode_configuration() const noexcept {
    return dependencies_.mode_configuration;
}

const gameplay::ModifierStore& SmokeApplication::modifier_store() const noexcept {
    return dependencies_.modifiers;
}

const gameplay::ModifierSet& SmokeApplication::modifiers() const noexcept {
    return dependencies_.modifiers.view(active_mode_id_);
}

gameplay::EventBus& SmokeApplication::event_bus() noexcept {
    return event_bus_;
}

gameplay::PresentationEventBus& SmokeApplication::presentation_events() noexcept {
    return presentation_event_bus_;
}

gameplay::ModeFlowController& SmokeApplication::flow() noexcept {
    return mode_flow_;
}

gameplay::ReplayRecorder& SmokeApplication::replay() noexcept {
    return replay_recorder_;
}

gameplay::SaveDataStore& SmokeApplication::save_data() noexcept {
    return save_data_store_;
}

gameplay::WorldModel& SmokeApplication::world_model() noexcept {
    return world_model_;
}

gameplay::ITransportControl& SmokeApplication::transport() noexcept {
    assert(active_transport_ != nullptr);
    return *active_transport_;
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
    {
        std::ostringstream audio_stream;
        audio_stream << "  audio request: enabled=" << dependencies_.application_config.audio.enable_playback_device
                     << " required=" << dependencies_.application_config.audio.fail_if_unavailable
                     << " rate=" << dependencies_.application_config.audio.preferred_sample_rate
                     << "Hz channels=" << dependencies_.application_config.audio.preferred_channels
                     << " frames=" << dependencies_.application_config.audio.preferred_buffer_frames
                     << " format=" << platform::to_string(dependencies_.application_config.audio.preferred_format)
                     << " start-paused=" << dependencies_.application_config.audio.start_paused
                     << " gain=" << dependencies_.application_config.audio.device_gain;
        crash_safe_log_.write(foundation::LogLevel::Info, audio_stream.str());
    }
    {
        std::ostringstream seed_stream;
        seed_stream << "  random seed: 0x" << std::hex << dependencies_.random_seed;
        crash_safe_log_.write(foundation::LogLevel::Info, seed_stream.str());
    }
    crash_safe_log_.write(
        foundation::LogLevel::Info,
        "  config source: " + dependencies_.configuration_source);
    {
        const gameplay::InputActionMapSummary action_map_summary = dependencies_.input_action_maps.summary();
        std::ostringstream input_stream;
        input_stream << "  input bindings: actions=" << dependencies_.input_bindings.action_count()
                     << " bindings=" << dependencies_.input_bindings.binding_count()
                     << " maps=" << action_map_summary.binding_count
                     << " contexts=" << action_map_summary.active_context_count << '/' << action_map_summary.context_count
                     << " profile=" << dependencies_.input_action_maps.active_device_profile();
        crash_safe_log_.write(foundation::LogLevel::Info, input_stream.str());
    }
    {
        const gameplay::ModeConfigurationSummary mode_summary = dependencies_.mode_configuration.summary();
        std::ostringstream mode_stream;
        mode_stream << "  mode config: modes=" << mode_summary.mode_count
                    << " entries=" << mode_summary.entry_count;
        crash_safe_log_.write(foundation::LogLevel::Info, mode_stream.str());
    }
    {
        const gameplay::ModifierStoreSummary modifier_summary = dependencies_.modifiers.summary();
        std::ostringstream modifier_stream;
        modifier_stream << "  modifiers: modes=" << modifier_summary.mode_count
                        << " entries=" << modifier_summary.entry_count
                        << " enabled=" << modifier_summary.enabled_entry_count;
        crash_safe_log_.write(foundation::LogLevel::Info, modifier_stream.str());
    }
    if (!dependencies_.startup_mode_id.empty()) {
        crash_safe_log_.write(
            foundation::LogLevel::Info,
            "  startup mode: " + dependencies_.startup_mode_id);
    }
    {
        std::ostringstream registry_stream;
        registry_stream << "  mode registry: api=" << gameplay::k_current_mode_api_version
                        << " modes=" << dependencies_.game_mode_registry.registered_modes().size()
                        << " extension-policy=source-level-v1";
        crash_safe_log_.write(foundation::LogLevel::Info, registry_stream.str());
    }
}

void SmokeApplication::log_audio_device(const platform::AudioDeviceInfo& info) {
    if (!dependencies_.application_config.debug.enable_startup_diagnostics) {
        return;
    }

    std::ostringstream stream;
    stream << "  audio device: state=" << platform::to_string(info.state)
           << " driver=" << info.driver_name
           << " name=" << info.device_name
           << " id=" << info.logical_device_id
           << " paused=" << info.paused
           << " requested=" << info.requested_spec.sample_rate_hz << "Hz/" << info.requested_spec.channels
           << "ch/" << platform::to_string(info.requested_spec.format)
           << " actual=" << info.actual_spec.sample_rate_hz << "Hz/" << info.actual_spec.channels
           << "ch/" << platform::to_string(info.actual_spec.format)
           << " frames=" << info.latency.device_buffer_frames
           << " latency=" << info.latency.total_output_latency_ms << "ms"
           << " latency-source=" << platform::to_string(info.latency.query_mode)
           << " gain=" << info.gain
           << " status=" << info.status_message;
    crash_safe_log_.write(foundation::LogLevel::Info, stream.str());
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