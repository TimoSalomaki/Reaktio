#pragma once

#include <cstdint>

namespace reaktio::render {

enum class RenderView : std::uint16_t {
    MainScene = 0,
    DebugOverlay = 1,
    Count = 2,
};

[[nodiscard]] constexpr std::uint16_t to_view_id(RenderView view) noexcept {
    return static_cast<std::uint16_t>(view);
}

} // namespace reaktio::render