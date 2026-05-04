#include "reaktio/platform/InputBindingQueries.hpp"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace reaktio::platform {

namespace {

enum class BindingDevice {
    None,
    Keyboard,
    Mouse,
};

struct ResolvedBinding {
    BindingDevice device{BindingDevice::None};
    std::int32_t code{};
    bool valid{};
};

std::string trim_copy(std::string_view value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(start, end - start));
}

std::string lowercase_copy(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const unsigned char character : value) {
        lowered.push_back(static_cast<char>(std::tolower(character)));
    }

    return lowered;
}

ResolvedBinding resolve_mouse_binding(std::string_view control) noexcept {
    const std::string lowered = lowercase_copy(control);
    if (lowered == "left") {
        return ResolvedBinding{.device = BindingDevice::Mouse, .code = SDL_BUTTON_LEFT, .valid = true};
    }
    if (lowered == "middle") {
        return ResolvedBinding{.device = BindingDevice::Mouse, .code = SDL_BUTTON_MIDDLE, .valid = true};
    }
    if (lowered == "right") {
        return ResolvedBinding{.device = BindingDevice::Mouse, .code = SDL_BUTTON_RIGHT, .valid = true};
    }
    if (lowered == "x1") {
        return ResolvedBinding{.device = BindingDevice::Mouse, .code = SDL_BUTTON_X1, .valid = true};
    }
    if (lowered == "x2") {
        return ResolvedBinding{.device = BindingDevice::Mouse, .code = SDL_BUTTON_X2, .valid = true};
    }

    return ResolvedBinding{};
}

ResolvedBinding resolve_binding(std::string_view binding) noexcept {
    const std::size_t separator = binding.find(':');
    if (separator == std::string_view::npos) {
        return ResolvedBinding{};
    }

    const std::string device = lowercase_copy(binding.substr(0, separator));
    const std::string control = trim_copy(binding.substr(separator + 1));
    if (control.empty()) {
        return ResolvedBinding{};
    }

    if (device == "keyboard") {
        const SDL_Scancode scancode = SDL_GetScancodeFromName(control.c_str());
        if (scancode != SDL_SCANCODE_UNKNOWN) {
            return ResolvedBinding{.device = BindingDevice::Keyboard, .code = static_cast<std::int32_t>(scancode), .valid = true};
        }
        return ResolvedBinding{};
    }

    if (device == "mouse") {
        return resolve_mouse_binding(control);
    }

    return ResolvedBinding{};
}

InputActionState query_resolved_binding(
    const InputSnapshot& input_snapshot,
    const ResolvedBinding& binding) noexcept {
    if (!binding.valid) {
        return InputActionState{};
    }

    if (binding.device == BindingDevice::Keyboard) {
        return InputActionState{
            .down = input_snapshot.is_key_down(binding.code),
            .pressed = input_snapshot.was_key_pressed(binding.code),
            .released = input_snapshot.was_key_released(binding.code),
        };
    }

    if (binding.device == BindingDevice::Mouse) {
        const std::uint32_t mask = 1u << static_cast<std::uint32_t>(binding.code - 1);
        InputActionState state{
            .down = (input_snapshot.mouse_button_mask() & mask) != 0u,
        };
        for (const MouseButtonEvent& event : input_snapshot.mouse_button_events()) {
            if (event.button != binding.code) {
                continue;
            }

            if (event.down) {
                state.pressed = true;
            } else {
                state.released = true;
            }
        }
        return state;
    }

    return InputActionState{};
}

void merge_state(InputActionState& destination, const InputActionState& source) noexcept {
    destination.down = destination.down || source.down;
    destination.pressed = destination.pressed || source.pressed;
    destination.released = destination.released || source.released;
}

} // namespace

InputActionState query_action_state(
    const InputSnapshot& input_snapshot,
    const InputBindingsConfig& input_bindings,
    std::string_view action_id) noexcept {
    const InputActionBinding* binding = input_bindings.find_action(action_id);
    if (binding == nullptr) {
        return InputActionState{};
    }

    InputActionState state{};
    if (!binding->primary.empty()) {
        merge_state(state, query_resolved_binding(input_snapshot, resolve_binding(binding->primary)));
    }
    if (!binding->secondary.empty()) {
        merge_state(state, query_resolved_binding(input_snapshot, resolve_binding(binding->secondary)));
    }
    return state;
}

InputActionState query_binding_state(
    const InputSnapshot& input_snapshot,
    std::string_view primary,
    std::string_view secondary) noexcept {
    InputActionState state{};
    if (!primary.empty()) {
        merge_state(state, query_resolved_binding(input_snapshot, resolve_binding(primary)));
    }
    if (!secondary.empty()) {
        merge_state(state, query_resolved_binding(input_snapshot, resolve_binding(secondary)));
    }
    return state;
}

bool is_action_down(
    const InputSnapshot& input_snapshot,
    const InputBindingsConfig& input_bindings,
    std::string_view action_id) noexcept {
    return query_action_state(input_snapshot, input_bindings, action_id).down;
}

bool was_action_pressed(
    const InputSnapshot& input_snapshot,
    const InputBindingsConfig& input_bindings,
    std::string_view action_id) noexcept {
    return query_action_state(input_snapshot, input_bindings, action_id).pressed;
}

bool was_action_released(
    const InputSnapshot& input_snapshot,
    const InputBindingsConfig& input_bindings,
    std::string_view action_id) noexcept {
    return query_action_state(input_snapshot, input_bindings, action_id).released;
}

} // namespace reaktio::platform