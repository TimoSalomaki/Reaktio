#pragma once

#include "reaktio/platform/ApplicationConfig.hpp"
#include "reaktio/render/RenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace reaktio::foundation {
class CrashSafeLog;
}

namespace reaktio::render {

class PostProcessChain {
  public:
    PostProcessChain(const platform::PostProcessConfig& config, foundation::CrashSafeLog& log) noexcept;
    ~PostProcessChain();

    PostProcessChain(const PostProcessChain&) = delete;
    PostProcessChain& operator=(const PostProcessChain&) = delete;

    [[nodiscard]] bool initialize() noexcept;
    void shutdown() noexcept;

    void begin_frame(std::uint16_t backbuffer_width, std::uint16_t backbuffer_height) noexcept;
    void configure_views() const noexcept;
    void submit() noexcept;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::uint16_t pass_count() const noexcept;
    [[nodiscard]] bgfx::FrameBufferHandle scene_frame_buffer() const noexcept;

  private:
    struct Impl;
    Impl* impl_;
};

} // namespace reaktio::render