#pragma once

#include "reaktio/foundation/DeterministicRandom.hpp"
#include "reaktio/foundation/Telemetry.hpp"
#include "reaktio/gameplay/ModeConfiguration.hpp"
#include "reaktio/platform/ApplicationConfig.hpp"
#include "reaktio/platform/InputBindings.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reaktio::app {

struct RuntimeConfiguration {
    foundation::RuntimeBudget runtime_budget{foundation::make_bootstrap_budget()};
    platform::ApplicationConfig application_config{platform::make_smoke_application_config()};
    platform::InputBindingsConfig input_bindings;
    gameplay::ModeConfigurationStore mode_configuration;
    std::string startup_mode_id{"mode.reference.sandbox"};
    std::uint64_t random_seed{foundation::k_default_random_seed};
};

struct RuntimeConfigurationIssue {
    std::filesystem::path source_path;
    std::size_t line{};
    std::string message;
    bool fatal{};
};

struct RuntimeConfigurationLoadResult {
    RuntimeConfiguration configuration;
    std::filesystem::path source_path;
    std::vector<RuntimeConfigurationIssue> issues;
    bool loaded_from_file{};

    [[nodiscard]] bool success() const noexcept;
};

[[nodiscard]] RuntimeConfiguration make_default_runtime_configuration();
[[nodiscard]] RuntimeConfigurationLoadResult load_runtime_configuration();

} // namespace reaktio::app