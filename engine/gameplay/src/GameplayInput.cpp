#include "reaktio/gameplay/GameplayInput.hpp"

#include <algorithm>
#include <utility>

namespace reaktio::gameplay {

namespace {

std::string fallback_display_name(std::string_view profile_id) {
    return profile_id.empty() ? std::string{} : std::string(profile_id);
}

} // namespace

void InputActionMapStore::clear() noexcept {
    device_profiles_.clear();
    contexts_.clear();
    bindings_.clear();
    active_device_profile_ = std::string(k_default_input_device_profile_id);
}

void InputActionMapStore::add_device_profile(std::string_view profile_id, std::string_view display_name) {
    if (profile_id.empty()) {
        return;
    }

    for (InputDeviceProfile& profile : device_profiles_) {
        if (profile.profile_id == profile_id) {
            profile.display_name = display_name.empty() ? fallback_display_name(profile_id) : std::string(display_name);
            return;
        }
    }

    device_profiles_.push_back(InputDeviceProfile{
        .profile_id = std::string(profile_id),
        .display_name = display_name.empty() ? fallback_display_name(profile_id) : std::string(display_name),
    });
}

void InputActionMapStore::set_active_device_profile(std::string_view profile_id) {
    if (profile_id.empty()) {
        return;
    }

    add_device_profile(profile_id);
    active_device_profile_ = std::string(profile_id);
}

void InputActionMapStore::set_context_active(std::string_view context_id, bool active) {
    if (context_id.empty()) {
        return;
    }

    for (InputContextState& context : contexts_) {
        if (context.context_id == context_id) {
            context.active = active;
            return;
        }
    }

    contexts_.push_back(InputContextState{
        .context_id = std::string(context_id),
        .active = active,
    });
}

void InputActionMapStore::clear_active_contexts() noexcept {
    for (InputContextState& context : contexts_) {
        context.active = false;
    }
}

void InputActionMapStore::set_binding(
    std::string_view context_id,
    std::string_view action_id,
    std::string_view primary,
    std::string_view secondary,
    std::string_view device_profile_id) {
    if (context_id.empty() || action_id.empty() || device_profile_id.empty()) {
        return;
    }

    add_device_profile(device_profile_id);
    set_context_active(context_id, is_context_active(context_id));

    if (InputActionBinding* binding = find_binding_mutable(context_id, action_id, device_profile_id); binding != nullptr) {
        binding->primary = std::string(primary);
        binding->secondary = std::string(secondary);
        return;
    }

    bindings_.push_back(InputActionBinding{
        .context_id = std::string(context_id),
        .action_id = std::string(action_id),
        .device_profile_id = std::string(device_profile_id),
        .primary = std::string(primary),
        .secondary = std::string(secondary),
    });
}

bool InputActionMapStore::rebind_primary(
    std::string_view context_id,
    std::string_view action_id,
    std::string_view binding,
    std::string_view device_profile_id) {
    if (InputActionBinding* action_binding = find_binding_mutable(context_id, action_id, device_profile_id); action_binding != nullptr) {
        action_binding->primary = std::string(binding);
        return true;
    }

    return false;
}

bool InputActionMapStore::rebind_secondary(
    std::string_view context_id,
    std::string_view action_id,
    std::string_view binding,
    std::string_view device_profile_id) {
    if (InputActionBinding* action_binding = find_binding_mutable(context_id, action_id, device_profile_id); action_binding != nullptr) {
        action_binding->secondary = std::string(binding);
        return true;
    }

    return false;
}

std::string_view InputActionMapStore::active_device_profile() const noexcept {
    return active_device_profile_;
}

bool InputActionMapStore::has_device_profile(std::string_view profile_id) const noexcept {
    return std::any_of(device_profiles_.begin(), device_profiles_.end(), [profile_id](const InputDeviceProfile& profile) {
        return profile.profile_id == profile_id;
    });
}

bool InputActionMapStore::is_context_active(std::string_view context_id) const noexcept {
    const auto it = std::find_if(contexts_.begin(), contexts_.end(), [context_id](const InputContextState& context) {
        return context.context_id == context_id;
    });
    return it != contexts_.end() && it->active;
}

const InputActionBinding* InputActionMapStore::find_binding(
    std::string_view context_id,
    std::string_view action_id,
    std::string_view device_profile_id) const noexcept {
    const auto it = std::find_if(bindings_.begin(), bindings_.end(), [&](const InputActionBinding& binding) {
        return binding.context_id == context_id && binding.action_id == action_id &&
               binding.device_profile_id == device_profile_id;
    });
    return it != bindings_.end() ? &(*it) : nullptr;
}

std::span<const InputDeviceProfile> InputActionMapStore::device_profiles() const noexcept {
    return std::span<const InputDeviceProfile>{device_profiles_.data(), device_profiles_.size()};
}

std::span<const InputContextState> InputActionMapStore::contexts() const noexcept {
    return std::span<const InputContextState>{contexts_.data(), contexts_.size()};
}

std::span<const InputActionBinding> InputActionMapStore::bindings() const noexcept {
    return std::span<const InputActionBinding>{bindings_.data(), bindings_.size()};
}

InputActionMapSummary InputActionMapStore::summary() const noexcept {
    return InputActionMapSummary{
        .device_profile_count = device_profiles_.size(),
        .context_count = contexts_.size(),
        .active_context_count = static_cast<std::size_t>(std::count_if(contexts_.begin(), contexts_.end(), [](const InputContextState& context) {
            return context.active;
        })),
        .binding_count = bindings_.size(),
    };
}

InputActionBinding* InputActionMapStore::find_binding_mutable(
    std::string_view context_id,
    std::string_view action_id,
    std::string_view device_profile_id) noexcept {
    const auto it = std::find_if(bindings_.begin(), bindings_.end(), [&](const InputActionBinding& binding) {
        return binding.context_id == context_id && binding.action_id == action_id &&
               binding.device_profile_id == device_profile_id;
    });
    return it != bindings_.end() ? &(*it) : nullptr;
}

void ActionInputSurface::clear() noexcept {
    actions_.clear();
}

void ActionInputSurface::set_action_state(InputActionState state) {
    for (InputActionState& existing : actions_) {
        if (existing.context_id == state.context_id && existing.action_id == state.action_id) {
            existing = std::move(state);
            return;
        }
    }

    actions_.push_back(std::move(state));
}

const InputActionState* ActionInputSurface::find(
    std::string_view context_id,
    std::string_view action_id) const noexcept {
    const auto it = std::find_if(actions_.begin(), actions_.end(), [&](const InputActionState& state) {
        return state.context_id == context_id && state.action_id == action_id;
    });
    return it != actions_.end() ? &(*it) : nullptr;
}

const InputActionState* ActionInputSurface::find(std::string_view action_id) const noexcept {
    const auto it = std::find_if(actions_.begin(), actions_.end(), [action_id](const InputActionState& state) {
        return state.action_id == action_id;
    });
    return it != actions_.end() ? &(*it) : nullptr;
}

InputActionState ActionInputSurface::state(
    std::string_view context_id,
    std::string_view action_id) const noexcept {
    if (const InputActionState* action_state = find(context_id, action_id); action_state != nullptr) {
        return *action_state;
    }
    return InputActionState{.context_id = std::string(context_id), .action_id = std::string(action_id)};
}

InputActionState ActionInputSurface::state(std::string_view action_id) const noexcept {
    if (const InputActionState* action_state = find(action_id); action_state != nullptr) {
        return *action_state;
    }
    return InputActionState{.context_id = std::string(k_default_input_context_id), .action_id = std::string(action_id)};
}

bool ActionInputSurface::is_down(std::string_view context_id, std::string_view action_id) const noexcept {
    return state(context_id, action_id).down;
}

bool ActionInputSurface::is_down(std::string_view action_id) const noexcept {
    return state(action_id).down;
}

bool ActionInputSurface::was_pressed(std::string_view context_id, std::string_view action_id) const noexcept {
    return state(context_id, action_id).pressed;
}

bool ActionInputSurface::was_pressed(std::string_view action_id) const noexcept {
    return state(action_id).pressed;
}

bool ActionInputSurface::was_released(std::string_view context_id, std::string_view action_id) const noexcept {
    return state(context_id, action_id).released;
}

bool ActionInputSurface::was_released(std::string_view action_id) const noexcept {
    return state(action_id).released;
}

std::span<const InputActionState> ActionInputSurface::actions() const noexcept {
    return std::span<const InputActionState>{actions_.data(), actions_.size()};
}

std::size_t ActionInputSurface::action_count() const noexcept {
    return actions_.size();
}

std::size_t ActionInputSurface::down_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(actions_.begin(), actions_.end(), [](const InputActionState& state) {
        return state.down;
    }));
}

std::size_t ActionInputSurface::pressed_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(actions_.begin(), actions_.end(), [](const InputActionState& state) {
        return state.pressed;
    }));
}

std::size_t ActionInputSurface::released_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(actions_.begin(), actions_.end(), [](const InputActionState& state) {
        return state.released;
    }));
}

void TextInputSurface::clear() noexcept {
    text_events_.clear();
    composition_ = {};
}

void TextInputSurface::add_text_event(TextInputEvent event) {
    text_events_.push_back(std::move(event));
}

void TextInputSurface::set_composition(TextCompositionState composition) {
    composition_ = std::move(composition);
}

std::span<const TextInputEvent> TextInputSurface::text_events() const noexcept {
    return std::span<const TextInputEvent>{text_events_.data(), text_events_.size()};
}

const TextCompositionState& TextInputSurface::composition() const noexcept {
    return composition_;
}

bool TextInputSurface::has_composition() const noexcept {
    return !composition_.text.empty();
}

std::size_t TextInputSurface::event_count() const noexcept {
    return text_events_.size();
}

void AnalogInputSurface::clear() noexcept {
    pointer_ = {};
    axes_.clear();
    connected_gamepad_count_ = 0;
}

void AnalogInputSurface::set_pointer(PointerAnalogState pointer) noexcept {
    pointer_ = pointer;
}

void AnalogInputSurface::add_axis(AnalogAxisState axis) {
    axes_.push_back(axis);
}

void AnalogInputSurface::set_connected_gamepad_count(std::size_t count) noexcept {
    connected_gamepad_count_ = count;
}

const PointerAnalogState& AnalogInputSurface::pointer() const noexcept {
    return pointer_;
}

std::span<const AnalogAxisState> AnalogInputSurface::axes() const noexcept {
    return std::span<const AnalogAxisState>{axes_.data(), axes_.size()};
}

std::size_t AnalogInputSurface::axis_count() const noexcept {
    return axes_.size();
}

std::size_t AnalogInputSurface::connected_gamepad_count() const noexcept {
    return connected_gamepad_count_;
}

void ModeInputFrame::clear() noexcept {
    actions_.clear();
    text_.clear();
    analog_.clear();
}

ActionInputSurface& ModeInputFrame::mutable_actions() noexcept {
    return actions_;
}

TextInputSurface& ModeInputFrame::mutable_text() noexcept {
    return text_;
}

AnalogInputSurface& ModeInputFrame::mutable_analog() noexcept {
    return analog_;
}

const ActionInputSurface& ModeInputFrame::actions() const noexcept {
    return actions_;
}

const TextInputSurface& ModeInputFrame::text() const noexcept {
    return text_;
}

const AnalogInputSurface& ModeInputFrame::analog() const noexcept {
    return analog_;
}

} // namespace reaktio::gameplay