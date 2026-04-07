#include "reaktio/app/SmokeApplication.hpp"

#include "reaktio/platform/ApplicationConfig.hpp"
#include "reaktio/foundation/Telemetry.hpp"
#include "reaktio/games/reference/ReferenceSandboxMode.hpp"
#include "reaktio/games/templates/StarterMode.hpp"
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

    reaktio::platform::ApplicationConfig application_config =
        reaktio::platform::make_smoke_application_config();

    reaktio::app::SmokeApplicationDependencies dependencies{
        reaktio::foundation::make_bootstrap_budget(),
        std::move(application_config),
        std::string(reaktio::games::reference::ReferenceSandboxMode::mode_descriptor().id),
        reaktio::platform::capture_stack_probe(),
        std::move(game_mode_registry),
        &std::cout,
    };

    reaktio::app::SmokeApplication application{std::move(dependencies)};
    return application.run();
}