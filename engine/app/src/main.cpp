#include "reaktio/app/RuntimeConfiguration.hpp"
#include "reaktio/app/SmokeApplication.hpp"

#include "reaktio/games/reference/ReferenceSandboxMode.hpp"
#include "reaktio/games/templates/StarterMode.hpp"
#include "reaktio/games/typing_slice/TypingSliceMode.hpp"
#include "reaktio/platform/StackProbe.hpp"

#include <iostream>
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

    // Phase 8: have the smoke run a deterministic post-shutdown lifecycle
    // exercise of the registered typing slice. The smoke library treats it
    // as an opaque IGameMode, so this stays the only place that knows about
    // the typing slice's concrete type. Replay recording is disabled because
    // the smoke's replay session is already finalized by shutdown.
    reaktio::games::typing_slice::TypingSliceConfig typing_dry_run_config{};
    typing_dry_run_config.record_replay_samples = false;
    auto typing_slice =
        std::make_unique<reaktio::games::typing_slice::TypingSliceMode>(
            std::move(typing_dry_run_config));
    dependencies.post_shutdown_dry_run_mode = std::move(typing_slice);
    dependencies.post_shutdown_dry_run_label = "typing-slice-shared-stack-validation";
    dependencies.post_shutdown_dry_run_text_events = {
        "h", "i", " ", "r", "e", "a",
        "X",  // deliberate miss for "k"
        "k", "t", "i", "o",
        "!",  // tail mismatch (prompt is "hi reaktio")
    };

    reaktio::app::SmokeApplication application{std::move(dependencies)};
    return application.run();
}