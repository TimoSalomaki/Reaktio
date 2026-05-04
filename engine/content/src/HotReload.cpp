#include "reaktio/content/HotReload.hpp"

#include "reaktio/content/ChartPreview.hpp"
#include "reaktio/foundation/CrashSafeLog.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace reaktio::content {

namespace {

struct ParsedKeyValue {
    std::string value;
    std::size_t line{};
};

using SectionValues = std::unordered_map<std::string, ParsedKeyValue>;
using SectionMap = std::unordered_map<std::string, SectionValues>;

struct FileSnapshot {
    bool exists{};
    std::uintmax_t size{};
    std::filesystem::file_time_type write_time{};
};

struct FamilyWatchState {
    HotReloadAssetFamily family{HotReloadAssetFamily::Charts};
    bool enabled{};
    std::filesystem::path root_path;
    std::unordered_map<std::filesystem::path, FileSnapshot> snapshots;
};

std::string trim_copy(std::string_view value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(start, end - start));
}

std::vector<std::string> split_string(std::string_view value, char delimiter) {
    std::vector<std::string> tokens;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find(delimiter, start);
        const std::string token = trim_copy(
            end == std::string_view::npos ? value.substr(start) : value.substr(start, end - start));
        if (!token.empty()) {
            tokens.push_back(token);
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return tokens;
}

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

std::filesystem::path resolve_default_path(std::string_view relative_path) {
    std::filesystem::path current = std::filesystem::current_path();
    while (true) {
        const std::filesystem::path candidate = current / std::filesystem::path(relative_path);
        if (std::filesystem::exists(candidate)) {
            return std::filesystem::absolute(candidate);
        }

        const std::filesystem::path parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }

    return std::filesystem::absolute(std::filesystem::current_path() / std::filesystem::path(relative_path));
}

std::optional<std::string> read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

SectionMap parse_sections(const std::string& text) {
    SectionMap sections;
    std::istringstream lines{text};
    std::string raw_line;
    std::string current_section;
    std::size_t line_number = 0;

    while (std::getline(lines, raw_line)) {
        ++line_number;
        std::string line = trim_copy(raw_line);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty() || line.front() == '#' || line.front() == ';') {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            current_section = trim_copy(std::string_view(line).substr(1, line.size() - 2));
            continue;
        }

        if (current_section.empty()) {
            continue;
        }

        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        sections[current_section][trim_copy(std::string_view(line).substr(0, separator))] = ParsedKeyValue{
            .value = trim_copy(std::string_view(line).substr(separator + 1)),
            .line = line_number,
        };
    }

    return sections;
}

void append_unique_path(std::vector<std::filesystem::path>& paths, const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }

    const std::filesystem::path resolved = std::filesystem::absolute(path);
    if (std::find(paths.begin(), paths.end(), resolved) == paths.end()) {
        paths.push_back(resolved);
    }
}

FileSnapshot capture_snapshot(const std::filesystem::path& path) {
    FileSnapshot snapshot{};
    std::error_code error_code;
    snapshot.exists = std::filesystem::exists(path, error_code) && !error_code;
    if (!snapshot.exists) {
        return snapshot;
    }

    snapshot.size = std::filesystem::is_regular_file(path, error_code)
        ? std::filesystem::file_size(path, error_code)
        : 0u;
    if (error_code) {
        snapshot.size = 0u;
        error_code.clear();
    }
    snapshot.write_time = std::filesystem::last_write_time(path, error_code);
    if (error_code) {
        snapshot.write_time = {};
    }
    return snapshot;
}

bool snapshots_equal(const FileSnapshot& lhs, const FileSnapshot& rhs) noexcept {
    return lhs.exists == rhs.exists && (!lhs.exists || (lhs.size == rhs.size && lhs.write_time == rhs.write_time));
}

void collect_generic_manifest_paths(const std::filesystem::path& manifest_path, std::vector<std::filesystem::path>& paths) {
    append_unique_path(paths, manifest_path);
    if (!std::filesystem::exists(manifest_path)) {
        return;
    }

    const std::optional<std::string> text = read_text_file(manifest_path);
    if (!text) {
        return;
    }

    const SectionMap sections = parse_sections(*text);
    const std::filesystem::path manifest_directory = std::filesystem::absolute(manifest_path).parent_path();
    for (const auto& [section_name, values] : sections) {
        if (section_name == "meta") {
            continue;
        }
        if (const auto payload_it = values.find("payload"); payload_it != values.end() && !payload_it->second.value.empty()) {
            append_unique_path(paths, manifest_directory / payload_it->second.value);
        }
        if (const auto dependencies_it = values.find("dependencies"); dependencies_it != values.end()) {
            for (const std::string& dependency : split_string(dependencies_it->second.value, ',')) {
                append_unique_path(paths, manifest_directory / dependency);
            }
        }
    }
}

void collect_shader_manifest_paths(const std::filesystem::path& manifest_path, std::vector<std::filesystem::path>& paths) {
    append_unique_path(paths, manifest_path);
    if (!std::filesystem::exists(manifest_path)) {
        return;
    }

    const std::optional<std::string> text = read_text_file(manifest_path);
    if (!text) {
        return;
    }

    const SectionMap sections = parse_sections(*text);
    const std::filesystem::path manifest_directory = std::filesystem::absolute(manifest_path).parent_path();
    for (const auto& [section_name, values] : sections) {
        if (section_name == "meta") {
            continue;
        }
        for (const auto& [key, parsed] : values) {
            if (key.rfind("vertex.", 0) == 0 || key.rfind("fragment.", 0) == 0) {
                append_unique_path(paths, manifest_directory / parsed.value);
            }
        }
    }
}

void update_family_state(
    FamilyWatchState& state,
    std::vector<std::filesystem::path>& expected_paths,
    std::vector<HotReloadChange>& changes) {
    std::unordered_map<std::filesystem::path, FileSnapshot> next_snapshots;
    next_snapshots.reserve(expected_paths.size());
    for (const std::filesystem::path& path : expected_paths) {
        next_snapshots.emplace(path, capture_snapshot(path));
    }

    for (const auto& [path, previous] : state.snapshots) {
        const auto next_it = next_snapshots.find(path);
        if (next_it == next_snapshots.end()) {
            changes.push_back(HotReloadChange{
                .family = state.family,
                .kind = HotReloadChangeKind::Removed,
                .path = path,
            });
            continue;
        }

        const FileSnapshot& current = next_it->second;
        if (!snapshots_equal(previous, current)) {
            changes.push_back(HotReloadChange{
                .family = state.family,
                .kind = !previous.exists && current.exists ? HotReloadChangeKind::Added
                      : previous.exists && !current.exists ? HotReloadChangeKind::Removed
                                                           : HotReloadChangeKind::Modified,
                .path = path,
            });
        }
    }

    for (const auto& [path, current] : next_snapshots) {
        if (state.snapshots.contains(path)) {
            continue;
        }
        if (current.exists) {
            changes.push_back(HotReloadChange{
                .family = state.family,
                .kind = HotReloadChangeKind::Added,
                .path = path,
            });
        }
    }

    state.snapshots = std::move(next_snapshots);
}

} // namespace

struct HotReloadWatcher::Impl {
    HotReloadConfig config{};
    HotReloadWatcherSummary summary{};
    double accumulated_seconds{};
    FamilyWatchState charts{.family = HotReloadAssetFamily::Charts};
    FamilyWatchState shaders{.family = HotReloadAssetFamily::Shaders};
    FamilyWatchState materials{.family = HotReloadAssetFamily::Materials};
    FamilyWatchState selected_content{.family = HotReloadAssetFamily::SelectedContent};
};

HotReloadWatcher::HotReloadWatcher()
    : impl_(std::make_unique<Impl>()) {}

HotReloadWatcher::~HotReloadWatcher() = default;

HotReloadWatcher::HotReloadWatcher(HotReloadWatcher&&) noexcept = default;

HotReloadWatcher& HotReloadWatcher::operator=(HotReloadWatcher&&) noexcept = default;

bool HotReloadWatcher::configure(const HotReloadConfig& config, foundation::CrashSafeLog& log) {
    clear();

    impl_->config = config;
    if (impl_->config.watch_charts && impl_->config.chart_manifest_path.empty()) {
        if (const std::optional<std::filesystem::path> chart_manifest = find_default_cooked_chart_manifest_path(); chart_manifest) {
            impl_->config.chart_manifest_path = *chart_manifest;
        } else {
            impl_->config.chart_manifest_path = resolve_default_path("content/cooked/charts/manifest.ini");
        }
    }
    if (impl_->config.watch_shaders && impl_->config.shader_manifest_path.empty()) {
        impl_->config.shader_manifest_path = resolve_default_path("content/cooked/render/shaders/manifest.ini");
    }
    if (impl_->config.watch_materials && impl_->config.material_manifest_path.empty()) {
        impl_->config.material_manifest_path = resolve_default_path("content/raw/render/materials/manifest.ini");
    }
    if (impl_->config.watch_selected_content && impl_->config.selected_content_manifest_path.empty()) {
        impl_->config.selected_content_manifest_path = resolve_default_path("content/cooked/render/manifest.ini");
    }

    impl_->summary.enabled = impl_->config.enabled;
    impl_->charts.enabled = impl_->config.enabled && impl_->config.watch_charts;
    impl_->charts.root_path = impl_->config.chart_manifest_path;
    impl_->shaders.enabled = impl_->config.enabled && impl_->config.watch_shaders;
    impl_->shaders.root_path = impl_->config.shader_manifest_path;
    impl_->materials.enabled = impl_->config.enabled && impl_->config.watch_materials;
    impl_->materials.root_path = impl_->config.material_manifest_path;
    impl_->selected_content.enabled = impl_->config.enabled && impl_->config.watch_selected_content;
    impl_->selected_content.root_path = impl_->config.selected_content_manifest_path;

    std::vector<HotReloadChange> ignored_changes;
    std::vector<std::filesystem::path> paths;
    if (impl_->charts.enabled) {
        paths.clear();
        collect_generic_manifest_paths(impl_->charts.root_path, paths);
        update_family_state(impl_->charts, paths, ignored_changes);
        impl_->summary.watched_chart_file_count = impl_->charts.snapshots.size();
    }
    if (impl_->shaders.enabled) {
        paths.clear();
        collect_shader_manifest_paths(impl_->shaders.root_path, paths);
        update_family_state(impl_->shaders, paths, ignored_changes);
        impl_->summary.watched_shader_file_count = impl_->shaders.snapshots.size();
    }
    if (impl_->materials.enabled) {
        paths.clear();
        collect_generic_manifest_paths(impl_->materials.root_path, paths);
        if (paths.empty()) {
            append_unique_path(paths, impl_->materials.root_path);
        }
        update_family_state(impl_->materials, paths, ignored_changes);
        impl_->summary.watched_material_file_count = impl_->materials.snapshots.size();
    }
    if (impl_->selected_content.enabled) {
        paths.clear();
        collect_generic_manifest_paths(impl_->selected_content.root_path, paths);
        update_family_state(impl_->selected_content, paths, ignored_changes);
        impl_->summary.watched_selected_content_file_count = impl_->selected_content.snapshots.size();
    }

    if (impl_->config.enabled) {
        std::ostringstream stream;
        stream << "Configured content hot reload: charts=" << impl_->summary.watched_chart_file_count
               << " shaders=" << impl_->summary.watched_shader_file_count
               << " materials=" << impl_->summary.watched_material_file_count
               << " selected=" << impl_->summary.watched_selected_content_file_count
               << " poll=" << impl_->config.poll_interval_seconds << "s";
        log.write(foundation::LogLevel::Info, stream.str());
    }

    return true;
}

HotReloadPollResult HotReloadWatcher::poll(double delta_seconds, foundation::CrashSafeLog& log) {
    HotReloadPollResult result{};
    if (!impl_->config.enabled) {
        return result;
    }

    impl_->accumulated_seconds += std::max(0.0, delta_seconds);
    if (impl_->accumulated_seconds < impl_->config.poll_interval_seconds) {
        return result;
    }

    impl_->accumulated_seconds = 0.0;
    result.scanned = true;

    std::vector<std::filesystem::path> paths;
    if (impl_->charts.enabled) {
        paths.clear();
        collect_generic_manifest_paths(impl_->charts.root_path, paths);
        update_family_state(impl_->charts, paths, result.changes);
        impl_->summary.watched_chart_file_count = impl_->charts.snapshots.size();
    }
    if (impl_->shaders.enabled) {
        paths.clear();
        collect_shader_manifest_paths(impl_->shaders.root_path, paths);
        update_family_state(impl_->shaders, paths, result.changes);
        impl_->summary.watched_shader_file_count = impl_->shaders.snapshots.size();
    }
    if (impl_->materials.enabled) {
        paths.clear();
        collect_generic_manifest_paths(impl_->materials.root_path, paths);
        if (paths.empty()) {
            append_unique_path(paths, impl_->materials.root_path);
        }
        update_family_state(impl_->materials, paths, result.changes);
        impl_->summary.watched_material_file_count = impl_->materials.snapshots.size();
    }
    if (impl_->selected_content.enabled) {
        paths.clear();
        collect_generic_manifest_paths(impl_->selected_content.root_path, paths);
        update_family_state(impl_->selected_content, paths, result.changes);
        impl_->summary.watched_selected_content_file_count = impl_->selected_content.snapshots.size();
    }

    if (!result.changes.empty()) {
        ++impl_->summary.revision;
        result.revision = impl_->summary.revision;
        std::ostringstream stream;
        stream << "Detected content changes for hot reload: count=" << result.changes.size()
               << " rev=" << result.revision;
        log.write(foundation::LogLevel::Info, stream.str());
    } else {
        result.revision = impl_->summary.revision;
    }

    return result;
}

void HotReloadWatcher::clear() noexcept {
    if (!impl_) {
        return;
    }

    impl_->config = {};
    impl_->summary = {};
    impl_->accumulated_seconds = 0.0;
    impl_->charts = FamilyWatchState{.family = HotReloadAssetFamily::Charts};
    impl_->shaders = FamilyWatchState{.family = HotReloadAssetFamily::Shaders};
    impl_->materials = FamilyWatchState{.family = HotReloadAssetFamily::Materials};
    impl_->selected_content = FamilyWatchState{.family = HotReloadAssetFamily::SelectedContent};
}

const HotReloadConfig& HotReloadWatcher::config() const noexcept {
    return impl_->config;
}

const HotReloadWatcherSummary& HotReloadWatcher::summary() const noexcept {
    return impl_->summary;
}

} // namespace reaktio::content