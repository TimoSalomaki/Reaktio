#pragma once

#include "reaktio/content/ContentPipeline.hpp"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace reaktio::content {

struct ContentValidationOptions {
    ContentRootPaths paths;
    bool validate_charts{true};
    bool validate_timing{true};
    bool validate_missing_assets{true};
};

struct ContentValidationSummary {
    std::filesystem::path raw_root;
    std::filesystem::path cooked_root;
    std::filesystem::path chart_manifest_path;
    std::filesystem::path audio_manifest_path;
    std::filesystem::path texture_manifest_path;
    std::filesystem::path mesh_manifest_path;
    std::filesystem::path font_manifest_path;
    std::size_t chart_manifest_entry_count{};
    std::size_t validated_chart_count{};
    std::size_t total_chart_events{};
    std::size_t total_interactive_cues{};
    std::size_t referenced_audio_clip_count{};
    std::size_t audio_clip_count{};
    std::size_t missing_audio_clip_reference_count{};
    std::size_t render_manifest_count{};
    std::size_t render_asset_count{};
    std::size_t warning_count{};
    std::size_t error_count{};
};

class ContentValidator {
  public:
    [[nodiscard]] bool validate_all(const ContentValidationOptions& options, foundation::CrashSafeLog& log);
    void clear() noexcept;

    [[nodiscard]] const std::vector<ContentCookIssue>& issues() const noexcept;
    [[nodiscard]] const ContentValidationSummary& summary() const noexcept;

  private:
    std::vector<ContentCookIssue> issues_;
    ContentValidationSummary summary_{};
};

} // namespace reaktio::content