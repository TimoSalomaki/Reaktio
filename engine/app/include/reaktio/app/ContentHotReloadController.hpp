#pragma once

#include "reaktio/content/HotReload.hpp"

#include <cstddef>
#include <cstdint>

namespace reaktio::content {
class CookedChartLibrary;
}

namespace reaktio::foundation {
class CrashSafeLog;
class ResourceRegistry;
}

namespace reaktio::gameplay {
class EventBus;
}

namespace reaktio::render {
class RenderSubsystem;
}

namespace reaktio::app {

struct ContentHotReloadControllerSummary {
    bool enabled{};
    std::uint64_t revision{};
    std::size_t applied_reload_count{};
    std::size_t pending_reload_count{};
    std::size_t failed_reload_count{};
};

class ContentHotReloadController {
  public:
    explicit ContentHotReloadController(content::HotReloadConfig config);

    [[nodiscard]] bool initialize(foundation::CrashSafeLog& log);
    void tick(
        double delta_seconds,
        std::uint64_t frame_index,
        std::uint64_t simulation_step,
        foundation::ResourceRegistry& resource_registry,
        render::RenderSubsystem& render_subsystem,
        content::CookedChartLibrary& cooked_chart_library,
        gameplay::EventBus& event_bus,
        foundation::CrashSafeLog& log);

    [[nodiscard]] const ContentHotReloadControllerSummary& summary() const noexcept;
    [[nodiscard]] const content::HotReloadWatcherSummary& watcher_summary() const noexcept;
    [[nodiscard]] const content::HotReloadConfig& config() const noexcept;

  private:
    content::HotReloadWatcher watcher_;
    ContentHotReloadControllerSummary summary_{};
    content::HotReloadConfig config_{};
};

} // namespace reaktio::app