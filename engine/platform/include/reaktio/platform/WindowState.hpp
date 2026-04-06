#pragma once

#include "reaktio/platform/ApplicationConfig.hpp"

#include <cstdint>
#include <string_view>

namespace reaktio::platform {

enum class NativeWindowPlatform {
    Unknown,
    Win32,
    Cocoa,
    Wayland,
    X11,
};

struct NativeWindowHandle {
    NativeWindowPlatform platform{NativeWindowPlatform::Unknown};
    void* display_connection{};
    std::uintptr_t primary_handle{};
};

struct WindowState {
    std::uint32_t id{};
    int logical_width{};
    int logical_height{};
    int pixel_width{};
    int pixel_height{};
    float display_scale{1.0f};
    WindowMode mode{WindowMode::Windowed};
    bool resizable{true};
    bool visible{false};
    bool input_focus{false};
    bool close_requested{false};
    NativeWindowHandle native_handle{};
};

[[nodiscard]] inline constexpr std::string_view to_string(NativeWindowPlatform platform) noexcept {
    switch (platform) {
    case NativeWindowPlatform::Unknown:
        return "unknown";
    case NativeWindowPlatform::Win32:
        return "win32";
    case NativeWindowPlatform::Cocoa:
        return "cocoa";
    case NativeWindowPlatform::Wayland:
        return "wayland";
    case NativeWindowPlatform::X11:
        return "x11";
    }

    return "unknown";
}

} // namespace reaktio::platform