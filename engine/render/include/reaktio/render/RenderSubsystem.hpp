#pragma once

#include "reaktio/render/RenderTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

namespace reaktio::foundation {
class CrashSafeLog;
class ResourceRegistry;
struct TelemetrySnapshot;
} // namespace reaktio::foundation

namespace reaktio::platform {
struct ApplicationConfig;
struct FrameTiming;
class InputSnapshot;
struct WindowState;
} // namespace reaktio::platform

namespace reaktio::render {

struct RenderFramePackets;

struct RenderStats {
    bool initialized{false};
    bool using_headless_fallback{false};
  bool post_process_enabled{false};
    std::uint16_t backbuffer_width{};
    std::uint16_t backbuffer_height{};
    std::uint16_t view_count{};
  std::uint16_t post_process_pass_count{};
    std::uint32_t draw_calls{};
    std::uint32_t compute_calls{};
    std::uint32_t blit_calls{};
    std::uint32_t transient_vertices{};
    std::uint32_t transient_allocations{};
    std::uint32_t transient_failed_allocations{};
    std::uint32_t instanced_batches{};
    std::uint32_t instanced_instances{};
    std::uint32_t instancing_fallback_batches{};
    std::uint32_t instance_failed_allocations{};
    std::uint32_t loaded_textures{};
    std::uint32_t loaded_meshes{};
    std::uint32_t loaded_fonts{};
    std::size_t loaded_asset_bytes{};
    std::string_view cooked_asset_source{"<none>"};
    std::uint32_t reset_flags{};
    std::string_view renderer_name{"uninitialized"};
};

class RenderSubsystem {
  public:
    RenderSubsystem(const platform::ApplicationConfig& config, foundation::CrashSafeLog& log);
    ~RenderSubsystem();

    RenderSubsystem(const RenderSubsystem&) = delete;
    RenderSubsystem& operator=(const RenderSubsystem&) = delete;

    bool initialize(const platform::WindowState& window_state);
    bool load_cooked_assets(foundation::ResourceRegistry& resource_registry);
    bool load_cooked_assets(
      const std::filesystem::path& manifest_path,
      foundation::ResourceRegistry& resource_registry);
    void begin_frame(const platform::WindowState& window_state);
    void submit_extracted_frame(const RenderFramePackets& packets) noexcept;
    void draw_debug_overlay(
        const platform::FrameTiming& frame_timing,
        const platform::InputSnapshot& input_snapshot,
        const foundation::TelemetrySnapshot* telemetry_snapshot) noexcept;
    void end_frame();

    [[nodiscard]] const RenderStats& stats() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace reaktio::render