#pragma once

#include <cstdint>

namespace reaktio::render {

enum class RenderView : std::uint16_t {
    MainScene = 0,
    PostBloomExtract = 1,
    PostBloomBlurHorizontal = 2,
    PostBloomBlurVertical = 3,
    PostComposite = 4,
    PostPresent = 5,
    DebugOverlay = 6,
    Count = 7,
};

[[nodiscard]] constexpr std::uint16_t to_view_id(RenderView view) noexcept {
    return static_cast<std::uint16_t>(view);
}

} // namespace reaktio::render