#pragma once

#include "reaktio/platform/InputBindings.hpp"
#include "reaktio/platform/InputSnapshot.hpp"

#include <string_view>

namespace reaktio::platform {

struct InputActionState {
    bool down{};
    bool pressed{};
    bool released{};
};

[[nodiscard]] InputActionState query_action_state(
    const InputSnapshot& input_snapshot,
    const InputBindingsConfig& input_bindings,
    std::string_view action_id) noexcept;
[[nodiscard]] bool is_action_down(
    const InputSnapshot& input_snapshot,
    const InputBindingsConfig& input_bindings,
    std::string_view action_id) noexcept;
[[nodiscard]] bool was_action_pressed(
    const InputSnapshot& input_snapshot,
    const InputBindingsConfig& input_bindings,
    std::string_view action_id) noexcept;
[[nodiscard]] bool was_action_released(
    const InputSnapshot& input_snapshot,
    const InputBindingsConfig& input_bindings,
    std::string_view action_id) noexcept;

} // namespace reaktio::platform