#pragma once

#include "reaktio/content/ChartDataModel.hpp"
#include "reaktio/content/ChartPreview.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace reaktio::foundation {
class CrashSafeLog;
}

namespace reaktio::content {

struct CookedChartRecord {
    CookedChartManifestRecord manifest;
    ChartDocument document;
    ChartDocumentSummary summary;
};

struct CookedChartLibrarySummary {
    bool loaded_from_manifest{};
    std::filesystem::path manifest_path;
    std::size_t chart_count{};
    std::size_t total_event_count{};
    std::size_t total_interactive_cue_count{};
};

class CookedChartLibrary {
  public:
    [[nodiscard]] bool load(foundation::CrashSafeLog& log);
    [[nodiscard]] bool load(const std::filesystem::path& manifest_path, foundation::CrashSafeLog& log);
    void clear() noexcept;

    [[nodiscard]] const CookedChartRecord* try_get(std::string_view authoring_id) const noexcept;
    [[nodiscard]] const CookedChartRecord* first_chart() const noexcept;
    [[nodiscard]] const CookedChartLibrarySummary& summary() const noexcept;

  private:
    std::unordered_map<std::string, CookedChartRecord> charts_;
    std::vector<std::string> load_order_;
    CookedChartLibrarySummary summary_{};
};

} // namespace reaktio::content