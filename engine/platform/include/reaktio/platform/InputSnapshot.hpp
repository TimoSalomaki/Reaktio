#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace reaktio::platform {

inline constexpr std::size_t k_keyboard_scancode_capacity = 512;

struct KeyboardEvent {
    std::uint64_t timestamp_ns{};
    std::uint32_t window_id{};
    std::int32_t scancode{};
    std::int32_t keycode{};
    std::uint16_t modifiers{};
    bool down{};
    bool repeat{};
};

struct TextEditingEvent {
    std::uint64_t timestamp_ns{};
    std::uint32_t window_id{};
    std::string text;
    std::int32_t start{-1};
    std::int32_t length{-1};
};

struct TextEditingCandidatesEvent {
    std::uint64_t timestamp_ns{};
    std::uint32_t window_id{};
    std::vector<std::string> candidates;
    std::int32_t selected_candidate{-1};
    bool horizontal{};
};

struct TextInputEvent {
    std::uint64_t timestamp_ns{};
    std::uint32_t window_id{};
    std::string text;
};

struct MouseMotionEvent {
    std::uint64_t timestamp_ns{};
    std::uint32_t window_id{};
    std::uint32_t state_mask{};
    float x{};
    float y{};
    float delta_x{};
    float delta_y{};
};

struct MouseButtonEvent {
    std::uint64_t timestamp_ns{};
    std::uint32_t window_id{};
    std::uint8_t button{};
    bool down{};
    std::uint8_t clicks{};
    float x{};
    float y{};
};

struct MouseWheelEvent {
    std::uint64_t timestamp_ns{};
    std::uint32_t window_id{};
    float x{};
    float y{};
    std::int32_t ticks_x{};
    std::int32_t ticks_y{};
};

struct GamepadDevice {
    std::int32_t instance_id{};
    std::string name;
};

struct GamepadConnectionEvent {
    std::uint64_t timestamp_ns{};
    GamepadDevice device;
    bool connected{};
};

struct GamepadAxisEvent {
    std::uint64_t timestamp_ns{};
    std::int32_t instance_id{};
    std::int32_t axis{};
    std::int16_t value{};
};

struct GamepadButtonEvent {
    std::uint64_t timestamp_ns{};
    std::int32_t instance_id{};
    std::int32_t button{};
    bool down{};
};

class InputSnapshot {
  public:
    void begin_frame() noexcept;

    void record_keyboard_event(const KeyboardEvent& event) noexcept;
    void record_text_editing_event(TextEditingEvent event);
    void record_text_editing_candidates_event(TextEditingCandidatesEvent event);
    void record_text_input_event(TextInputEvent event);
    void record_mouse_motion_event(const MouseMotionEvent& event) noexcept;
    void record_mouse_button_event(const MouseButtonEvent& event) noexcept;
    void record_mouse_wheel_event(const MouseWheelEvent& event) noexcept;
    void record_gamepad_connection_event(GamepadConnectionEvent event);
    void record_gamepad_axis_event(const GamepadAxisEvent& event) noexcept;
    void record_gamepad_button_event(const GamepadButtonEvent& event) noexcept;

    [[nodiscard]] bool is_key_down(std::int32_t scancode) const noexcept;
    [[nodiscard]] bool was_key_pressed(std::int32_t scancode) const noexcept;
    [[nodiscard]] bool was_key_released(std::int32_t scancode) const noexcept;

    [[nodiscard]] float mouse_x() const noexcept;
    [[nodiscard]] float mouse_y() const noexcept;
    [[nodiscard]] std::uint32_t mouse_button_mask() const noexcept;
    [[nodiscard]] float mouse_wheel_x() const noexcept;
    [[nodiscard]] float mouse_wheel_y() const noexcept;
    [[nodiscard]] std::int32_t mouse_wheel_ticks_x() const noexcept;
    [[nodiscard]] std::int32_t mouse_wheel_ticks_y() const noexcept;

    [[nodiscard]] const std::string& composition_text() const noexcept;
    [[nodiscard]] std::int32_t composition_start() const noexcept;
    [[nodiscard]] std::int32_t composition_length() const noexcept;

    [[nodiscard]] const std::vector<KeyboardEvent>& keyboard_events() const noexcept;
    [[nodiscard]] const std::vector<TextEditingEvent>& text_editing_events() const noexcept;
    [[nodiscard]] const std::vector<TextEditingCandidatesEvent>& text_editing_candidates_events() const noexcept;
    [[nodiscard]] const std::vector<TextInputEvent>& text_input_events() const noexcept;
    [[nodiscard]] const std::vector<MouseMotionEvent>& mouse_motion_events() const noexcept;
    [[nodiscard]] const std::vector<MouseButtonEvent>& mouse_button_events() const noexcept;
    [[nodiscard]] const std::vector<MouseWheelEvent>& mouse_wheel_events() const noexcept;
    [[nodiscard]] const std::vector<GamepadConnectionEvent>& gamepad_connection_events() const noexcept;
    [[nodiscard]] const std::vector<GamepadAxisEvent>& gamepad_axis_events() const noexcept;
    [[nodiscard]] const std::vector<GamepadButtonEvent>& gamepad_button_events() const noexcept;
    [[nodiscard]] const std::vector<GamepadDevice>& connected_gamepads() const noexcept;

  private:
    static bool is_valid_scancode(std::int32_t scancode) noexcept;

    std::array<std::uint8_t, k_keyboard_scancode_capacity> key_down_{};
    std::array<std::uint8_t, k_keyboard_scancode_capacity> key_pressed_{};
    std::array<std::uint8_t, k_keyboard_scancode_capacity> key_released_{};
    float mouse_x_{};
    float mouse_y_{};
    std::uint32_t mouse_button_mask_{};
    float mouse_wheel_x_{};
    float mouse_wheel_y_{};
    std::int32_t mouse_wheel_ticks_x_{};
    std::int32_t mouse_wheel_ticks_y_{};
    std::string composition_text_;
    std::int32_t composition_start_{-1};
    std::int32_t composition_length_{-1};
    std::vector<KeyboardEvent> keyboard_events_;
    std::vector<TextEditingEvent> text_editing_events_;
    std::vector<TextEditingCandidatesEvent> text_editing_candidates_events_;
    std::vector<TextInputEvent> text_input_events_;
    std::vector<MouseMotionEvent> mouse_motion_events_;
    std::vector<MouseButtonEvent> mouse_button_events_;
    std::vector<MouseWheelEvent> mouse_wheel_events_;
    std::vector<GamepadConnectionEvent> gamepad_connection_events_;
    std::vector<GamepadAxisEvent> gamepad_axis_events_;
    std::vector<GamepadButtonEvent> gamepad_button_events_;
    std::vector<GamepadDevice> connected_gamepads_;
};

} // namespace reaktio::platform