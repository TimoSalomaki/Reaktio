#include "reaktio/content/ContentPipeline.hpp"

#include "reaktio/content/ChartDataModel.hpp"
#include "reaktio/content/ContentValidation.hpp"
#include "reaktio/foundation/CrashSafeLog.hpp"
#include "reaktio/rhythm/TempoMap.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cinttypes>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace reaktio::content {

namespace {

struct ParsedKeyValue {
    std::string value;
    std::size_t line{};
};

using SectionValues = std::unordered_map<std::string, ParsedKeyValue>;
using SectionMap = std::unordered_map<std::string, SectionValues>;

struct AuthoredChartManifestEntry {
    std::string authoring_id;
    std::string runtime_label;
    std::filesystem::path source_path;
};

struct AuthoredAudioClipManifestEntry {
    std::string authoring_id;
    std::string runtime_label;
    std::filesystem::path source_path;
};

struct CookedChartManifestEntry {
    std::string authoring_id;
    std::string runtime_label;
    std::filesystem::path source_path;
    std::filesystem::path payload_path;
    std::string payload_hash;
};

struct CookedChartDocumentRecord {
    CookedChartManifestEntry manifest_entry;
    ChartDocument document;
    ChartDocumentSummary summary;
};

struct ManifestDependencyRecord {
    std::filesystem::path relative_path;
    std::string hash;
};

enum class RenderAssetKind : std::uint8_t {
    Texture,
    Mesh,
    Font,
};

struct AuthoredRenderManifestEntry {
    RenderAssetKind kind{RenderAssetKind::Texture};
    std::string authoring_id;
    std::string runtime_label;
    std::filesystem::path source_path;
    std::filesystem::path payload_path;
};

struct CookedRenderManifestEntry {
    RenderAssetKind kind{RenderAssetKind::Texture};
    std::string authoring_id;
    std::string runtime_label;
    std::filesystem::path source_relative_path;
    std::string source_hash;
    std::filesystem::path payload_relative_path;
    std::string payload_hash;
    std::vector<ManifestDependencyRecord> dependencies;
};

constexpr std::string_view k_raw_chart_manifest_relative_path = "charts/manifest.ini";
constexpr std::string_view k_raw_audio_manifest_relative_path = "audio/manifest.ini";
constexpr std::string_view k_cooked_chart_manifest_relative_path = "charts/manifest.ini";
constexpr std::string_view k_authoring_chart_schema = "reaktio.chart.v1";
constexpr std::string_view k_cooked_chart_schema = "reaktio.cooked.chart.v1";
constexpr std::string_view k_cooked_chart_manifest_schema = "reaktio.cooked.chart_manifest.v1";
constexpr std::string_view k_cooked_render_manifest_schema = "reaktio.cooked.render_asset_manifest.v1";
constexpr std::string_view k_raw_texture_manifest_relative_path = "render/textures/manifest.ini";
constexpr std::string_view k_raw_mesh_manifest_relative_path = "render/meshes/manifest.ini";
constexpr std::string_view k_raw_font_manifest_relative_path = "render/fonts/manifest.ini";
constexpr std::string_view k_cooked_render_manifest_relative_path = "render/manifest.ini";
constexpr std::string_view k_manifest_generator = "reaktio_content_cooker";
constexpr std::uint64_t k_fnv1a64_offset_basis = 14695981039346656037ull;
constexpr std::uint64_t k_fnv1a64_prime = 1099511628211ull;

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

std::string lowercase_copy(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const unsigned char character : value) {
        lowered.push_back(static_cast<char>(std::tolower(character)));
    }
    return lowered;
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

std::string sanitize_file_stem(std::string_view value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (const unsigned char character : value) {
        if (std::isalnum(character) != 0) {
            sanitized.push_back(static_cast<char>(std::tolower(character)));
        } else {
            sanitized.push_back('-');
        }
    }

    while (!sanitized.empty() && sanitized.back() == '-') {
        sanitized.pop_back();
    }
    while (!sanitized.empty() && sanitized.front() == '-') {
        sanitized.erase(sanitized.begin());
    }

    return sanitized.empty() ? std::string("chart") : sanitized;
}

std::string join_strings(std::span<const std::string> values, std::string_view delimiter) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            stream << delimiter;
        }
        stream << values[index];
    }
    return stream.str();
}

std::optional<std::vector<std::uint8_t>> read_binary_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }

    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        return std::nullopt;
    }

    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!input) {
            return std::nullopt;
        }
    }

    return bytes;
}

std::uint64_t fnv1a64(std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t hash = k_fnv1a64_offset_basis;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= k_fnv1a64_prime;
    }
    return hash;
}

std::string format_hash(std::uint64_t hash) {
    std::ostringstream stream;
    stream << "fnv1a64:" << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

std::optional<std::string> hash_file(const std::filesystem::path& path) {
    const std::optional<std::vector<std::uint8_t>> bytes = read_binary_file(path);
    if (!bytes) {
        return std::nullopt;
    }
    return format_hash(fnv1a64(*bytes));
}

std::string hash_text(std::string_view text) {
    const auto* begin = reinterpret_cast<const std::uint8_t*>(text.data());
    return format_hash(fnv1a64(std::span<const std::uint8_t>(begin, text.size())));
}

std::filesystem::path make_relative_path(const std::filesystem::path& path, const std::filesystem::path& base_path) {
    std::error_code error_code;
    const std::filesystem::path relative = std::filesystem::relative(path, base_path, error_code);
    return error_code ? path : relative;
}

std::string path_to_manifest_string(const std::filesystem::path& path, const std::filesystem::path& base_path) {
    return make_relative_path(path, base_path).generic_string();
}

std::string serialize_dependency_paths(std::span<const ManifestDependencyRecord> dependencies) {
    std::vector<std::string> values;
    values.reserve(dependencies.size());
    for (const ManifestDependencyRecord& dependency : dependencies) {
        values.push_back(dependency.relative_path.generic_string());
    }
    return join_strings(values, ",");
}

std::string serialize_dependency_hashes(std::span<const ManifestDependencyRecord> dependencies) {
    std::vector<std::string> values;
    values.reserve(dependencies.size());
    for (const ManifestDependencyRecord& dependency : dependencies) {
        values.push_back(dependency.relative_path.generic_string() + "=" + dependency.hash);
    }
    return join_strings(values, ",");
}

std::string_view to_string(RenderAssetKind kind) noexcept {
    switch (kind) {
    case RenderAssetKind::Texture:
        return "texture";
    case RenderAssetKind::Mesh:
        return "mesh";
    case RenderAssetKind::Font:
        return "font";
    }

    return "unknown";
}

std::string_view render_manifest_prefix(RenderAssetKind kind) noexcept {
    switch (kind) {
    case RenderAssetKind::Texture:
        return "texture.";
    case RenderAssetKind::Mesh:
        return "mesh.";
    case RenderAssetKind::Font:
        return "font.";
    }

    return "";
}

std::string_view render_metadata_section(RenderAssetKind kind) noexcept {
    switch (kind) {
    case RenderAssetKind::Texture:
        return "texture";
    case RenderAssetKind::Mesh:
        return "mesh";
    case RenderAssetKind::Font:
        return "font";
    }

    return "";
}

std::string_view render_metadata_dependency_key(RenderAssetKind kind) noexcept {
    switch (kind) {
    case RenderAssetKind::Texture:
        return "payload";
    case RenderAssetKind::Mesh:
        return "payload";
    case RenderAssetKind::Font:
        return "atlas_payload";
    }

    return "";
}

std::string_view cooked_render_subdirectory(RenderAssetKind kind) noexcept {
    switch (kind) {
    case RenderAssetKind::Texture:
        return "textures";
    case RenderAssetKind::Mesh:
        return "meshes";
    case RenderAssetKind::Font:
        return "fonts";
    }

    return "";
}

std::string_view cooked_render_payload_extension(RenderAssetKind kind) noexcept {
    switch (kind) {
    case RenderAssetKind::Texture:
        return ".texture.ini";
    case RenderAssetKind::Mesh:
        return ".mesh.ini";
    case RenderAssetKind::Font:
        return ".font.ini";
    }

    return ".ini";
}

bool is_auxiliary_render_section(RenderAssetKind kind, std::string_view section_name) noexcept {
    if (section_name == "meta") {
        return true;
    }

    if (kind == RenderAssetKind::Font) {
        return section_name.rfind("charset.", 0) == 0 || section_name.rfind("fallback.", 0) == 0;
    }

    return false;
}

void append_issue(
    std::vector<ContentCookIssue>& issues,
    foundation::CrashSafeLog& log,
    ContentCookIssueSeverity severity,
    const std::filesystem::path& source_path,
    std::size_t line,
    std::string message) {
    issues.push_back(ContentCookIssue{
        .severity = severity,
        .source_path = source_path,
        .line = line,
        .message = std::move(message),
    });

    std::ostringstream stream;
    stream << issues.back().message;
    if (!source_path.empty()) {
        stream << " [" << source_path.string();
        if (line > 0) {
            stream << ':' << line;
        }
        stream << ']';
    }
    log.write(
        severity == ContentCookIssueSeverity::Error ? foundation::LogLevel::Error : foundation::LogLevel::Warning,
        stream.str());
}

std::optional<std::string> try_get_environment_value(std::string_view name) {
#if defined(_WIN32)
    char* buffer = nullptr;
    std::size_t length = 0;
    const std::string variable_name(name);
    if (_dupenv_s(&buffer, &length, variable_name.c_str()) != 0 || buffer == nullptr || length == 0) {
        if (buffer != nullptr) {
            std::free(buffer);
        }
        return std::nullopt;
    }

    std::string value(buffer);
    std::free(buffer);
    return value.empty() ? std::nullopt : std::optional<std::string>{std::move(value)};
#else
    const std::string variable_name(name);
    if (const char* value = std::getenv(variable_name.c_str()); value != nullptr && value[0] != '\0') {
        return std::string(value);
    }
    return std::nullopt;
#endif
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

bool write_text_file(const std::filesystem::path& path, std::string_view text) {
    std::error_code error_code;
    std::filesystem::create_directories(path.parent_path(), error_code);
    if (error_code) {
        return false;
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }

    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good();
}

SectionMap parse_sections(
    std::string_view text,
    const std::filesystem::path& source_path,
    foundation::CrashSafeLog& log,
    std::vector<ContentCookIssue>& issues,
    bool& fatal_error) {
    SectionMap sections;
    std::istringstream lines{std::string(text)};
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
            if (current_section.empty()) {
                fatal_error = true;
                append_issue(issues, log, ContentCookIssueSeverity::Error, source_path, line_number, "Encountered an empty section name.");
            }
            continue;
        }

        if (current_section.empty()) {
            fatal_error = true;
            append_issue(issues, log, ContentCookIssueSeverity::Error, source_path, line_number, "Entry appeared before a section header.");
            continue;
        }

        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            fatal_error = true;
            append_issue(issues, log, ContentCookIssueSeverity::Error, source_path, line_number, "Entry is missing '='.");
            continue;
        }

        const std::string key = trim_copy(std::string_view(line).substr(0, separator));
        const std::string value = trim_copy(std::string_view(line).substr(separator + 1));
        if (key.empty()) {
            fatal_error = true;
            append_issue(issues, log, ContentCookIssueSeverity::Error, source_path, line_number, "Entry key is empty.");
            continue;
        }

        auto& section = sections[current_section];
        if (section.contains(key)) {
            append_issue(issues, log, ContentCookIssueSeverity::Warning, source_path, line_number, "Duplicate key in section; last value wins.");
        }
        section[key] = ParsedKeyValue{.value = value, .line = line_number};
    }

    return sections;
}

const ParsedKeyValue* find_value(
    const SectionMap& sections,
    std::string_view section_name,
    std::string_view key_name) noexcept {
    const auto section_it = sections.find(std::string(section_name));
    if (section_it == sections.end()) {
        return nullptr;
    }

    const auto value_it = section_it->second.find(std::string(key_name));
    return value_it != section_it->second.end() ? &value_it->second : nullptr;
}

bool try_parse_int64(std::string_view value, std::int64_t& parsed) noexcept {
    const std::string buffer = trim_copy(value);
    if (buffer.empty()) {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const long long converted = std::strtoll(buffer.c_str(), &end, 0);
    if (errno == ERANGE || end == nullptr || *end != '\0') {
        return false;
    }

    parsed = static_cast<std::int64_t>(converted);
    return true;
}

bool try_parse_uint32(std::string_view value, std::uint32_t& parsed) noexcept {
    const std::string buffer = trim_copy(value);
    if (buffer.empty() || buffer.front() == '-') {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const unsigned long converted = std::strtoul(buffer.c_str(), &end, 0);
    if (errno == ERANGE || end == nullptr || *end != '\0' || converted > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    parsed = static_cast<std::uint32_t>(converted);
    return true;
}

bool try_parse_double(std::string_view value, double& parsed) noexcept {
    const std::string buffer = trim_copy(value);
    if (buffer.empty()) {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    parsed = std::strtod(buffer.c_str(), &end);
    return errno != ERANGE && end != nullptr && *end == '\0';
}

template <typename Integer>
bool try_parse_integer(std::string_view value, Integer& parsed) noexcept {
    std::int64_t converted = 0;
    if (!try_parse_int64(value, converted) ||
        converted < static_cast<std::int64_t>(std::numeric_limits<Integer>::min()) ||
        converted > static_cast<std::int64_t>(std::numeric_limits<Integer>::max())) {
        return false;
    }

    parsed = static_cast<Integer>(converted);
    return true;
}

std::vector<AuthoredField> collect_unknown_fields(
    const SectionValues& values,
    std::span<const std::string_view> known_keys) {
    std::vector<AuthoredField> fields;
    for (const auto& [key, parsed] : values) {
        const bool known = std::find(known_keys.begin(), known_keys.end(), std::string_view(key)) != known_keys.end();
        if (!known) {
            fields.push_back(AuthoredField{.key = key, .value = parsed.value});
        }
    }
    std::sort(fields.begin(), fields.end(), [](const AuthoredField& lhs, const AuthoredField& rhs) {
        return lhs.key < rhs.key;
    });
    return fields;
}

bool add_unique_id(
    std::unordered_set<std::string>& ids,
    std::string_view id,
    const std::filesystem::path& source_path,
    foundation::CrashSafeLog& log,
    std::vector<ContentCookIssue>& issues,
    std::size_t line,
    std::string_view kind) {
    if (!ids.insert(std::string(id)).second) {
        append_issue(
            issues,
            log,
            ContentCookIssueSeverity::Error,
            source_path,
            line,
            std::string("Duplicate ") + std::string(kind) + " id '" + std::string(id) + "'.");
        return false;
    }
    return true;
}

std::optional<AuthoredChartManifestEntry> parse_chart_manifest_entry(
    std::string_view section_name,
    const SectionValues& values,
    const std::filesystem::path& manifest_directory,
    const std::filesystem::path& manifest_path,
    foundation::CrashSafeLog& log,
    std::vector<ContentCookIssue>& issues) {
    if (section_name == "meta") {
        return std::nullopt;
    }

    if (section_name.rfind("chart.", 0) != 0) {
        append_issue(issues, log, ContentCookIssueSeverity::Warning, manifest_path, 0, "Unknown chart manifest section was ignored.");
        return std::nullopt;
    }

    const auto runtime_label_it = values.find("runtime_label");
    const auto source_it = values.find("source");
    if (runtime_label_it == values.end() || source_it == values.end()) {
        append_issue(issues, log, ContentCookIssueSeverity::Error, manifest_path, 0, "Chart manifest section is missing runtime_label or source.");
        return std::nullopt;
    }

    const std::string authoring_id = std::string(section_name.substr(std::string_view("chart.").size()));
    const std::filesystem::path source_path = std::filesystem::absolute(manifest_directory / source_it->second.value);
    return AuthoredChartManifestEntry{
        .authoring_id = authoring_id,
        .runtime_label = runtime_label_it->second.value,
        .source_path = source_path,
    };
}

std::vector<AuthoredChartManifestEntry> load_chart_manifest(
    const std::filesystem::path& manifest_path,
    foundation::CrashSafeLog& log,
    std::vector<ContentCookIssue>& issues,
    bool& fatal_error) {
    std::vector<AuthoredChartManifestEntry> entries;
    const std::optional<std::string> text = read_text_file(manifest_path);
    if (!text) {
        append_issue(issues, log, ContentCookIssueSeverity::Error, manifest_path, 0, "Unable to read chart authoring manifest.");
        fatal_error = true;
        return entries;
    }

    const SectionMap sections = parse_sections(*text, manifest_path, log, issues, fatal_error);
    if (fatal_error) {
        return entries;
    }

    const std::filesystem::path manifest_directory = manifest_path.parent_path();
    for (const auto& [section_name, values] : sections) {
        const std::optional<AuthoredChartManifestEntry> entry = parse_chart_manifest_entry(
            section_name,
            values,
            manifest_directory,
            manifest_path,
            log,
            issues);
        if (!entry) {
            if (section_name.rfind("chart.", 0) == 0) {
                fatal_error = true;
            }
            continue;
        }
        if (!std::filesystem::exists(entry->source_path)) {
            append_issue(issues, log, ContentCookIssueSeverity::Error, entry->source_path, 0, "Chart source path does not exist.");
            fatal_error = true;
            continue;
        }
        entries.push_back(*entry);
    }

    std::sort(entries.begin(), entries.end(), [](const AuthoredChartManifestEntry& lhs, const AuthoredChartManifestEntry& rhs) {
        return lhs.authoring_id < rhs.authoring_id;
    });
    return entries;
}

std::vector<AuthoredAudioClipManifestEntry> load_audio_manifest_entries(
    const std::filesystem::path& manifest_path,
    foundation::CrashSafeLog& log,
    std::vector<ContentCookIssue>& issues,
    bool& fatal_error) {
    std::vector<AuthoredAudioClipManifestEntry> entries;
    if (!std::filesystem::exists(manifest_path)) {
        append_issue(
            issues,
            log,
            ContentCookIssueSeverity::Warning,
            manifest_path,
            0,
            "No audio authoring manifest was found; audio asset validation was limited to chart references.");
        return entries;
    }

    const std::optional<std::string> text = read_text_file(manifest_path);
    if (!text) {
        append_issue(issues, log, ContentCookIssueSeverity::Error, manifest_path, 0, "Unable to read audio authoring manifest.");
        fatal_error = true;
        return entries;
    }

    const SectionMap sections = parse_sections(*text, manifest_path, log, issues, fatal_error);
    if (fatal_error) {
        return entries;
    }

    std::unordered_set<std::string> runtime_labels;
    const std::filesystem::path manifest_directory = manifest_path.parent_path();
    for (const auto& [section_name, values] : sections) {
        if (section_name == "meta") {
            continue;
        }

        if (section_name.rfind("clip.", 0) != 0) {
            append_issue(issues, log, ContentCookIssueSeverity::Warning, manifest_path, 0, "Unknown audio manifest section was ignored.");
            continue;
        }

        const auto runtime_label_it = values.find("runtime_label");
        const auto source_it = values.find("source");
        if (runtime_label_it == values.end() || source_it == values.end()) {
            append_issue(issues, log, ContentCookIssueSeverity::Error, manifest_path, 0, "Audio clip section is missing runtime_label or source.");
            fatal_error = true;
            continue;
        }

        const std::string authoring_id = std::string(section_name.substr(std::string_view("clip.").size()));
        if (authoring_id.empty()) {
            append_issue(issues, log, ContentCookIssueSeverity::Error, manifest_path, 0, "Audio clip section has an empty authoring id.");
            fatal_error = true;
            continue;
        }

        if (runtime_label_it->second.value.empty()) {
            append_issue(issues, log, ContentCookIssueSeverity::Error, manifest_path, runtime_label_it->second.line, "Audio clip runtime_label cannot be empty.");
            fatal_error = true;
            continue;
        }

        if (!runtime_labels.insert(runtime_label_it->second.value).second) {
            append_issue(issues, log, ContentCookIssueSeverity::Error, manifest_path, runtime_label_it->second.line, "Duplicate audio clip runtime_label.");
            fatal_error = true;
            continue;
        }

        if (source_it->second.value.empty()) {
            append_issue(issues, log, ContentCookIssueSeverity::Error, manifest_path, source_it->second.line, "Audio clip source cannot be empty.");
            fatal_error = true;
            continue;
        }

        const std::filesystem::path source_value(source_it->second.value);
        const std::filesystem::path source_path = source_value.is_absolute()
            ? std::filesystem::absolute(source_value)
            : std::filesystem::absolute(manifest_directory / source_value);
        if (!std::filesystem::exists(source_path)) {
            append_issue(issues, log, ContentCookIssueSeverity::Error, source_path, 0, "Audio clip source path does not exist.");
            fatal_error = true;
            continue;
        }

        const std::string extension = lowercase_copy(source_path.extension().string());
        if (extension != ".wav" && extension != ".wave") {
            append_issue(issues, log, ContentCookIssueSeverity::Error, source_path, 0, "Audio clip source format is unsupported; expected WAV.");
            fatal_error = true;
            continue;
        }

        entries.push_back(AuthoredAudioClipManifestEntry{
            .authoring_id = authoring_id,
            .runtime_label = runtime_label_it->second.value,
            .source_path = source_path,
        });
    }

    std::sort(entries.begin(), entries.end(), [](const AuthoredAudioClipManifestEntry& lhs, const AuthoredAudioClipManifestEntry& rhs) {
        return lhs.authoring_id < rhs.authoring_id;
    });
    return entries;
}

std::optional<AuthoredRenderManifestEntry> parse_render_manifest_entry(
    RenderAssetKind kind,
    std::string_view section_name,
    const SectionValues& values,
    const std::filesystem::path& manifest_directory,
    const std::filesystem::path& cooked_root,
    const std::filesystem::path& manifest_path,
    foundation::CrashSafeLog& log,
    std::vector<ContentCookIssue>& issues) {
    if (is_auxiliary_render_section(kind, section_name)) {
        return std::nullopt;
    }

    const std::string_view prefix = render_manifest_prefix(kind);
    if (section_name.rfind(prefix, 0) != 0) {
        append_issue(issues, log, ContentCookIssueSeverity::Warning, manifest_path, 0, "Unknown render manifest section was ignored.");
        return std::nullopt;
    }

    const auto runtime_label_it = values.find("runtime_label");
    const auto source_it = values.find("source");
    if (runtime_label_it == values.end() || source_it == values.end()) {
        append_issue(issues, log, ContentCookIssueSeverity::Error, manifest_path, 0, "Render manifest section is missing runtime_label or source.");
        return std::nullopt;
    }

    const std::string authoring_id = std::string(section_name.substr(prefix.size()));
    const std::filesystem::path source_path = std::filesystem::path(source_it->second.value).is_absolute()
        ? std::filesystem::absolute(std::filesystem::path(source_it->second.value))
        : std::filesystem::absolute(manifest_directory / source_it->second.value);

    std::string payload_stem;
    if (const auto output_name_it = values.find("output_name"); output_name_it != values.end() && !output_name_it->second.value.empty()) {
        payload_stem = sanitize_file_stem(output_name_it->second.value);
    } else if (kind == RenderAssetKind::Font) {
        payload_stem = sanitize_file_stem(authoring_id);
    } else {
        payload_stem = sanitize_file_stem(std::filesystem::path(source_it->second.value).stem().string());
    }

    const std::filesystem::path payload_path = std::filesystem::absolute(
        cooked_root / "render" / cooked_render_subdirectory(kind) / (payload_stem + std::string(cooked_render_payload_extension(kind))));

    return AuthoredRenderManifestEntry{
        .kind = kind,
        .authoring_id = authoring_id,
        .runtime_label = runtime_label_it->second.value,
        .source_path = source_path,
        .payload_path = payload_path,
    };
}

std::vector<AuthoredRenderManifestEntry> load_render_manifest_entries(
    RenderAssetKind kind,
    const std::filesystem::path& manifest_path,
    const std::filesystem::path& cooked_root,
    foundation::CrashSafeLog& log,
    std::vector<ContentCookIssue>& issues,
    bool& fatal_error) {
    std::vector<AuthoredRenderManifestEntry> entries;
    if (!std::filesystem::exists(manifest_path)) {
        append_issue(
            issues,
            log,
            ContentCookIssueSeverity::Warning,
            manifest_path,
            0,
            std::string("No ") + std::string(to_string(kind)) + " authoring manifest was found; matching render entries were skipped.");
        return entries;
    }

    const std::optional<std::string> text = read_text_file(manifest_path);
    if (!text) {
        append_issue(issues, log, ContentCookIssueSeverity::Error, manifest_path, 0, "Unable to read render authoring manifest.");
        fatal_error = true;
        return entries;
    }

    const SectionMap sections = parse_sections(*text, manifest_path, log, issues, fatal_error);
    if (fatal_error) {
        return entries;
    }

    const std::filesystem::path manifest_directory = manifest_path.parent_path();
    for (const auto& [section_name, values] : sections) {
        const std::optional<AuthoredRenderManifestEntry> entry = parse_render_manifest_entry(
            kind,
            section_name,
            values,
            manifest_directory,
            cooked_root,
            manifest_path,
            log,
            issues);
        if (!entry) {
            if (!is_auxiliary_render_section(kind, section_name) && section_name.rfind(render_manifest_prefix(kind), 0) == 0) {
                fatal_error = true;
            }
            continue;
        }

        if (!std::filesystem::exists(entry->source_path)) {
            append_issue(issues, log, ContentCookIssueSeverity::Error, entry->source_path, 0, "Render asset source path does not exist.");
            fatal_error = true;
            continue;
        }
        entries.push_back(*entry);
    }

    std::sort(entries.begin(), entries.end(), [](const AuthoredRenderManifestEntry& lhs, const AuthoredRenderManifestEntry& rhs) {
        if (lhs.kind != rhs.kind) {
            return static_cast<std::uint8_t>(lhs.kind) < static_cast<std::uint8_t>(rhs.kind);
        }
        return lhs.authoring_id < rhs.authoring_id;
    });
    return entries;
}

std::optional<CookedRenderManifestEntry> build_render_manifest_entry(
    const AuthoredRenderManifestEntry& authored_entry,
    const std::filesystem::path& project_root,
    const std::filesystem::path& cooked_render_root,
    foundation::CrashSafeLog& log,
    std::vector<ContentCookIssue>& issues) {
    const std::optional<std::string> payload_text = read_text_file(authored_entry.payload_path);
    if (!payload_text) {
        append_issue(issues, log, ContentCookIssueSeverity::Error, authored_entry.payload_path, 0, "Unable to read cooked render asset payload metadata.");
        return std::nullopt;
    }

    const std::optional<std::string> source_hash = hash_file(authored_entry.source_path);
    if (!source_hash) {
        append_issue(issues, log, ContentCookIssueSeverity::Error, authored_entry.source_path, 0, "Unable to hash render asset source file.");
        return std::nullopt;
    }

    bool fatal_error = false;
    const SectionMap sections = parse_sections(*payload_text, authored_entry.payload_path, log, issues, fatal_error);
    if (fatal_error) {
        return std::nullopt;
    }

    std::vector<ManifestDependencyRecord> dependencies;
    if (const ParsedKeyValue* dependency_value = find_value(
            sections,
            render_metadata_section(authored_entry.kind),
            render_metadata_dependency_key(authored_entry.kind));
        dependency_value != nullptr && !dependency_value->value.empty()) {
        for (const std::string& dependency_token : split_string(dependency_value->value, ',')) {
            const std::filesystem::path dependency_path = std::filesystem::path(dependency_token).is_absolute()
                ? std::filesystem::absolute(std::filesystem::path(dependency_token))
                : std::filesystem::absolute(authored_entry.payload_path.parent_path() / dependency_token);
            if (!std::filesystem::exists(dependency_path)) {
                append_issue(issues, log, ContentCookIssueSeverity::Error, dependency_path, 0, "Render asset dependency path does not exist.");
                return std::nullopt;
            }

            const std::optional<std::string> dependency_hash = hash_file(dependency_path);
            if (!dependency_hash) {
                append_issue(issues, log, ContentCookIssueSeverity::Error, dependency_path, 0, "Unable to hash render asset dependency file.");
                return std::nullopt;
            }

            dependencies.push_back(ManifestDependencyRecord{
                .relative_path = make_relative_path(dependency_path, cooked_render_root),
                .hash = *dependency_hash,
            });
        }
    }

    return CookedRenderManifestEntry{
        .kind = authored_entry.kind,
        .authoring_id = authored_entry.authoring_id,
        .runtime_label = authored_entry.runtime_label,
        .source_relative_path = make_relative_path(authored_entry.source_path, project_root),
        .source_hash = *source_hash,
        .payload_relative_path = make_relative_path(authored_entry.payload_path, cooked_render_root),
        .payload_hash = hash_text(*payload_text),
        .dependencies = std::move(dependencies),
    };
}

std::string serialize_chart_manifest(
    std::span<const CookedChartDocumentRecord> records,
    const std::filesystem::path& project_root,
    const std::filesystem::path& cooked_chart_root,
    foundation::CrashSafeLog& log,
    std::vector<ContentCookIssue>& issues,
    bool& fatal_error) {
    (void)cooked_chart_root;

    std::ostringstream stream;
    stream << "[meta]\n";
    stream << "schema = " << k_cooked_chart_manifest_schema << '\n';
    stream << "manifest_version = 1\n";
    stream << "generator = " << k_manifest_generator << "\n\n";

    for (const CookedChartDocumentRecord& record : records) {
        const std::optional<std::string> source_hash = hash_file(record.manifest_entry.source_path);
        if (!source_hash) {
            append_issue(issues, log, ContentCookIssueSeverity::Error, record.manifest_entry.source_path, 0, "Unable to hash chart source file.");
            fatal_error = true;
            return {};
        }

        stream << "[chart." << record.manifest_entry.authoring_id << "]\n";
        stream << "runtime_label = " << record.manifest_entry.runtime_label << '\n';
        stream << "version = 1\n";
        stream << "source = " << path_to_manifest_string(record.manifest_entry.source_path, project_root) << '\n';
        stream << "source_hash = " << *source_hash << '\n';
        stream << "payload = " << record.manifest_entry.payload_path.generic_string() << '\n';
        stream << "payload_hash = " << record.manifest_entry.payload_hash << '\n';
        stream << "dependencies = \n";
        stream << "dependency_hashes = \n\n";
    }

    return stream.str();
}

std::string serialize_render_manifest(std::span<const CookedRenderManifestEntry> records) {
    std::ostringstream stream;
    stream << "[meta]\n";
    stream << "schema = " << k_cooked_render_manifest_schema << '\n';
    stream << "manifest_version = 1\n";
    stream << "generator = " << k_manifest_generator << "\n\n";

    for (const CookedRenderManifestEntry& record : records) {
        stream << '[' << to_string(record.kind) << '.' << record.authoring_id << "]\n";
        stream << "runtime_label = " << record.runtime_label << '\n';
        stream << "version = 1\n";
        stream << "source = " << record.source_relative_path.generic_string() << '\n';
        stream << "source_hash = " << record.source_hash << '\n';
        stream << "payload = " << record.payload_relative_path.generic_string() << '\n';
        stream << "payload_hash = " << record.payload_hash << '\n';
        stream << "dependencies = " << serialize_dependency_paths(record.dependencies) << '\n';
        stream << "dependency_hashes = " << serialize_dependency_hashes(record.dependencies) << "\n\n";
    }

    return stream.str();
}

class ChartDocumentParser {
  public:
    ChartDocumentParser(
        const AuthoredChartManifestEntry& manifest_entry,
        foundation::CrashSafeLog& log,
        std::vector<ContentCookIssue>& issues)
        : manifest_entry_(manifest_entry),
          log_(log),
          issues_(issues) {}

    std::optional<ChartDocument> parse() {
        const std::optional<std::string> text = read_text_file(manifest_entry_.source_path);
        if (!text) {
            add_error(0, "Unable to read chart source file.");
            return std::nullopt;
        }

        bool parse_fatal_error = false;
        sections_ = ::reaktio::content::parse_sections(*text, manifest_entry_.source_path, log_, issues_, parse_fatal_error);
        if (parse_fatal_error) {
            return std::nullopt;
        }

        ChartDocument document{};
        if (!parse_meta(document) || !parse_audio(document) || !parse_tempo(document) || !parse_chart_sections(document)) {
            return std::nullopt;
        }

        if (document.metadata.id != manifest_entry_.authoring_id) {
            add_error(0, "Chart metadata id does not match the manifest section id.");
            return std::nullopt;
        }

        if (document.audio.preview_end_ms < document.audio.preview_start_ms) {
            add_error(0, "Chart preview_end_ms cannot be earlier than preview_start_ms.");
            return std::nullopt;
        }

        if (!document.default_scroll_profile_id.empty() &&
            find_scroll_profile(document, document.default_scroll_profile_id) == nullptr) {
            add_error(0, "Default scroll profile id does not exist in the chart document.");
            return std::nullopt;
        }

        for (const ChartEvent& event : document.events) {
            const std::string scroll_profile_id = std::visit(
                [](const auto& value) -> std::string {
                    using EventType = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<EventType, NoteCue> || std::is_same_v<EventType, HoldCue> ||
                                  std::is_same_v<EventType, HazardCue>) {
                        return value.cue.scroll_profile_id;
                    } else {
                        return {};
                    }
                },
                event);

            if (!scroll_profile_id.empty() && find_scroll_profile(document, scroll_profile_id) == nullptr) {
                add_error(0, "Chart event references an unknown scroll profile id.");
                return std::nullopt;
            }
        }

        rhythm::TempoMap tempo_map;
        if (!tempo_map.rebuild(document.tempo_map)) {
            add_error(0, std::string("Chart tempo map failed validation: ") + std::string(tempo_map.last_error()));
            return std::nullopt;
        }

        std::sort(document.scroll_profiles.begin(), document.scroll_profiles.end(), [](const ScrollProfileDefinition& lhs, const ScrollProfileDefinition& rhs) {
            return lhs.id < rhs.id;
        });
        std::sort(document.events.begin(), document.events.end(), [](const ChartEvent& lhs, const ChartEvent& rhs) {
            if (event_start_tick(lhs) != event_start_tick(rhs)) {
                return event_start_tick(lhs) < event_start_tick(rhs);
            }
            return event_id(lhs) < event_id(rhs);
        });
        return document;
    }

  private:
    bool parse_meta(ChartDocument& document) {
        const ParsedKeyValue* schema = find_value(sections_, "meta", "schema");
        const ParsedKeyValue* id = find_value(sections_, "meta", "id");
        const ParsedKeyValue* display_name = find_value(sections_, "meta", "display_name");
        if (schema == nullptr || id == nullptr || display_name == nullptr) {
            add_error(0, "Chart [meta] section is missing required keys.");
            return false;
        }

        document.metadata.schema = schema->value;
        document.metadata.id = id->value;
        document.metadata.display_name = display_name->value;
        if (const ParsedKeyValue* author = find_value(sections_, "meta", "author")) {
            document.metadata.author = author->value;
        }
        if (const ParsedKeyValue* description = find_value(sections_, "meta", "description")) {
            document.metadata.description = description->value;
        }
        if (const ParsedKeyValue* tags = find_value(sections_, "meta", "tags")) {
            document.metadata.tags = split_string(tags->value, ',');
        }
        if (const ParsedKeyValue* source_revision = find_value(sections_, "meta", "source_revision")) {
            document.metadata.source_revision = source_revision->value;
        }
        if (document.metadata.schema != k_authoring_chart_schema) {
            add_warning(schema->line, "Chart schema id is not the expected authoring schema version.");
        }
        return true;
    }

    bool parse_audio(ChartDocument& document) {
        const ParsedKeyValue* clip_id = find_value(sections_, "audio", "clip_id");
        const ParsedKeyValue* preview_start_ms = find_value(sections_, "audio", "preview_start_ms");
        const ParsedKeyValue* preview_end_ms = find_value(sections_, "audio", "preview_end_ms");
        if (clip_id == nullptr || preview_start_ms == nullptr || preview_end_ms == nullptr) {
            add_error(0, "Chart [audio] section is missing required keys.");
            return false;
        }

        document.audio.clip_id = clip_id->value;
        if (!try_parse_integer(preview_start_ms->value, document.audio.preview_start_ms) ||
            !try_parse_integer(preview_end_ms->value, document.audio.preview_end_ms)) {
            add_error(0, "Chart [audio] section contains invalid preview timing values.");
            return false;
        }
        if (const ParsedKeyValue* lead_in_ms = find_value(sections_, "audio", "lead_in_ms")) {
            if (!try_parse_integer(lead_in_ms->value, document.audio.lead_in_ms)) {
                add_error(lead_in_ms->line, "Chart [audio] lead_in_ms is invalid.");
                return false;
            }
        }
        if (const ParsedKeyValue* tail_out_ms = find_value(sections_, "audio", "tail_out_ms")) {
            if (!try_parse_integer(tail_out_ms->value, document.audio.tail_out_ms)) {
                add_error(tail_out_ms->line, "Chart [audio] tail_out_ms is invalid.");
                return false;
            }
        }
        return true;
    }

    bool parse_tempo(ChartDocument& document) {
        const ParsedKeyValue* ticks_per_quarter = find_value(sections_, "tempo", "ticks_per_quarter");
        const ParsedKeyValue* beat_zero_offset_ms = find_value(sections_, "tempo", "beat_zero_offset_ms");
        if (ticks_per_quarter == nullptr || beat_zero_offset_ms == nullptr) {
            add_error(0, "Chart [tempo] section is missing required keys.");
            return false;
        }

        if (!try_parse_integer(ticks_per_quarter->value, document.tempo_map.config.ticks_per_quarter_note) ||
            !try_parse_integer(beat_zero_offset_ms->value, document.beat_zero_offset_ms)) {
            add_error(0, "Chart [tempo] section contains invalid values.");
            return false;
        }

        document.tempo_map.config.sample_rate_hz = 48000;
        if (const ParsedKeyValue* default_scroll_profile = find_value(sections_, "tempo", "default_scroll_profile")) {
            document.default_scroll_profile_id = default_scroll_profile->value;
        }
        return true;
    }

    bool parse_chart_sections(ChartDocument& document) {
        for (const auto& [section_name, values] : sections_) {
            if (section_name == "meta" || section_name == "audio" || section_name == "tempo") {
                continue;
            }
            if (section_name.rfind("scroll_profile.", 0) == 0) {
                if (!parse_scroll_profile(section_name, values, document)) {
                    return false;
                }
                continue;
            }
            if (section_name.rfind("cue.", 0) == 0) {
                if (!parse_cue(section_name, values, document)) {
                    return false;
                }
                continue;
            }
            if (section_name.rfind("event.", 0) == 0) {
                if (!parse_event(section_name, values, document)) {
                    return false;
                }
                continue;
            }
            if (section_name.rfind("camera.", 0) == 0) {
                if (!parse_camera(section_name, values, document)) {
                    return false;
                }
                continue;
            }
            if (section_name.rfind("text_prompt.", 0) == 0) {
                if (!parse_text_prompt(section_name, values, document)) {
                    return false;
                }
                continue;
            }
            if (section_name.rfind("vfx.", 0) == 0) {
                if (!parse_vfx(section_name, values, document)) {
                    return false;
                }
                continue;
            }

            add_warning(0, "Unknown chart section was ignored.");
        }

        return true;
    }

    bool parse_scroll_profile(std::string_view section_name, const SectionValues& values, ChartDocument& document) {
        const std::string id(section_name.substr(std::string_view("scroll_profile.").size()));
        if (!add_unique_id(scroll_profile_ids_, id, manifest_entry_.source_path, log_, issues_, 0, "scroll-profile")) {
            return false;
        }

        const auto units_it = values.find("units_per_second");
        const auto spawn_it = values.find("spawn_lead_ticks");
        if (units_it == values.end() || spawn_it == values.end()) {
            add_error(0, "Scroll profile section is missing required keys.");
            return false;
        }

        ScrollProfileDefinition profile{};
        profile.id = id;
        if (!try_parse_double(units_it->second.value, profile.units_per_second) ||
            !try_parse_integer(spawn_it->second.value, profile.spawn_lead_ticks)) {
            add_error(0, "Scroll profile section contains invalid values.");
            return false;
        }
        if (const auto release_it = values.find("release_tail_ticks"); release_it != values.end()) {
            if (!try_parse_integer(release_it->second.value, profile.release_tail_ticks)) {
                add_error(release_it->second.line, "Scroll profile release_tail_ticks is invalid.");
                return false;
            }
        }

        constexpr std::string_view known_keys[] = {"units_per_second", "spawn_lead_ticks", "release_tail_ticks"};
        profile.extensions = collect_unknown_fields(values, known_keys);
        document.scroll_profiles.push_back(std::move(profile));
        return true;
    }

    bool parse_cue(std::string_view section_name, const SectionValues& values, ChartDocument& document) {
        const auto kind_it = values.find("kind");
        const auto tick_it = values.find("tick");
        if (kind_it == values.end() || tick_it == values.end()) {
            add_error(0, "Cue section is missing required keys.");
            return false;
        }

        RoutedCueData cue{};
        cue.event.id = std::string(section_name.substr(std::string_view("cue.").size()));
        if (!add_unique_id(event_ids_, cue.event.id, manifest_entry_.source_path, log_, issues_, tick_it->second.line, "event")) {
            return false;
        }
        if (!try_parse_integer(tick_it->second.value, cue.event.placement.start_tick)) {
            add_error(tick_it->second.line, "Cue tick is invalid.");
            return false;
        }

        cue.scroll_profile_id = document.default_scroll_profile_id;
        if (const auto duration_it = values.find("duration_ticks"); duration_it != values.end()) {
            if (!try_parse_integer(duration_it->second.value, cue.event.placement.duration_ticks)) {
                add_error(duration_it->second.line, "Cue duration_ticks is invalid.");
                return false;
            }
        }
        if (const auto lane_it = values.find("lane"); lane_it != values.end()) {
            std::uint32_t lane = 0;
            if (!try_parse_uint32(lane_it->second.value, lane)) {
                add_error(lane_it->second.line, "Cue lane is invalid.");
                return false;
            }
            cue.route.lane_index = lane;
        }
        if (const auto channel_it = values.find("channel"); channel_it != values.end()) {
            std::uint32_t channel = 0;
            if (!try_parse_uint32(channel_it->second.value, channel)) {
                add_error(channel_it->second.line, "Cue channel is invalid.");
                return false;
            }
            cue.route.channel_index = channel;
        }
        if (const auto scroll_it = values.find("scroll_profile"); scroll_it != values.end()) {
            cue.scroll_profile_id = scroll_it->second.value;
        }
        if (const auto judgement_it = values.find("judgement_profile"); judgement_it != values.end()) {
            cue.judgement_profile_id = judgement_it->second.value;
        }

        constexpr std::string_view known_keys[] = {
            "kind",
            "tick",
            "duration_ticks",
            "lane",
            "channel",
            "scroll_profile",
            "judgement_profile",
            "hazard_profile",
        };
        cue.event.extensions = collect_unknown_fields(values, known_keys);

        const std::string kind = lowercase_copy(kind_it->second.value);
        if (kind == "tap") {
            cue.event.placement.duration_ticks = 0;
            document.events.push_back(NoteCue{.cue = std::move(cue)});
            return true;
        }
        if (kind == "hold") {
            if (cue.event.placement.duration_ticks <= 0) {
                add_error(kind_it->second.line, "Hold cue requires a positive duration_ticks value.");
                return false;
            }
            document.events.push_back(HoldCue{.cue = std::move(cue)});
            return true;
        }
        if (kind == "hazard") {
            HazardCue hazard{.cue = std::move(cue)};
            if (const auto hazard_profile_it = values.find("hazard_profile"); hazard_profile_it != values.end()) {
                hazard.hazard_profile_id = hazard_profile_it->second.value;
            }
            document.events.push_back(std::move(hazard));
            return true;
        }

        add_error(kind_it->second.line, "Cue kind must be tap, hold, or hazard.");
        return false;
    }

    bool parse_event(std::string_view section_name, const SectionValues& values, ChartDocument& document) {
        const auto kind_it = values.find("kind");
        const auto tick_it = values.find("tick");
        if (kind_it == values.end() || tick_it == values.end()) {
            add_error(0, "Event section is missing required keys.");
            return false;
        }

        rhythm::ChartTick start_tick = 0;
        if (!try_parse_integer(tick_it->second.value, start_tick)) {
            add_error(tick_it->second.line, "Event tick is invalid.");
            return false;
        }

        const std::string event_id(section_name.substr(std::string_view("event.").size()));
        const std::string kind = lowercase_copy(kind_it->second.value);
        if (kind == "tempo_change") {
            rhythm::TempoChange change{};
            change.start_tick = start_tick;
            if (const auto bpm_it = values.find("bpm"); bpm_it != values.end()) {
                double bpm = 0.0;
                if (!try_parse_double(bpm_it->second.value, bpm) || bpm <= 0.0) {
                    add_error(bpm_it->second.line, "Tempo change bpm is invalid.");
                    return false;
                }
                change.microseconds_per_quarter_note = static_cast<rhythm::TimelineMicroseconds>(
                    (60.0 * static_cast<double>(rhythm::microseconds_per_second())) / bpm);
            } else if (const auto micros_it = values.find("microseconds_per_quarter_note"); micros_it != values.end()) {
                if (!try_parse_integer(micros_it->second.value, change.microseconds_per_quarter_note) ||
                    change.microseconds_per_quarter_note <= 0) {
                    add_error(micros_it->second.line, "Tempo change microseconds_per_quarter_note is invalid.");
                    return false;
                }
            } else {
                add_error(kind_it->second.line, "Tempo change requires bpm or microseconds_per_quarter_note.");
                return false;
            }
            document.tempo_map.tempo_changes.push_back(change);
            return true;
        }

        if (kind == "time_signature") {
            rhythm::TimeSignatureChange signature{};
            signature.start_tick = start_tick;
            const auto numerator_it = values.find("numerator");
            const auto denominator_it = values.find("denominator");
            if (numerator_it == values.end() || denominator_it == values.end() ||
                !try_parse_integer(numerator_it->second.value, signature.numerator) ||
                !try_parse_integer(denominator_it->second.value, signature.denominator) ||
                signature.numerator <= 0 || signature.denominator <= 0) {
                add_error(kind_it->second.line, "Time signature event contains invalid numerator or denominator.");
                return false;
            }
            document.tempo_map.time_signature_changes.push_back(signature);
            return true;
        }

        if (kind == "stop") {
            rhythm::StopSegment stop{};
            stop.start_tick = start_tick;
            if (const auto duration_ms_it = values.find("duration_ms"); duration_ms_it != values.end()) {
                TimelineMilliseconds duration_ms = 0;
                if (!try_parse_integer(duration_ms_it->second.value, duration_ms) || duration_ms < 0) {
                    add_error(duration_ms_it->second.line, "Stop event duration_ms is invalid.");
                    return false;
                }
                stop.duration_microseconds = duration_ms * 1000;
            } else if (const auto duration_micros_it = values.find("duration_microseconds"); duration_micros_it != values.end()) {
                if (!try_parse_integer(duration_micros_it->second.value, stop.duration_microseconds) || stop.duration_microseconds < 0) {
                    add_error(duration_micros_it->second.line, "Stop event duration_microseconds is invalid.");
                    return false;
                }
            } else {
                add_error(kind_it->second.line, "Stop event requires duration_ms or duration_microseconds.");
                return false;
            }
            document.tempo_map.stops.push_back(stop);
            return true;
        }

        if (kind == "warp") {
            rhythm::WarpSegment warp{};
            warp.start_tick = start_tick;
            const auto duration_it = values.find("duration_ticks");
            if (duration_it == values.end() || !try_parse_integer(duration_it->second.value, warp.duration_ticks) || warp.duration_ticks <= 0) {
                add_error(kind_it->second.line, "Warp event requires a positive duration_ticks value.");
                return false;
            }
            document.tempo_map.warps.push_back(warp);
            return true;
        }

        if (kind == "trigger") {
            const auto trigger_id_it = values.find("trigger_id");
            if (trigger_id_it == values.end()) {
                add_error(kind_it->second.line, "Trigger event requires trigger_id.");
                return false;
            }
            if (!add_unique_id(event_ids_, event_id, manifest_entry_.source_path, log_, issues_, trigger_id_it->second.line, "event")) {
                return false;
            }
            TriggerEvent trigger{};
            trigger.event.id = event_id;
            trigger.event.placement.start_tick = start_tick;
            if (const auto duration_it = values.find("duration_ticks"); duration_it != values.end()) {
                if (!try_parse_integer(duration_it->second.value, trigger.event.placement.duration_ticks)) {
                    add_error(duration_it->second.line, "Trigger event duration_ticks is invalid.");
                    return false;
                }
            }
            trigger.trigger_id = trigger_id_it->second.value;
            if (const auto payload_it = values.find("payload"); payload_it != values.end()) {
                trigger.payload = payload_it->second.value;
            }
            constexpr std::string_view known_keys[] = {"kind", "tick", "duration_ticks", "trigger_id", "payload"};
            trigger.event.extensions = collect_unknown_fields(values, known_keys);
            document.events.push_back(std::move(trigger));
            return true;
        }

        add_error(kind_it->second.line, "Event kind is unsupported.");
        return false;
    }

    bool parse_camera(std::string_view section_name, const SectionValues& values, ChartDocument& document) {
        const auto tick_it = values.find("tick");
        const auto action_it = values.find("camera_action_id");
        if (tick_it == values.end() || action_it == values.end()) {
            add_error(0, "Camera event section is missing required keys.");
            return false;
        }
        CameraEvent event{};
        event.event.id = std::string(section_name.substr(std::string_view("camera.").size()));
        if (!add_unique_id(event_ids_, event.event.id, manifest_entry_.source_path, log_, issues_, tick_it->second.line, "event")) {
            return false;
        }
        if (!try_parse_integer(tick_it->second.value, event.event.placement.start_tick)) {
            add_error(tick_it->second.line, "Camera event tick is invalid.");
            return false;
        }
        if (const auto duration_it = values.find("duration_ticks"); duration_it != values.end()) {
            if (!try_parse_integer(duration_it->second.value, event.event.placement.duration_ticks)) {
                add_error(duration_it->second.line, "Camera event duration_ticks is invalid.");
                return false;
            }
        }
        event.camera_action_id = action_it->second.value;
        if (const auto payload_it = values.find("payload"); payload_it != values.end()) {
            event.payload = payload_it->second.value;
        }
        constexpr std::string_view known_keys[] = {"tick", "duration_ticks", "camera_action_id", "payload"};
        event.event.extensions = collect_unknown_fields(values, known_keys);
        document.events.push_back(std::move(event));
        return true;
    }

    bool parse_text_prompt(std::string_view section_name, const SectionValues& values, ChartDocument& document) {
        const auto tick_it = values.find("tick");
        if (tick_it == values.end()) {
            add_error(0, "Text prompt section is missing tick.");
            return false;
        }
        TextPromptEvent event{};
        event.event.id = std::string(section_name.substr(std::string_view("text_prompt.").size()));
        if (!add_unique_id(event_ids_, event.event.id, manifest_entry_.source_path, log_, issues_, tick_it->second.line, "event")) {
            return false;
        }
        if (!try_parse_integer(tick_it->second.value, event.event.placement.start_tick)) {
            add_error(tick_it->second.line, "Text prompt tick is invalid.");
            return false;
        }
        if (const auto duration_it = values.find("duration_ticks"); duration_it != values.end()) {
            if (!try_parse_integer(duration_it->second.value, event.event.placement.duration_ticks)) {
                add_error(duration_it->second.line, "Text prompt duration_ticks is invalid.");
                return false;
            }
        }
        if (const auto prompt_text_it = values.find("prompt_text"); prompt_text_it != values.end()) {
            event.prompt_text = prompt_text_it->second.value;
        }
        if (const auto prompt_token_it = values.find("prompt_token"); prompt_token_it != values.end()) {
            event.prompt_token = prompt_token_it->second.value;
        }
        if (const auto locale_table_it = values.find("locale_table_id"); locale_table_it != values.end()) {
            event.locale_table_id = locale_table_it->second.value;
        }
        if (event.prompt_text.empty() && event.prompt_token.empty()) {
            add_error(tick_it->second.line, "Text prompt event requires prompt_text or prompt_token.");
            return false;
        }
        constexpr std::string_view known_keys[] = {"tick", "duration_ticks", "prompt_text", "prompt_token", "locale_table_id"};
        event.event.extensions = collect_unknown_fields(values, known_keys);
        document.events.push_back(std::move(event));
        return true;
    }

    bool parse_vfx(std::string_view section_name, const SectionValues& values, ChartDocument& document) {
        const auto tick_it = values.find("tick");
        const auto effect_it = values.find("effect_id");
        if (tick_it == values.end() || effect_it == values.end()) {
            add_error(0, "Vfx section is missing required keys.");
            return false;
        }
        VfxEvent event{};
        event.event.id = std::string(section_name.substr(std::string_view("vfx.").size()));
        if (!add_unique_id(event_ids_, event.event.id, manifest_entry_.source_path, log_, issues_, tick_it->second.line, "event")) {
            return false;
        }
        if (!try_parse_integer(tick_it->second.value, event.event.placement.start_tick)) {
            add_error(tick_it->second.line, "Vfx tick is invalid.");
            return false;
        }
        if (const auto duration_it = values.find("duration_ticks"); duration_it != values.end()) {
            if (!try_parse_integer(duration_it->second.value, event.event.placement.duration_ticks)) {
                add_error(duration_it->second.line, "Vfx duration_ticks is invalid.");
                return false;
            }
        }
        event.effect_id = effect_it->second.value;
        if (const auto payload_it = values.find("payload"); payload_it != values.end()) {
            event.payload = payload_it->second.value;
        }
        constexpr std::string_view known_keys[] = {"tick", "duration_ticks", "effect_id", "payload"};
        event.event.extensions = collect_unknown_fields(values, known_keys);
        document.events.push_back(std::move(event));
        return true;
    }

    void add_error(std::size_t line, std::string message) {
        append_issue(issues_, log_, ContentCookIssueSeverity::Error, manifest_entry_.source_path, line, std::move(message));
    }

    void add_warning(std::size_t line, std::string message) {
        append_issue(issues_, log_, ContentCookIssueSeverity::Warning, manifest_entry_.source_path, line, std::move(message));
    }

    const AuthoredChartManifestEntry& manifest_entry_;
    foundation::CrashSafeLog& log_;
    std::vector<ContentCookIssue>& issues_;
    SectionMap sections_;
    std::unordered_set<std::string> scroll_profile_ids_;
    std::unordered_set<std::string> event_ids_;
};

std::string serialize_known_and_extension_fields(
    std::initializer_list<std::pair<std::string_view, std::string>> known_values,
    std::span<const AuthoredField> extensions) {
    std::ostringstream stream;
    for (const auto& [key, value] : known_values) {
        if (!value.empty()) {
            stream << key << " = " << value << '\n';
        }
    }
    for (const AuthoredField& field : extensions) {
        stream << field.key << " = " << field.value << '\n';
    }
    return stream.str();
}

std::string serialize_chart_document(const ChartDocument& document) {
    std::ostringstream stream;
    stream << "[chart]\n";
    stream << "schema = " << k_cooked_chart_schema << '\n';
    stream << "id = " << document.metadata.id << '\n';
    stream << "display_name = " << document.metadata.display_name << '\n';
    if (!document.metadata.author.empty()) {
        stream << "author = " << document.metadata.author << '\n';
    }
    if (!document.metadata.description.empty()) {
        stream << "description = " << document.metadata.description << '\n';
    }
    if (!document.metadata.tags.empty()) {
        stream << "tags = " << join_strings(document.metadata.tags, ",") << '\n';
    }
    if (!document.metadata.source_revision.empty()) {
        stream << "source_revision = " << document.metadata.source_revision << '\n';
    }
    stream << '\n';

    stream << "[audio]\n";
    stream << "clip_id = " << document.audio.clip_id << '\n';
    stream << "preview_start_ms = " << document.audio.preview_start_ms << '\n';
    stream << "preview_end_ms = " << document.audio.preview_end_ms << '\n';
    stream << "lead_in_ms = " << document.audio.lead_in_ms << '\n';
    stream << "tail_out_ms = " << document.audio.tail_out_ms << "\n\n";

    stream << "[tempo]\n";
    stream << "ticks_per_quarter = " << document.tempo_map.config.ticks_per_quarter_note << '\n';
    stream << "beat_zero_offset_ms = " << document.beat_zero_offset_ms << '\n';
    if (!document.default_scroll_profile_id.empty()) {
        stream << "default_scroll_profile = " << document.default_scroll_profile_id << '\n';
    }
    stream << '\n';

    for (const ScrollProfileDefinition& profile : document.scroll_profiles) {
        stream << "[scroll_profile." << profile.id << "]\n";
        stream << "units_per_second = " << profile.units_per_second << '\n';
        stream << "spawn_lead_ticks = " << profile.spawn_lead_ticks << '\n';
        stream << "release_tail_ticks = " << profile.release_tail_ticks << '\n';
        for (const AuthoredField& field : profile.extensions) {
            stream << field.key << " = " << field.value << '\n';
        }
        stream << '\n';
    }

    for (std::size_t index = 0; index < document.tempo_map.tempo_changes.size(); ++index) {
        const rhythm::TempoChange& change = document.tempo_map.tempo_changes[index];
        stream << "[tempo_change." << index << "]\n";
        stream << "tick = " << change.start_tick << '\n';
        stream << "microseconds_per_quarter_note = " << change.microseconds_per_quarter_note << "\n\n";
    }
    for (std::size_t index = 0; index < document.tempo_map.time_signature_changes.size(); ++index) {
        const rhythm::TimeSignatureChange& change = document.tempo_map.time_signature_changes[index];
        stream << "[time_signature." << index << "]\n";
        stream << "tick = " << change.start_tick << '\n';
        stream << "numerator = " << change.numerator << '\n';
        stream << "denominator = " << change.denominator << "\n\n";
    }
    for (std::size_t index = 0; index < document.tempo_map.stops.size(); ++index) {
        const rhythm::StopSegment& stop = document.tempo_map.stops[index];
        stream << "[stop." << index << "]\n";
        stream << "tick = " << stop.start_tick << '\n';
        stream << "duration_microseconds = " << stop.duration_microseconds << "\n\n";
    }
    for (std::size_t index = 0; index < document.tempo_map.warps.size(); ++index) {
        const rhythm::WarpSegment& warp = document.tempo_map.warps[index];
        stream << "[warp." << index << "]\n";
        stream << "tick = " << warp.start_tick << '\n';
        stream << "duration_ticks = " << warp.duration_ticks << "\n\n";
    }

    for (const ChartEvent& event : document.events) {
        std::visit(
            [&stream](const auto& value) {
                using EventType = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<EventType, NoteCue>) {
                    stream << "[note." << value.cue.event.id << "]\n";
                    stream << "tick = " << value.cue.event.placement.start_tick << '\n';
                    if (value.cue.route.lane_index.has_value()) {
                        stream << "lane = " << *value.cue.route.lane_index << '\n';
                    }
                    if (value.cue.route.channel_index.has_value()) {
                        stream << "channel = " << *value.cue.route.channel_index << '\n';
                    }
                    if (!value.cue.scroll_profile_id.empty()) {
                        stream << "scroll_profile = " << value.cue.scroll_profile_id << '\n';
                    }
                    if (!value.cue.judgement_profile_id.empty()) {
                        stream << "judgement_profile = " << value.cue.judgement_profile_id << '\n';
                    }
                    for (const AuthoredField& field : value.cue.event.extensions) {
                        stream << field.key << " = " << field.value << '\n';
                    }
                    stream << '\n';
                } else if constexpr (std::is_same_v<EventType, HoldCue>) {
                    stream << "[hold." << value.cue.event.id << "]\n";
                    stream << "tick = " << value.cue.event.placement.start_tick << '\n';
                    stream << "duration_ticks = " << value.cue.event.placement.duration_ticks << '\n';
                    if (value.cue.route.lane_index.has_value()) {
                        stream << "lane = " << *value.cue.route.lane_index << '\n';
                    }
                    if (value.cue.route.channel_index.has_value()) {
                        stream << "channel = " << *value.cue.route.channel_index << '\n';
                    }
                    if (!value.cue.scroll_profile_id.empty()) {
                        stream << "scroll_profile = " << value.cue.scroll_profile_id << '\n';
                    }
                    if (!value.cue.judgement_profile_id.empty()) {
                        stream << "judgement_profile = " << value.cue.judgement_profile_id << '\n';
                    }
                    for (const AuthoredField& field : value.cue.event.extensions) {
                        stream << field.key << " = " << field.value << '\n';
                    }
                    stream << '\n';
                } else if constexpr (std::is_same_v<EventType, HazardCue>) {
                    stream << "[hazard." << value.cue.event.id << "]\n";
                    stream << "tick = " << value.cue.event.placement.start_tick << '\n';
                    if (value.cue.event.placement.duration_ticks > 0) {
                        stream << "duration_ticks = " << value.cue.event.placement.duration_ticks << '\n';
                    }
                    if (value.cue.route.lane_index.has_value()) {
                        stream << "lane = " << *value.cue.route.lane_index << '\n';
                    }
                    if (value.cue.route.channel_index.has_value()) {
                        stream << "channel = " << *value.cue.route.channel_index << '\n';
                    }
                    if (!value.cue.scroll_profile_id.empty()) {
                        stream << "scroll_profile = " << value.cue.scroll_profile_id << '\n';
                    }
                    if (!value.cue.judgement_profile_id.empty()) {
                        stream << "judgement_profile = " << value.cue.judgement_profile_id << '\n';
                    }
                    if (!value.hazard_profile_id.empty()) {
                        stream << "hazard_profile = " << value.hazard_profile_id << '\n';
                    }
                    for (const AuthoredField& field : value.cue.event.extensions) {
                        stream << field.key << " = " << field.value << '\n';
                    }
                    stream << '\n';
                } else if constexpr (std::is_same_v<EventType, TriggerEvent>) {
                    stream << "[trigger." << value.event.id << "]\n";
                    stream << "tick = " << value.event.placement.start_tick << '\n';
                    if (value.event.placement.duration_ticks > 0) {
                        stream << "duration_ticks = " << value.event.placement.duration_ticks << '\n';
                    }
                    stream << "trigger_id = " << value.trigger_id << '\n';
                    if (!value.payload.empty()) {
                        stream << "payload = " << value.payload << '\n';
                    }
                    for (const AuthoredField& field : value.event.extensions) {
                        stream << field.key << " = " << field.value << '\n';
                    }
                    stream << '\n';
                } else if constexpr (std::is_same_v<EventType, CameraEvent>) {
                    stream << "[camera." << value.event.id << "]\n";
                    stream << "tick = " << value.event.placement.start_tick << '\n';
                    if (value.event.placement.duration_ticks > 0) {
                        stream << "duration_ticks = " << value.event.placement.duration_ticks << '\n';
                    }
                    stream << "camera_action_id = " << value.camera_action_id << '\n';
                    if (!value.payload.empty()) {
                        stream << "payload = " << value.payload << '\n';
                    }
                    for (const AuthoredField& field : value.event.extensions) {
                        stream << field.key << " = " << field.value << '\n';
                    }
                    stream << '\n';
                } else if constexpr (std::is_same_v<EventType, TextPromptEvent>) {
                    stream << "[text_prompt." << value.event.id << "]\n";
                    stream << "tick = " << value.event.placement.start_tick << '\n';
                    if (value.event.placement.duration_ticks > 0) {
                        stream << "duration_ticks = " << value.event.placement.duration_ticks << '\n';
                    }
                    if (!value.prompt_text.empty()) {
                        stream << "prompt_text = " << value.prompt_text << '\n';
                    }
                    if (!value.prompt_token.empty()) {
                        stream << "prompt_token = " << value.prompt_token << '\n';
                    }
                    if (!value.locale_table_id.empty()) {
                        stream << "locale_table_id = " << value.locale_table_id << '\n';
                    }
                    for (const AuthoredField& field : value.event.extensions) {
                        stream << field.key << " = " << field.value << '\n';
                    }
                    stream << '\n';
                } else {
                    stream << "[vfx." << value.event.id << "]\n";
                    stream << "tick = " << value.event.placement.start_tick << '\n';
                    if (value.event.placement.duration_ticks > 0) {
                        stream << "duration_ticks = " << value.event.placement.duration_ticks << '\n';
                    }
                    stream << "effect_id = " << value.effect_id << '\n';
                    if (!value.payload.empty()) {
                        stream << "payload = " << value.payload << '\n';
                    }
                    for (const AuthoredField& field : value.event.extensions) {
                        stream << field.key << " = " << field.value << '\n';
                    }
                    stream << '\n';
                }
            },
            event);
    }

    return stream.str();
}

void refresh_validation_issue_counts(
    std::span<const ContentCookIssue> issues,
    ContentValidationSummary& summary) noexcept {
    summary.warning_count = 0;
    summary.error_count = 0;
    for (const ContentCookIssue& issue : issues) {
        switch (issue.severity) {
        case ContentCookIssueSeverity::Warning:
            ++summary.warning_count;
            break;
        case ContentCookIssueSeverity::Error:
            ++summary.error_count;
            break;
        }
    }
}

bool event_starts_inside_warp(const ChartEvent& event, const rhythm::WarpSegment& warp) noexcept {
    if (warp.duration_ticks <= 0 || warp.start_tick < 0) {
        return false;
    }

    const rhythm::ChartTick start_tick = event_start_tick(event);
    const rhythm::ChartTick max_tick = std::numeric_limits<rhythm::ChartTick>::max();
    const rhythm::ChartTick end_tick = warp.start_tick > max_tick - warp.duration_ticks
        ? max_tick
        : warp.start_tick + warp.duration_ticks;
    return start_tick >= warp.start_tick && start_tick < end_tick;
}

bool event_spans_warp(const ChartEvent& event, const rhythm::WarpSegment& warp) noexcept {
    if (warp.duration_ticks <= 0 || warp.start_tick < 0) {
        return false;
    }

    const rhythm::ChartTick start_tick = event_start_tick(event);
    const rhythm::ChartTick duration_ticks = std::max<rhythm::ChartTick>(event_duration_ticks(event), 1);
    const rhythm::ChartTick max_tick = std::numeric_limits<rhythm::ChartTick>::max();
    const rhythm::ChartTick event_end_tick = start_tick > max_tick - duration_ticks ? max_tick : start_tick + duration_ticks;
    const rhythm::ChartTick warp_end_tick = warp.start_tick > max_tick - warp.duration_ticks ? max_tick : warp.start_tick + warp.duration_ticks;
    return start_tick < warp.start_tick && event_end_tick > warp.start_tick && event_end_tick <= warp_end_tick;
}

bool validate_chart_timing_consistency(
    const AuthoredChartManifestEntry& manifest_entry,
    const ChartDocument& document,
    foundation::CrashSafeLog& log,
    std::vector<ContentCookIssue>& issues) {
    bool valid = true;
    auto add_error = [&](std::string message) {
        valid = false;
        append_issue(issues, log, ContentCookIssueSeverity::Error, manifest_entry.source_path, 0, std::move(message));
    };
    auto add_warning = [&](std::string message) {
        append_issue(issues, log, ContentCookIssueSeverity::Warning, manifest_entry.source_path, 0, std::move(message));
    };

    if (document.audio.clip_id.empty()) {
        add_error("Chart audio clip_id cannot be empty.");
    }
    if (document.audio.preview_start_ms < 0 || document.audio.preview_end_ms < 0) {
        add_error("Chart preview timing must use non-negative millisecond values.");
    }
    if (document.audio.lead_in_ms < 0 || document.audio.tail_out_ms < 0) {
        add_error("Chart lead-in and tail-out timing must be non-negative.");
    }

    for (const ScrollProfileDefinition& profile : document.scroll_profiles) {
        if (profile.id.empty()) {
            add_error("Scroll profile id cannot be empty.");
        }
        if (profile.units_per_second <= 0.0) {
            add_error("Scroll profile units_per_second must be positive.");
        }
        if (profile.spawn_lead_ticks < 0 || profile.release_tail_ticks < 0) {
            add_error("Scroll profile spawn and release timing must use non-negative ticks.");
        }
    }

    bool warned_missing_scroll_profile = false;
    for (const ChartEvent& event : document.events) {
        const std::string& id = event_id(event);
        if (id.empty()) {
            add_error("Chart event id cannot be empty.");
        }

        const rhythm::ChartTick start_tick = event_start_tick(event);
        const rhythm::ChartTick duration_ticks = event_duration_ticks(event);
        if (start_tick < 0) {
            add_error(std::string("Chart event '") + id + "' starts at a negative tick.");
        }
        if (duration_ticks < 0) {
            add_error(std::string("Chart event '") + id + "' has a negative duration.");
        }

        const rhythm::ChartTick effective_duration_ticks = std::max<rhythm::ChartTick>(duration_ticks, 1);
        if (start_tick > std::numeric_limits<rhythm::ChartTick>::max() - effective_duration_ticks) {
            add_error(std::string("Chart event '") + id + "' overflows the chart tick range.");
        }

        if (is_interactive_event(event)) {
            const std::string scroll_profile_id = std::visit(
                [](const auto& value) -> std::string {
                    using EventType = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<EventType, NoteCue> || std::is_same_v<EventType, HoldCue> ||
                                  std::is_same_v<EventType, HazardCue>) {
                        return value.cue.scroll_profile_id;
                    } else {
                        return {};
                    }
                },
                event);
            if (scroll_profile_id.empty() && !warned_missing_scroll_profile) {
                add_warning("Interactive chart cues have no scroll profile; presentation defaults will be mode-specific.");
                warned_missing_scroll_profile = true;
            }
        }

        for (const rhythm::WarpSegment& warp : document.tempo_map.warps) {
            if (is_interactive_event(event) && event_starts_inside_warp(event, warp)) {
                add_error(std::string("Interactive chart event '") + id + "' starts inside a warp segment.");
            } else if (event_spans_warp(event, warp)) {
                add_warning(std::string("Chart event '") + id + "' spans a warp segment; verify the timing intent.");
            }
        }
    }

    rhythm::TempoMap tempo_map;
    if (!tempo_map.rebuild(document.tempo_map)) {
        add_error(std::string("Chart tempo map failed validation: ") + std::string(tempo_map.last_error()));
    }

    return valid;
}

bool validate_chart_authoring_documents(
    const ContentValidationOptions& options,
    foundation::CrashSafeLog& log,
    std::vector<ContentCookIssue>& issues,
    ContentValidationSummary& summary,
    std::unordered_set<std::string>& referenced_audio_clip_ids) {
    bool valid = true;
    if (!std::filesystem::exists(summary.chart_manifest_path)) {
        append_issue(
            issues,
            log,
            ContentCookIssueSeverity::Warning,
            summary.chart_manifest_path,
            0,
            "No chart authoring manifest was found; chart validation was skipped.");
        return true;
    }

    bool fatal_error = false;
    const std::vector<AuthoredChartManifestEntry> manifest_entries = load_chart_manifest(
        summary.chart_manifest_path,
        log,
        issues,
        fatal_error);
    if (fatal_error) {
        return false;
    }

    summary.chart_manifest_entry_count = manifest_entries.size();
    for (const AuthoredChartManifestEntry& manifest_entry : manifest_entries) {
        ChartDocumentParser parser(manifest_entry, log, issues);
        std::optional<ChartDocument> document = parser.parse();
        if (!document) {
            valid = false;
            continue;
        }

        const ChartDocumentSummary document_summary = summarize_chart_document(*document);
        ++summary.validated_chart_count;
        summary.total_chart_events += document_summary.event_count;
        summary.total_interactive_cues += document_summary.interactive_cue_count;

        if (!document->audio.clip_id.empty()) {
            referenced_audio_clip_ids.insert(document->audio.clip_id);
        }

        if (options.validate_timing && !validate_chart_timing_consistency(manifest_entry, *document, log, issues)) {
            valid = false;
        }
    }

    return valid;
}

bool validate_audio_assets(
    const std::unordered_set<std::string>& referenced_audio_clip_ids,
    foundation::CrashSafeLog& log,
    std::vector<ContentCookIssue>& issues,
    ContentValidationSummary& summary) {
    bool valid = true;
    bool fatal_error = false;
    const bool audio_manifest_exists = std::filesystem::exists(summary.audio_manifest_path);
    const std::vector<AuthoredAudioClipManifestEntry> audio_entries = load_audio_manifest_entries(
        summary.audio_manifest_path,
        log,
        issues,
        fatal_error);
    if (fatal_error) {
        valid = false;
    }

    summary.audio_clip_count = audio_entries.size();
    summary.referenced_audio_clip_count = referenced_audio_clip_ids.size();

    std::unordered_set<std::string> known_audio_clip_ids;
    known_audio_clip_ids.reserve(audio_entries.size());
    for (const AuthoredAudioClipManifestEntry& entry : audio_entries) {
        known_audio_clip_ids.insert(entry.authoring_id);
        known_audio_clip_ids.insert(std::string("clip.") + entry.authoring_id);
    }

    if (!audio_manifest_exists && !referenced_audio_clip_ids.empty()) {
        summary.missing_audio_clip_reference_count = referenced_audio_clip_ids.size();
        append_issue(
            issues,
            log,
            ContentCookIssueSeverity::Error,
            summary.audio_manifest_path,
            0,
            "Charts reference audio clips, but the audio authoring manifest is missing.");
        return false;
    }

    for (const std::string& clip_id : referenced_audio_clip_ids) {
        if (!known_audio_clip_ids.contains(clip_id)) {
            ++summary.missing_audio_clip_reference_count;
            valid = false;
            append_issue(
                issues,
                log,
                ContentCookIssueSeverity::Error,
                summary.audio_manifest_path,
                0,
                std::string("Chart references unknown audio clip '") + clip_id + "'.");
        }
    }

    return valid;
}

bool validate_render_assets(
    const ContentValidationOptions& options,
    foundation::CrashSafeLog& log,
    std::vector<ContentCookIssue>& issues,
    ContentValidationSummary& summary) {
    bool valid = true;
    for (const auto& [kind, manifest_path] : {
             std::pair{RenderAssetKind::Texture, summary.texture_manifest_path},
             std::pair{RenderAssetKind::Mesh, summary.mesh_manifest_path},
             std::pair{RenderAssetKind::Font, summary.font_manifest_path}}) {
        if (std::filesystem::exists(manifest_path)) {
            ++summary.render_manifest_count;
        }

        bool fatal_error = false;
        const std::vector<AuthoredRenderManifestEntry> entries = load_render_manifest_entries(
            kind,
            manifest_path,
            options.paths.cooked_root,
            log,
            issues,
            fatal_error);
        summary.render_asset_count += entries.size();
        if (fatal_error) {
            valid = false;
        }
    }

    return valid;
}

} // namespace

bool ContentPipeline::cook_all(const ContentRootPaths& paths, foundation::CrashSafeLog& log) {
    clear();

    summary_.raw_root = std::filesystem::absolute(paths.raw_root);
    summary_.cooked_root = std::filesystem::absolute(paths.cooked_root);
    summary_.chart_manifest_source_path = summary_.raw_root / k_raw_chart_manifest_relative_path;
    summary_.chart_manifest_output_path = summary_.cooked_root / k_cooked_chart_manifest_relative_path;
    summary_.render_manifest_output_path = summary_.cooked_root / k_cooked_render_manifest_relative_path;
    const std::filesystem::path project_root = summary_.raw_root.parent_path().parent_path();
    const std::filesystem::path cooked_chart_root = summary_.chart_manifest_output_path.parent_path();
    const std::filesystem::path cooked_render_root = summary_.render_manifest_output_path.parent_path();

    bool fatal_error = false;
    if (!std::filesystem::exists(summary_.chart_manifest_source_path)) {
        append_issue(
            issues_,
            log,
            ContentCookIssueSeverity::Warning,
            summary_.chart_manifest_source_path,
            0,
            "No chart authoring manifest was found; chart cooking was skipped.");
    } else {
        const std::vector<AuthoredChartManifestEntry> manifest_entries = load_chart_manifest(
            summary_.chart_manifest_source_path,
            log,
            issues_,
            fatal_error);
        if (fatal_error) {
            return false;
        }

        std::vector<CookedChartDocumentRecord> cooked_records;
        cooked_records.reserve(manifest_entries.size());
        for (const AuthoredChartManifestEntry& manifest_entry : manifest_entries) {
            ChartDocumentParser parser(manifest_entry, log, issues_);
            std::optional<ChartDocument> document = parser.parse();
            if (!document) {
                return false;
            }

            const ChartDocumentSummary document_summary = summarize_chart_document(*document);

            const std::string file_stem = sanitize_file_stem(manifest_entry.authoring_id);
            const std::filesystem::path payload_relative_path = std::filesystem::path(file_stem + ".chart.ini");
            cooked_records.push_back(CookedChartDocumentRecord{
                .manifest_entry = CookedChartManifestEntry{
                    .authoring_id = manifest_entry.authoring_id,
                    .runtime_label = manifest_entry.runtime_label,
                    .source_path = manifest_entry.source_path,
                    .payload_path = payload_relative_path,
                },
                .document = std::move(*document),
                .summary = document_summary,
            });
        }

        std::sort(cooked_records.begin(), cooked_records.end(), [](const CookedChartDocumentRecord& lhs, const CookedChartDocumentRecord& rhs) {
            return lhs.manifest_entry.authoring_id < rhs.manifest_entry.authoring_id;
        });

        for (CookedChartDocumentRecord& record : cooked_records) {
            const std::filesystem::path payload_path = cooked_chart_root / record.manifest_entry.payload_path;
            const std::string payload_text = serialize_chart_document(record.document);
            record.manifest_entry.payload_hash = hash_text(payload_text);
            if (!write_text_file(payload_path, payload_text)) {
                append_issue(issues_, log, ContentCookIssueSeverity::Error, payload_path, 0, "Unable to write cooked chart payload.");
                return false;
            }
        }

        const std::string chart_manifest_text = serialize_chart_manifest(
            cooked_records,
            project_root,
            cooked_chart_root,
            log,
            issues_,
            fatal_error);
        if (fatal_error) {
            return false;
        }
        if (!write_text_file(summary_.chart_manifest_output_path, chart_manifest_text)) {
            append_issue(issues_, log, ContentCookIssueSeverity::Error, summary_.chart_manifest_output_path, 0, "Unable to write cooked chart manifest.");
            return false;
        }

        summary_.authored_chart_count = manifest_entries.size();
        summary_.cooked_chart_count = cooked_records.size();
        for (const CookedChartDocumentRecord& record : cooked_records) {
            summary_.total_chart_events += record.summary.event_count;
            summary_.total_interactive_cues += record.summary.interactive_cue_count;
        }
    }

    std::vector<AuthoredRenderManifestEntry> authored_render_entries;
    for (const auto& [kind, relative_manifest_path] : {
             std::pair{RenderAssetKind::Texture, std::filesystem::path(k_raw_texture_manifest_relative_path)},
             std::pair{RenderAssetKind::Mesh, std::filesystem::path(k_raw_mesh_manifest_relative_path)},
             std::pair{RenderAssetKind::Font, std::filesystem::path(k_raw_font_manifest_relative_path)}}) {
        std::vector<AuthoredRenderManifestEntry> family_entries = load_render_manifest_entries(
            kind,
            summary_.raw_root / relative_manifest_path,
            summary_.cooked_root,
            log,
            issues_,
            fatal_error);
        if (fatal_error) {
            return false;
        }
        authored_render_entries.insert(
            authored_render_entries.end(),
            std::make_move_iterator(family_entries.begin()),
            std::make_move_iterator(family_entries.end()));
    }

    std::sort(authored_render_entries.begin(), authored_render_entries.end(), [](const AuthoredRenderManifestEntry& lhs, const AuthoredRenderManifestEntry& rhs) {
        if (lhs.kind != rhs.kind) {
            return static_cast<std::uint8_t>(lhs.kind) < static_cast<std::uint8_t>(rhs.kind);
        }
        return lhs.authoring_id < rhs.authoring_id;
    });

    std::vector<CookedRenderManifestEntry> cooked_render_entries;
    cooked_render_entries.reserve(authored_render_entries.size());
    for (const AuthoredRenderManifestEntry& authored_entry : authored_render_entries) {
        std::optional<CookedRenderManifestEntry> cooked_entry = build_render_manifest_entry(
            authored_entry,
            project_root,
            cooked_render_root,
            log,
            issues_);
        if (!cooked_entry) {
            return false;
        }

        switch (cooked_entry->kind) {
        case RenderAssetKind::Texture:
            ++summary_.cooked_texture_count;
            break;
        case RenderAssetKind::Mesh:
            ++summary_.cooked_mesh_count;
            break;
        case RenderAssetKind::Font:
            ++summary_.cooked_font_count;
            break;
        }
        cooked_render_entries.push_back(std::move(*cooked_entry));
    }

    summary_.cooked_render_asset_count = cooked_render_entries.size();
    if (!write_text_file(summary_.render_manifest_output_path, serialize_render_manifest(cooked_render_entries))) {
        append_issue(issues_, log, ContentCookIssueSeverity::Error, summary_.render_manifest_output_path, 0, "Unable to write cooked render asset manifest.");
        return false;
    }

    return true;
}

void ContentPipeline::clear() noexcept {
    issues_.clear();
    summary_ = {};
}

const std::vector<ContentCookIssue>& ContentPipeline::issues() const noexcept {
    return issues_;
}

const ContentCookSummary& ContentPipeline::summary() const noexcept {
    return summary_;
}

std::optional<ContentRootPaths> find_default_content_roots() {
    const std::optional<std::string> raw_root = try_get_environment_value("REAKTIO_CONTENT_RAW_ROOT");
    const std::optional<std::string> cooked_root = try_get_environment_value("REAKTIO_CONTENT_COOKED_ROOT");
    if (raw_root && cooked_root) {
        return ContentRootPaths{
            .raw_root = std::filesystem::absolute(std::filesystem::path(*raw_root)),
            .cooked_root = std::filesystem::absolute(std::filesystem::path(*cooked_root)),
        };
    }

    std::filesystem::path current = std::filesystem::current_path();
    while (true) {
        const std::filesystem::path candidate_raw = current / "content" / "raw";
        const std::filesystem::path candidate_cooked = current / "content" / "cooked";
        if (std::filesystem::exists(candidate_raw) && std::filesystem::exists(candidate_cooked)) {
            return ContentRootPaths{
                .raw_root = std::filesystem::absolute(candidate_raw),
                .cooked_root = std::filesystem::absolute(candidate_cooked),
            };
        }

        const std::filesystem::path parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }

    return std::nullopt;
}

std::string_view to_string(ContentCookIssueSeverity severity) noexcept {
    switch (severity) {
    case ContentCookIssueSeverity::Warning:
        return "warning";
    case ContentCookIssueSeverity::Error:
        return "error";
    }

    return "unknown";
}

bool ContentValidator::validate_all(const ContentValidationOptions& options, foundation::CrashSafeLog& log) {
    clear();

    bool valid = true;
    if (options.paths.raw_root.empty()) {
        append_issue(issues_, log, ContentCookIssueSeverity::Error, {}, 0, "Content validation requires a raw content root.");
        refresh_validation_issue_counts(issues_, summary_);
        return false;
    }
    if (options.paths.cooked_root.empty()) {
        append_issue(issues_, log, ContentCookIssueSeverity::Error, {}, 0, "Content validation requires a cooked content root.");
        refresh_validation_issue_counts(issues_, summary_);
        return false;
    }

    summary_.raw_root = std::filesystem::absolute(options.paths.raw_root);
    summary_.cooked_root = std::filesystem::absolute(options.paths.cooked_root);
    summary_.chart_manifest_path = summary_.raw_root / k_raw_chart_manifest_relative_path;
    summary_.audio_manifest_path = summary_.raw_root / k_raw_audio_manifest_relative_path;
    summary_.texture_manifest_path = summary_.raw_root / k_raw_texture_manifest_relative_path;
    summary_.mesh_manifest_path = summary_.raw_root / k_raw_mesh_manifest_relative_path;
    summary_.font_manifest_path = summary_.raw_root / k_raw_font_manifest_relative_path;

    if (!options.validate_charts && !options.validate_timing && !options.validate_missing_assets) {
        append_issue(issues_, log, ContentCookIssueSeverity::Error, summary_.raw_root, 0, "Content validation requires at least one enabled check.");
        refresh_validation_issue_counts(issues_, summary_);
        return false;
    }

    if (!std::filesystem::exists(summary_.raw_root)) {
        append_issue(issues_, log, ContentCookIssueSeverity::Error, summary_.raw_root, 0, "Raw content root does not exist.");
        refresh_validation_issue_counts(issues_, summary_);
        return false;
    }
    if (!std::filesystem::exists(summary_.cooked_root)) {
        append_issue(issues_, log, ContentCookIssueSeverity::Warning, summary_.cooked_root, 0, "Cooked content root does not exist yet.");
    }

    std::unordered_set<std::string> referenced_audio_clip_ids;
    if (options.validate_charts || options.validate_timing || options.validate_missing_assets) {
        if (!validate_chart_authoring_documents(options, log, issues_, summary_, referenced_audio_clip_ids)) {
            valid = false;
        }
    }

    if (options.validate_missing_assets) {
        if (!validate_audio_assets(referenced_audio_clip_ids, log, issues_, summary_)) {
            valid = false;
        }
        if (!validate_render_assets(options, log, issues_, summary_)) {
            valid = false;
        }
    }

    refresh_validation_issue_counts(issues_, summary_);
    return valid && summary_.error_count == 0;
}

void ContentValidator::clear() noexcept {
    issues_.clear();
    summary_ = {};
}

const std::vector<ContentCookIssue>& ContentValidator::issues() const noexcept {
    return issues_;
}

const ContentValidationSummary& ContentValidator::summary() const noexcept {
    return summary_;
}

} // namespace reaktio::content