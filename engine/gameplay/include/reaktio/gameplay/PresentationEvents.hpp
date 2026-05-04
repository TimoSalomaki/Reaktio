#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace reaktio::gameplay {

enum class CameraEventKind : std::uint8_t {
    Shake,
    Kick,
    FocusPulse,
    ZoomPulse,
    LookAt,
    Custom,
};

struct CameraEvent {
    CameraEventKind kind{CameraEventKind::Shake};
    std::string id;
    double duration_seconds{};
    double intensity{};
    std::array<float, 3> direction{0.0f, 0.0f, 0.0f};
    std::uint32_t channel_index{};
    std::uint64_t simulation_step{};
    std::uint64_t frame_index{};
};

enum class ScreenEffectKind : std::uint8_t {
    Flash,
    Vignette,
    ColorPulse,
    Shockwave,
    Chromatic,
    Custom,
};

struct ScreenEffectEvent {
    ScreenEffectKind kind{ScreenEffectKind::Flash};
    std::string id;
    double duration_seconds{};
    double intensity{};
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    std::uint64_t simulation_step{};
    std::uint64_t frame_index{};
};

enum class HapticsTarget : std::uint8_t {
    Default,
    ControllerPrimary,
    ControllerSecondary,
    KeyboardLed,
    Custom,
};

enum class HapticsKind : std::uint8_t {
    Pulse,
    Pattern,
    Cancel,
    Custom,
};

struct HapticsEvent {
    HapticsKind kind{HapticsKind::Pulse};
    HapticsTarget target{HapticsTarget::Default};
    std::string id;
    double duration_seconds{};
    double intensity_low{};
    double intensity_high{};
    std::uint64_t simulation_step{};
    std::uint64_t frame_index{};
};

struct PresentationEventStatistics {
    std::uint64_t camera_event_count{};
    std::uint64_t screen_effect_count{};
    std::uint64_t haptics_event_count{};
    std::uint64_t dropped_event_count{};
};

class PresentationEventBus {
  public:
    static constexpr std::size_t k_per_kind_capacity = 64;

    void reset() noexcept;
    void clear() noexcept;

    bool publish(CameraEvent event);
    bool publish(ScreenEffectEvent event);
    bool publish(HapticsEvent event);

    [[nodiscard]] std::span<const CameraEvent> camera_events() const noexcept;
    [[nodiscard]] std::span<const ScreenEffectEvent> screen_effects() const noexcept;
    [[nodiscard]] std::span<const HapticsEvent> haptics_events() const noexcept;

    [[nodiscard]] const PresentationEventStatistics& statistics() const noexcept;

  private:
    std::vector<CameraEvent> camera_events_;
    std::vector<ScreenEffectEvent> screen_effects_;
    std::vector<HapticsEvent> haptics_events_;
    PresentationEventStatistics statistics_{};
};

[[nodiscard]] constexpr std::string_view to_string(CameraEventKind kind) noexcept {
    switch (kind) {
    case CameraEventKind::Shake:
        return "shake";
    case CameraEventKind::Kick:
        return "kick";
    case CameraEventKind::FocusPulse:
        return "focus_pulse";
    case CameraEventKind::ZoomPulse:
        return "zoom_pulse";
    case CameraEventKind::LookAt:
        return "look_at";
    case CameraEventKind::Custom:
        return "custom";
    }

    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(ScreenEffectKind kind) noexcept {
    switch (kind) {
    case ScreenEffectKind::Flash:
        return "flash";
    case ScreenEffectKind::Vignette:
        return "vignette";
    case ScreenEffectKind::ColorPulse:
        return "color_pulse";
    case ScreenEffectKind::Shockwave:
        return "shockwave";
    case ScreenEffectKind::Chromatic:
        return "chromatic";
    case ScreenEffectKind::Custom:
        return "custom";
    }

    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(HapticsKind kind) noexcept {
    switch (kind) {
    case HapticsKind::Pulse:
        return "pulse";
    case HapticsKind::Pattern:
        return "pattern";
    case HapticsKind::Cancel:
        return "cancel";
    case HapticsKind::Custom:
        return "custom";
    }

    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(HapticsTarget target) noexcept {
    switch (target) {
    case HapticsTarget::Default:
        return "default";
    case HapticsTarget::ControllerPrimary:
        return "controller_primary";
    case HapticsTarget::ControllerSecondary:
        return "controller_secondary";
    case HapticsTarget::KeyboardLed:
        return "keyboard_led";
    case HapticsTarget::Custom:
        return "custom";
    }

    return "unknown";
}

} // namespace reaktio::gameplay
