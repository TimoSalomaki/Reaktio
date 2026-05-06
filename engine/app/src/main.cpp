#include "reaktio/app/RuntimeConfiguration.hpp"
#include "reaktio/app/SmokeApplication.hpp"

#include "reaktio/games/rail_slice/RailSliceMode.hpp"
#include "reaktio/games/reference/ReferenceSandboxMode.hpp"
#include "reaktio/games/templates/StarterMode.hpp"
#include "reaktio/games/typing_slice/TypingSliceMode.hpp"
#include "reaktio/platform/StackProbe.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <utility>

int main() {
    reaktio::gameplay::GameModeRegistry game_mode_registry;
    if (!game_mode_registry.register_mode<reaktio::games::reference::ReferenceSandboxMode>()) {
        return 1;
    }
    if (!game_mode_registry.register_mode<reaktio::games::templates::StarterMode>()) {
        return 1;
    }
    if (!game_mode_registry.register_mode<reaktio::games::typing_slice::TypingSliceMode>()) {
        return 1;
    }
    if (!game_mode_registry.register_mode<reaktio::games::rail_slice::RailSliceMode>()) {
        return 1;
    }

    reaktio::app::RuntimeConfigurationLoadResult config_result =
        reaktio::app::load_runtime_configuration();
    for (const reaktio::app::RuntimeConfigurationIssue& issue : config_result.issues) {
        std::ostream& stream = issue.fatal ? std::cerr : std::clog;
        stream << (issue.fatal ? "Configuration error" : "Configuration warning") << ": "
               << issue.message;
        if (!issue.source_path.empty()) {
            stream << " [" << issue.source_path.string();
            if (issue.line > 0) {
                stream << ':' << issue.line;
            }
            stream << ']';
        }
        stream << '\n';
    }

    if (!config_result.success()) {
        return 1;
    }

    reaktio::app::RuntimeConfiguration runtime_configuration = std::move(config_result.configuration);

    reaktio::app::SmokeApplicationDependencies dependencies{
        std::move(runtime_configuration.runtime_budget),
        std::move(runtime_configuration.application_config),
        std::move(runtime_configuration.startup_mode_id),
        runtime_configuration.random_seed,
        reaktio::platform::capture_stack_probe(),
        std::move(game_mode_registry),
        &std::cout,
        config_result.loaded_from_file ? config_result.source_path.string() : std::string("<defaults>"),
        std::move(runtime_configuration.input_bindings),
        std::move(runtime_configuration.input_action_maps),
        std::move(runtime_configuration.mode_configuration),
        std::move(runtime_configuration.modifiers),
        std::move(runtime_configuration.hot_reload),
    };

    // Phase 8/9: have the smoke run deterministic post-shutdown lifecycle
    // exercises of every registered slice mode. The smoke library treats
    // each entry as an opaque IGameMode; main.cpp is the only place that
    // knows about each slice's concrete type. Replay recording is disabled
    // for both because the smoke's replay session is already finalized by
    // shutdown.
    {
        reaktio::games::typing_slice::TypingSliceConfig typing_dry_run_config{};
        typing_dry_run_config.record_replay_samples = false;
        reaktio::app::SmokeApplicationDependencies::PostShutdownDryRunEntry typing_entry{};
        typing_entry.mode = std::make_unique<reaktio::games::typing_slice::TypingSliceMode>(
            std::move(typing_dry_run_config));
        typing_entry.label = "typing-slice-shared-stack-validation";
        const auto push_text = [&](std::string_view text) {
            reaktio::app::SmokeApplicationDependencies::PostShutdownScriptedFrame frame{};
            frame.utf8_text = std::string(text);
            typing_entry.scripted_frames.push_back(std::move(frame));
        };
        push_text("h");
        push_text("i");
        push_text(" ");
        push_text("r");
        push_text("e");
        push_text("a");
        push_text("X");  // deliberate miss for "k"
        push_text("k");
        push_text("t");
        push_text("i");
        push_text("o");
        push_text("!");  // tail mismatch (prompt is "hi reaktio")
        dependencies.post_shutdown_dry_runs.push_back(std::move(typing_entry));
    }

    {
        reaktio::games::rail_slice::RailSliceConfig rail_dry_run_config{};
        rail_dry_run_config.record_replay_samples = false;
        // The dry-run script is short by design (a smoke verifier, not a
        // full play session). Bump the player rail velocity so the scripted
        // 405-step run actually traverses the entire rail and exercises the
        // full lifecycle (Idle -> Playing -> Cleared -> Results). Real game
        // sessions use the slice's default 8 m/s.
        rail_dry_run_config.player_arc_velocity = 40.0;
        // Anchor the hold rule's window squarely inside the journey below
        // so the scripted hold phase actually completes.
        rail_dry_run_config.hold_rule_config.start_arc_length = 16.0;
        rail_dry_run_config.hold_rule_config.end_arc_length = 22.0;
        reaktio::app::SmokeApplicationDependencies::PostShutdownDryRunEntry rail_entry{};
        rail_entry.mode = std::make_unique<reaktio::games::rail_slice::RailSliceMode>(
            std::move(rail_dry_run_config));
        rail_entry.label = "rail-slice-shared-stack-validation";
        const auto push_action = [&](std::string_view action_id, bool pressed, bool down) {
            reaktio::app::SmokeApplicationDependencies::PostShutdownScriptedFrame frame{};
            frame.action_context_id = std::string(reaktio::games::rail_slice::k_rail_action_context);
            frame.action_id = std::string(action_id);
            frame.action_pressed = pressed;
            frame.action_down = down || pressed;
            rail_entry.scripted_frames.push_back(std::move(frame));
        };
        const auto push_idle = [&]() {
            reaktio::app::SmokeApplicationDependencies::PostShutdownScriptedFrame frame{};
            rail_entry.scripted_frames.push_back(std::move(frame));
        };
        // Scripted player journey (player velocity = 40 m/s, fixed step
        // 1/120s -> ~0.333 m/step). Total rail = 98 m, ~294 steps to reach
        // the end. Layout sequence:
        //   - swap right early to dodge the lane-0 hazard at arc 12.
        //   - hold the hold action across arcs 16..22 (steps ~48..66) so
        //     the HoldRule transitions through Armed -> InProgress ->
        //     Completed.
        //   - return to lane 0 to land the lane-0 shootable at arc 25.
        //   - fire bursts.
        //   - swap to lane -1 for the second shootable at arc 32.
        //   - fire again.
        //   - idle through the rest of the rail to the cleared/results state.
        for (int i = 0; i < 5; ++i) {
            push_idle();
        }
        push_action(reaktio::games::rail_slice::k_rail_action_lane_right, true, false);
        for (int i = 0; i < 38; ++i) {
            push_idle();
        }
        // Hold across the hold window. Each held step pushes a frame that
        // reports the hold action as down (no fresh press needed; tick_hold
        // only checks the down state).
        for (int i = 0; i < 25; ++i) {
            push_action(reaktio::games::rail_slice::k_rail_action_hold, false, true);
        }
        push_action(reaktio::games::rail_slice::k_rail_action_lane_left, true, false);
        for (int i = 0; i < 10; ++i) {
            push_idle();
        }
        push_action(reaktio::games::rail_slice::k_rail_action_fire, true, false);
        for (int i = 0; i < 15; ++i) {
            push_idle();
        }
        push_action(reaktio::games::rail_slice::k_rail_action_lane_left, true, false);
        for (int i = 0; i < 10; ++i) {
            push_idle();
        }
        push_action(reaktio::games::rail_slice::k_rail_action_fire, true, false);
        for (int i = 0; i < 280; ++i) {
            push_idle();
        }
        rail_entry.verifier = [](const reaktio::gameplay::IGameMode& mode) -> std::string {
            const auto* slice =
                dynamic_cast<const reaktio::games::rail_slice::RailSliceMode*>(&mode);
            if (slice == nullptr) {
                return {};
            }
            std::ostringstream extra;
            extra << " lane-swaps=" << slice->lane_swap_state().swap_count
                  << " jumps=" << slice->vertical_action_state().jump_count
                  << " slides=" << slice->vertical_action_state().slide_count
                  << " dodges=" << slice->dodge_state().dodge_count
                  << " shots=" << slice->shoot_state().emitted_count
                  << " obstacle-kills=" << slice->projectile_hits()
                  << " pickups=" << slice->pickups_collected()
                  << " hazards=" << slice->hazard_hits()
                  << " hold-completed=" << slice->hold_state().completed_count
                  << " hold-released=" << slice->hold_state().released_count
                  << " hold-missed=" << slice->hold_state().missed_count
                  << " hold-outcome=" << reaktio::gameplay::to_string(slice->hold_state().last_outcome)
                  << " spatial-cues=" << slice->spatial_cue_sample_count()
                  << " chart-cues-judged=" << slice->chart_cues_judged()
                  << " score=" << slice->scoring().summary().score
                  << " grade=" << reaktio::gameplay::to_string(slice->scoring().summary().grade);
            return extra.str();
        };
        dependencies.post_shutdown_dry_runs.push_back(std::move(rail_entry));
    }

    reaktio::app::SmokeApplication application{std::move(dependencies)};
    return application.run();
}