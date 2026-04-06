#include "reaktio/platform/InputSnapshot.hpp"

#include <algorithm>

namespace reaktio::platform {

void InputSnapshot::begin_frame() noexcept {
    key_pressed_.fill(0);
    key_released_.fill(0);
    mouse_wheel_x_ = 0.0f;
    mouse_wheel_y_ = 0.0f;
    mouse_wheel_ticks_x_ = 0;
    mouse_wheel_ticks_y_ = 0;

    keyboard_events_.clear();
    text_editing_events_.clear();
    text_editing_candidates_events_.clear();
    text_input_events_.clear();
    mouse_motion_events_.clear();
    mouse_button_events_.clear();
    mouse_wheel_events_.clear();
    gamepad_connection_events_.clear();
    gamepad_axis_events_.clear();
    gamepad_button_events_.clear();
}

void InputSnapshot::record_keyboard_event(const KeyboardEvent& event) noexcept {
    if (is_valid_scancode(event.scancode)) {
        const auto index = static_cast<std::size_t>(event.scancode);
        key_down_[index] = static_cast<std::uint8_t>(event.down);
        if (event.down) {
            if (!event.repeat) {
                key_pressed_[index] = 1;
            }
        } else {
            key_released_[index] = 1;
        }
    }

    keyboard_events_.push_back(event);
}

void InputSnapshot::record_text_editing_event(TextEditingEvent event) {
    composition_text_ = event.text;
    composition_start_ = event.start;
    composition_length_ = event.length;
    text_editing_events_.push_back(std::move(event));
}

void InputSnapshot::record_text_editing_candidates_event(TextEditingCandidatesEvent event) {
    text_editing_candidates_events_.push_back(std::move(event));
}

void InputSnapshot::record_text_input_event(TextInputEvent event) {
    composition_text_.clear();
    composition_start_ = -1;
    composition_length_ = -1;
    text_input_events_.push_back(std::move(event));
}

void InputSnapshot::record_mouse_motion_event(const MouseMotionEvent& event) noexcept {
    mouse_x_ = event.x;
    mouse_y_ = event.y;
    mouse_motion_events_.push_back(event);
}

void InputSnapshot::record_mouse_button_event(const MouseButtonEvent& event) noexcept {
    mouse_x_ = event.x;
    mouse_y_ = event.y;

    if (event.button > 0 && event.button <= 32) {
        const std::uint32_t mask = 1u << (event.button - 1u);
        if (event.down) {
            mouse_button_mask_ |= mask;
        } else {
            mouse_button_mask_ &= ~mask;
        }
    }

    mouse_button_events_.push_back(event);
}

void InputSnapshot::record_mouse_wheel_event(const MouseWheelEvent& event) noexcept {
    mouse_wheel_x_ += event.x;
    mouse_wheel_y_ += event.y;
    mouse_wheel_ticks_x_ += event.ticks_x;
    mouse_wheel_ticks_y_ += event.ticks_y;
    mouse_wheel_events_.push_back(event);
}

void InputSnapshot::record_gamepad_connection_event(GamepadConnectionEvent event) {
    auto existing = std::find_if(
        connected_gamepads_.begin(),
        connected_gamepads_.end(),
        [&event](const GamepadDevice& device) { return device.instance_id == event.device.instance_id; });

    if (event.connected) {
        if (existing == connected_gamepads_.end()) {
            connected_gamepads_.push_back(event.device);
        } else {
            *existing = event.device;
        }
    } else if (existing != connected_gamepads_.end()) {
        connected_gamepads_.erase(existing);
    }

    gamepad_connection_events_.push_back(std::move(event));
}

void InputSnapshot::record_gamepad_axis_event(const GamepadAxisEvent& event) noexcept {
    gamepad_axis_events_.push_back(event);
}

void InputSnapshot::record_gamepad_button_event(const GamepadButtonEvent& event) noexcept {
    gamepad_button_events_.push_back(event);
}

bool InputSnapshot::is_key_down(std::int32_t scancode) const noexcept {
    if (!is_valid_scancode(scancode)) {
        return false;
    }

    return key_down_[static_cast<std::size_t>(scancode)] != 0;
}

bool InputSnapshot::was_key_pressed(std::int32_t scancode) const noexcept {
    if (!is_valid_scancode(scancode)) {
        return false;
    }

    return key_pressed_[static_cast<std::size_t>(scancode)] != 0;
}

bool InputSnapshot::was_key_released(std::int32_t scancode) const noexcept {
    if (!is_valid_scancode(scancode)) {
        return false;
    }

    return key_released_[static_cast<std::size_t>(scancode)] != 0;
}

float InputSnapshot::mouse_x() const noexcept {
    return mouse_x_;
}

float InputSnapshot::mouse_y() const noexcept {
    return mouse_y_;
}

std::uint32_t InputSnapshot::mouse_button_mask() const noexcept {
    return mouse_button_mask_;
}

float InputSnapshot::mouse_wheel_x() const noexcept {
    return mouse_wheel_x_;
}

float InputSnapshot::mouse_wheel_y() const noexcept {
    return mouse_wheel_y_;
}

std::int32_t InputSnapshot::mouse_wheel_ticks_x() const noexcept {
    return mouse_wheel_ticks_x_;
}

std::int32_t InputSnapshot::mouse_wheel_ticks_y() const noexcept {
    return mouse_wheel_ticks_y_;
}

const std::string& InputSnapshot::composition_text() const noexcept {
    return composition_text_;
}

std::int32_t InputSnapshot::composition_start() const noexcept {
    return composition_start_;
}

std::int32_t InputSnapshot::composition_length() const noexcept {
    return composition_length_;
}

const std::vector<KeyboardEvent>& InputSnapshot::keyboard_events() const noexcept {
    return keyboard_events_;
}

const std::vector<TextEditingEvent>& InputSnapshot::text_editing_events() const noexcept {
    return text_editing_events_;
}

const std::vector<TextEditingCandidatesEvent>& InputSnapshot::text_editing_candidates_events() const noexcept {
    return text_editing_candidates_events_;
}

const std::vector<TextInputEvent>& InputSnapshot::text_input_events() const noexcept {
    return text_input_events_;
}

const std::vector<MouseMotionEvent>& InputSnapshot::mouse_motion_events() const noexcept {
    return mouse_motion_events_;
}

const std::vector<MouseButtonEvent>& InputSnapshot::mouse_button_events() const noexcept {
    return mouse_button_events_;
}

const std::vector<MouseWheelEvent>& InputSnapshot::mouse_wheel_events() const noexcept {
    return mouse_wheel_events_;
}

const std::vector<GamepadConnectionEvent>& InputSnapshot::gamepad_connection_events() const noexcept {
    return gamepad_connection_events_;
}

const std::vector<GamepadAxisEvent>& InputSnapshot::gamepad_axis_events() const noexcept {
    return gamepad_axis_events_;
}

const std::vector<GamepadButtonEvent>& InputSnapshot::gamepad_button_events() const noexcept {
    return gamepad_button_events_;
}

const std::vector<GamepadDevice>& InputSnapshot::connected_gamepads() const noexcept {
    return connected_gamepads_;
}

bool InputSnapshot::is_valid_scancode(std::int32_t scancode) noexcept {
    return scancode >= 0 && static_cast<std::size_t>(scancode) < k_keyboard_scancode_capacity;
}

} // namespace reaktio::platform