#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace reaktio::foundation {
class CrashSafeLog;
}

namespace reaktio::content {

enum class ContentCookIssueSeverity : std::uint8_t {
    Warning,
    Error,
};

struct ContentCookIssue {
    ContentCookIssueSeverity severity{ContentCookIssueSeverity::Error};
    std::filesystem::path source_path;
    std::size_t line{};
    std::string message;
};

struct ContentRootPaths {
    std::filesystem::path raw_root;
    std::filesystem::path cooked_root;
};

struct ContentCookSummary {
    std::filesystem::path raw_root;
    std::filesystem::path cooked_root;
    std::filesystem::path chart_manifest_source_path;
    std::filesystem::path chart_manifest_output_path;
    std::filesystem::path render_manifest_output_path;
    std::size_t authored_chart_count{};
    std::size_t cooked_chart_count{};
    std::size_t total_chart_events{};
    std::size_t total_interactive_cues{};
    std::size_t cooked_render_asset_count{};
    std::size_t cooked_texture_count{};
    std::size_t cooked_mesh_count{};
    std::size_t cooked_font_count{};
};

class ContentPipeline {
  public:
    [[nodiscard]] bool cook_all(const ContentRootPaths& paths, foundation::CrashSafeLog& log);
    void clear() noexcept;

    [[nodiscard]] const std::vector<ContentCookIssue>& issues() const noexcept;
    [[nodiscard]] const ContentCookSummary& summary() const noexcept;
  private:
    std::vector<ContentCookIssue> issues_;
    ContentCookSummary summary_{};
};

[[nodiscard]] std::optional<ContentRootPaths> find_default_content_roots();
[[nodiscard]] std::string_view to_string(ContentCookIssueSeverity severity) noexcept;

} // namespace reaktio::content