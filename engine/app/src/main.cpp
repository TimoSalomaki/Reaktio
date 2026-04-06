#include "reaktio/app/SmokeApplication.hpp"

#include "reaktio/foundation/Telemetry.hpp"
#include "reaktio/games/templates/StarterMode.hpp"
#include "reaktio/platform/StackProbe.hpp"

#include <iostream>
#include <utility>

int main() {
    reaktio::gameplay::GameModeRegistry game_mode_registry;
    if (!game_mode_registry.register_mode<reaktio::games::templates::StarterMode>()) {
        return 1;
    }

    reaktio::app::SmokeApplicationDependencies dependencies{
        reaktio::foundation::make_bootstrap_budget(),
        reaktio::platform::capture_stack_probe(),
        std::move(game_mode_registry),
        &std::cout,
    };

    reaktio::app::SmokeApplication application{std::move(dependencies)};
    return application.run();
}