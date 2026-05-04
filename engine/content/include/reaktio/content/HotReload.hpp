#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace reaktio::foundation {
class CrashSafeLog;
}

namespace reaktio::content {

enum class HotReloadAssetFamily : std::uint8_t {
    Charts,
    Shaders,
    Materials,
    SelectedContent,
};

enum class HotReloadChangeKind : std::uint8_t {
    Added,
    Modified,
    Removed,
};

struct HotReloadConfig {
    bool enabled{false};
    double poll_interval_seconds{0.50};
    bool watch_charts{true};
    bool watch_shaders{true};
    bool watch_materials{true};
    bool watch_selected_content{true};
    std::filesystem::path chart_manifest_path;
    std::filesystem::path shader_manifest_path;
    std::filesystem::path material_manifest_path;
    std::filesystem::path selected_content_manifest_path;
};

struct HotReloadChange {
    HotReloadAssetFamily family{HotReloadAssetFamily::Charts};
    HotReloadChangeKind kind{HotReloadChangeKind::Modified};
    std::filesystem::path path;
};

struct HotReloadPollResult {
    bool scanned{};
    std::uint64_t revision{};
    std::vector<HotReloadChange> changes;
};

struct HotReloadWatcherSummary {
    bool enabled{};
    std::uint64_t revision{};
    std::size_t watched_chart_file_count{};
    std::size_t watched_shader_file_count{};
    std::size_t watched_material_file_count{};
    std::size_t watched_selected_content_file_count{};
};

class HotReloadWatcher {
  public:
        HotReloadWatcher();
        ~HotReloadWatcher();

        HotReloadWatcher(const HotReloadWatcher&) = delete;
        HotReloadWatcher& operator=(const HotReloadWatcher&) = delete;
        HotReloadWatcher(HotReloadWatcher&&) noexcept;
        HotReloadWatcher& operator=(HotReloadWatcher&&) noexcept;

    [[nodiscard]] bool configure(const HotReloadConfig& config, foundation::CrashSafeLog& log);
    [[nodiscard]] HotReloadPollResult poll(double delta_seconds, foundation::CrashSafeLog& log);
    void clear() noexcept;

    [[nodiscard]] const HotReloadConfig& config() const noexcept;
    [[nodiscard]] const HotReloadWatcherSummary& summary() const noexcept;

  private:
    struct Impl;
        std::unique_ptr<Impl> impl_;
};

[[nodiscard]] constexpr std::string_view to_string(HotReloadAssetFamily family) noexcept {
    switch (family) {
    case HotReloadAssetFamily::Charts:
        return "charts";
    case HotReloadAssetFamily::Shaders:
        return "shaders";
    case HotReloadAssetFamily::Materials:
        return "materials";
    case HotReloadAssetFamily::SelectedContent:
        return "selected-content";
    }

    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(HotReloadChangeKind kind) noexcept {
    switch (kind) {
    case HotReloadChangeKind::Added:
        return "added";
    case HotReloadChangeKind::Modified:
        return "modified";
    case HotReloadChangeKind::Removed:
        return "removed";
    }

    return "unknown";
}

} // namespace reaktio::content