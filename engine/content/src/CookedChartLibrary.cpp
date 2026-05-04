#include "reaktio/content/CookedChartLibrary.hpp"

#include "reaktio/foundation/CrashSafeLog.hpp"

#include <algorithm>
#include <sstream>

namespace reaktio::content {

namespace {

void log_message(
    foundation::CrashSafeLog& log,
    foundation::LogLevel level,
    const std::filesystem::path& source_path,
    std::string_view message) {
    std::ostringstream stream;
    stream << message;
    if (!source_path.empty()) {
        stream << " [" << source_path.string() << ']';
    }
    log.write(level, stream.str());
}

} // namespace

bool CookedChartLibrary::load(foundation::CrashSafeLog& log) {
    const std::optional<std::filesystem::path> manifest_path = find_default_cooked_chart_manifest_path();
    if (!manifest_path) {
        clear();
        log.write(foundation::LogLevel::Warning, "No cooked chart manifest was found; continuing without cooked charts.");
        return true;
    }

    return load(*manifest_path, log);
}

bool CookedChartLibrary::load(const std::filesystem::path& manifest_path, foundation::CrashSafeLog& log) {
    clear();

    const std::filesystem::path resolved_manifest_path = std::filesystem::absolute(manifest_path);
    if (!std::filesystem::exists(resolved_manifest_path)) {
        log_message(log, foundation::LogLevel::Error, resolved_manifest_path, "Cooked chart manifest does not exist.");
        return false;
    }

    std::vector<CookedChartManifestRecord> manifest_records;
    std::string error_message;
    if (!load_cooked_chart_manifest(resolved_manifest_path, manifest_records, error_message)) {
        log_message(log, foundation::LogLevel::Error, resolved_manifest_path, error_message);
        return false;
    }

    summary_.loaded_from_manifest = true;
    summary_.manifest_path = resolved_manifest_path;
    for (const CookedChartManifestRecord& manifest_record : manifest_records) {
        ChartDocument document{};
        error_message.clear();
        if (!load_cooked_chart_document(manifest_record.payload_path, document, error_message)) {
            log_message(log, foundation::LogLevel::Error, manifest_record.payload_path, error_message);
            clear();
            return false;
        }

        const ChartDocumentSummary document_summary = summarize_chart_document(document);
        charts_.emplace(
            manifest_record.authoring_id,
            CookedChartRecord{
                .manifest = manifest_record,
                .document = std::move(document),
                .summary = document_summary,
            });
        load_order_.push_back(manifest_record.authoring_id);
        summary_.total_event_count += document_summary.event_count;
        summary_.total_interactive_cue_count += document_summary.interactive_cue_count;
    }

    std::sort(load_order_.begin(), load_order_.end());
    summary_.chart_count = charts_.size();
    return true;
}

void CookedChartLibrary::clear() noexcept {
    charts_.clear();
    load_order_.clear();
    summary_ = {};
}

const CookedChartRecord* CookedChartLibrary::try_get(std::string_view authoring_id) const noexcept {
    const auto it = charts_.find(std::string(authoring_id));
    return it != charts_.end() ? &it->second : nullptr;
}

const CookedChartRecord* CookedChartLibrary::first_chart() const noexcept {
    if (load_order_.empty()) {
        return nullptr;
    }

    return try_get(load_order_.front());
}

const CookedChartLibrarySummary& CookedChartLibrary::summary() const noexcept {
    return summary_;
}

} // namespace reaktio::content