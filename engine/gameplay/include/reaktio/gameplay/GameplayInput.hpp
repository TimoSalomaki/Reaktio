#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace reaktio::gameplay {

inline constexpr std::string_view k_default_input_context_id = "gameplay";
inline constexpr std::string_view k_default_input_device_profile_id = "keyboard_mouse";

struct InputDeviceProfile {
    std::string profile_id;
    std::string display_name;
};

struct InputContextState {
    std::string context_id;
    bool active{};
};

struct InputActionBinding {
    std::string context_id;
    std::string action_id;
    std::string device_profile_id;
    std::string primary;
    std::string secondary;
};

struct InputActionMapSummary {
    std::size_t device_profile_count{};
    std::size_t context_count{};
    std::size_t active_context_count{};
    std::size_t binding_count{};
};

class InputActionMapStore {
  public:
    void clear() noexcept;
    void add_device_profile(std::string_view profile_id, std::string_view display_name = {});
    void set_active_device_profile(std::string_view profile_id);
    void set_context_active(std::string_view context_id, bool active);
    void clear_active_contexts() noexcept;
    void set_binding(
        std::string_view context_id,
        std::string_view action_id,
        std::string_view primary,
        std::string_view secondary = {},
        std::string_view device_profile_id = k_default_input_device_profile_id);
    [[nodiscard]] bool rebind_primary(
        std::string_view context_id,
        std::string_view action_id,
        std::string_view binding,
        std::string_view device_profile_id = k_default_input_device_profile_id);
    [[nodiscard]] bool rebind_secondary(
        std::string_view context_id,
        std::string_view action_id,
        std::string_view binding,
        std::string_view device_profile_id = k_default_input_device_profile_id);

    [[nodiscard]] std::string_view active_device_profile() const noexcept;
    [[nodiscard]] bool has_device_profile(std::string_view profile_id) const noexcept;
    [[nodiscard]] bool is_context_active(std::string_view context_id) const noexcept;
    [[nodiscard]] const InputActionBinding* find_binding(
        std::string_view context_id,
        std::string_view action_id,
        std::string_view device_profile_id = k_default_input_device_profile_id) const noexcept;
    [[nodiscard]] std::span<const InputDeviceProfile> device_profiles() const noexcept;
    [[nodiscard]] std::span<const InputContextState> contexts() const noexcept;
    [[nodiscard]] std::span<const InputActionBinding> bindings() const noexcept;
    [[nodiscard]] InputActionMapSummary summary() const noexcept;

  private:
    InputActionBinding* find_binding_mutable(
        std::string_view context_id,
        std::string_view action_id,
        std::string_view device_profile_id) noexcept;

    std::vector<InputDeviceProfile> device_profiles_;
    std::vector<InputContextState> contexts_;
    std::vector<InputActionBinding> bindings_;
    std::string active_device_profile_{std::string(k_default_input_device_profile_id)};
};

struct InputActionState {
    std::string context_id;
    std::string action_id;
    bool down{};
    bool pressed{};
    bool released{};
};

class ActionInputSurface {
  public:
    void clear() noexcept;
    void set_action_state(InputActionState state);

    [[nodiscard]] const InputActionState* find(
        std::string_view context_id,
        std::string_view action_id) const noexcept;
    [[nodiscard]] const InputActionState* find(std::string_view action_id) const noexcept;
    [[nodiscard]] InputActionState state(
        std::string_view context_id,
        std::string_view action_id) const noexcept;
    [[nodiscard]] InputActionState state(std::string_view action_id) const noexcept;
    [[nodiscard]] bool is_down(std::string_view context_id, std::string_view action_id) const noexcept;
    [[nodiscard]] bool is_down(std::string_view action_id) const noexcept;
    [[nodiscard]] bool was_pressed(std::string_view context_id, std::string_view action_id) const noexcept;
    [[nodiscard]] bool was_pressed(std::string_view action_id) const noexcept;
    [[nodiscard]] bool was_released(std::string_view context_id, std::string_view action_id) const noexcept;
    [[nodiscard]] bool was_released(std::string_view action_id) const noexcept;
    [[nodiscard]] std::span<const InputActionState> actions() const noexcept;
    [[nodiscard]] std::size_t action_count() const noexcept;
    [[nodiscard]] std::size_t down_count() const noexcept;
    [[nodiscard]] std::size_t pressed_count() const noexcept;
    [[nodiscard]] std::size_t released_count() const noexcept;

  private:
    std::vector<InputActionState> actions_;
};

struct TextInputEvent {
    std::uint64_t timestamp_ns{};
    std::string text;
};

struct TextCompositionState {
    std::string text;
    std::int32_t start{-1};
    std::int32_t length{-1};
    std::vector<std::string> candidates;
    std::int32_t selected_candidate{-1};
    bool candidates_horizontal{};
};

class TextInputSurface {
  public:
    void clear() noexcept;
    void add_text_event(TextInputEvent event);
    void set_composition(TextCompositionState composition);

    [[nodiscard]] std::span<const TextInputEvent> text_events() const noexcept;
    [[nodiscard]] const TextCompositionState& composition() const noexcept;
    [[nodiscard]] bool has_composition() const noexcept;
    [[nodiscard]] std::size_t event_count() const noexcept;

  private:
    std::vector<TextInputEvent> text_events_;
    TextCompositionState composition_{};
};

struct PointerAnalogState {
    float x{};
    float y{};
    float delta_x{};
    float delta_y{};
    float wheel_x{};
    float wheel_y{};
    std::int32_t wheel_ticks_x{};
    std::int32_t wheel_ticks_y{};
    std::uint32_t button_mask{};
    bool moved{};
    bool wheeled{};
};

struct AnalogAxisState {
    std::int32_t device_id{};
    std::int32_t axis{};
    std::int16_t raw_value{};
    float normalized_value{};
};

class AnalogInputSurface {
  public:
    void clear() noexcept;
    void set_pointer(PointerAnalogState pointer) noexcept;
    void add_axis(AnalogAxisState axis);
    void set_connected_gamepad_count(std::size_t count) noexcept;

    [[nodiscard]] const PointerAnalogState& pointer() const noexcept;
    [[nodiscard]] std::span<const AnalogAxisState> axes() const noexcept;
    [[nodiscard]] std::size_t axis_count() const noexcept;
    [[nodiscard]] std::size_t connected_gamepad_count() const noexcept;

  private:
    PointerAnalogState pointer_{};
    std::vector<AnalogAxisState> axes_;
    std::size_t connected_gamepad_count_{};
};

class ModeInputFrame {
  public:
    void clear() noexcept;

    [[nodiscard]] ActionInputSurface& mutable_actions() noexcept;
    [[nodiscard]] TextInputSurface& mutable_text() noexcept;
    [[nodiscard]] AnalogInputSurface& mutable_analog() noexcept;

    [[nodiscard]] const ActionInputSurface& actions() const noexcept;
    [[nodiscard]] const TextInputSurface& text() const noexcept;
    [[nodiscard]] const AnalogInputSurface& analog() const noexcept;

  private:
    ActionInputSurface actions_;
    TextInputSurface text_;
    AnalogInputSurface analog_;
};

} // namespace reaktio::gameplay