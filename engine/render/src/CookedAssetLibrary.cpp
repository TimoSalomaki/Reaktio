#include "reaktio/render/CookedAssetLibrary.hpp"

#include "reaktio/foundation/CrashSafeLog.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>

namespace reaktio::render {

namespace {

struct ParsedKeyValue {
    std::string value;
    std::size_t line{};
};

using SectionValues = std::unordered_map<std::string, ParsedKeyValue>;
using SectionMap = std::unordered_map<std::string, SectionValues>;

constexpr std::string_view k_default_manifest_relative_path = "content/cooked/render/manifest.ini";

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

std::vector<std::string> tokenize_whitespace_or_comma(std::string_view value) {
    std::vector<std::string> tokens;
    std::string current;
    for (const unsigned char character : value) {
        if (std::isspace(character) != 0 || character == ',') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }

        current.push_back(static_cast<char>(character));
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}

void log_message(
    foundation::CrashSafeLog& log,
    foundation::LogLevel level,
    const std::filesystem::path& source_path,
    std::size_t line,
    std::string_view message) {
    std::ostringstream stream;
    stream << message;
    if (!source_path.empty()) {
        stream << " [" << source_path.string();
        if (line > 0) {
            stream << ':' << line;
        }
        stream << ']';
    }
    log.write(level, stream.str());
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

std::optional<std::filesystem::path> configured_manifest_path_from_env() {
    if (const std::optional<std::string> configured_path =
            try_get_environment_value("REAKTIO_RENDER_ASSET_MANIFEST_PATH");
        configured_path) {
        return std::filesystem::absolute(std::filesystem::path(*configured_path));
    }

    return std::nullopt;
}

std::optional<std::filesystem::path> find_default_manifest_path() {
    std::filesystem::path current = std::filesystem::current_path();
    while (true) {
        const std::filesystem::path candidate = current / k_default_manifest_relative_path;
        if (std::filesystem::exists(candidate)) {
            return std::filesystem::absolute(candidate);
        }

        const std::filesystem::path parent = current.parent_path();
        if (parent == current) {
            break;
        }

        current = parent;
    }

    return std::nullopt;
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

SectionMap parse_sections(
    foundation::CrashSafeLog& log,
    const std::filesystem::path& source_path,
    std::string_view text,
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
                log_message(log, foundation::LogLevel::Error, source_path, line_number, "Encountered an empty section name.");
            }
            continue;
        }

        if (current_section.empty()) {
            fatal_error = true;
            log_message(log, foundation::LogLevel::Error, source_path, line_number, "Entry appeared before a section header.");
            continue;
        }

        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            fatal_error = true;
            log_message(log, foundation::LogLevel::Error, source_path, line_number, "Entry is missing '='.");
            continue;
        }

        const std::string key = trim_copy(std::string_view(line).substr(0, separator));
        const std::string value = trim_copy(std::string_view(line).substr(separator + 1));
        if (key.empty()) {
            fatal_error = true;
            log_message(log, foundation::LogLevel::Error, source_path, line_number, "Entry key is empty.");
            continue;
        }

        auto& section = sections[current_section];
        if (section.contains(key)) {
            log_message(
                log,
                foundation::LogLevel::Warning,
                source_path,
                line_number,
                "Duplicate key in section; last value wins.");
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

bool try_parse_unsigned(std::string_view value, std::uint32_t& parsed) noexcept {
    if (value.empty() || value.front() == '-') {
        return false;
    }

    const std::string buffer = trim_copy(value);
    char* end = nullptr;
    const unsigned long converted = std::strtoul(buffer.c_str(), &end, 0);
    if (end == nullptr || *end != '\0' || converted > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    parsed = static_cast<std::uint32_t>(converted);
    return true;
}

bool try_parse_u16(std::string_view value, std::uint16_t& parsed) noexcept {
    std::uint32_t converted = 0;
    if (!try_parse_unsigned(value, converted) || converted > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }

    parsed = static_cast<std::uint16_t>(converted);
    return true;
}

bool try_parse_float(std::string_view value, float& parsed) noexcept {
    const std::string buffer = trim_copy(value);
    char* end = nullptr;
    parsed = std::strtof(buffer.c_str(), &end);
    return end != nullptr && *end == '\0';
}

bool try_parse_texture_format(std::string_view value, CookedTextureFormat& parsed) noexcept {
    if (lowercase_copy(value) == "rgba8") {
        parsed = CookedTextureFormat::Rgba8;
        return true;
    }

    return false;
}

bool parse_hex_byte_list(std::string_view value, std::vector<std::uint8_t>& bytes) {
    const std::vector<std::string> tokens = tokenize_whitespace_or_comma(value);
    bytes.clear();
    bytes.reserve(tokens.size());
    for (const std::string& token : tokens) {
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(token.c_str(), &end, 16);
        if (end == nullptr || *end != '\0' || parsed > 0xffu) {
            return false;
        }
        bytes.push_back(static_cast<std::uint8_t>(parsed));
    }

    return true;
}

bool parse_position_list(std::string_view value, std::vector<std::array<float, 3>>& positions) {
    const std::vector<std::string> tuples = split_string(value, ';');
    positions.clear();
    positions.reserve(tuples.size());
    for (const std::string& tuple : tuples) {
        const std::vector<std::string> components = split_string(tuple, ',');
        if (components.size() != 3) {
            return false;
        }

        std::array<float, 3> position{};
        for (std::size_t index = 0; index < 3; ++index) {
            if (!try_parse_float(components[index], position[index])) {
                return false;
            }
        }

        positions.push_back(position);
    }

    return true;
}

bool parse_index_list(std::string_view value, std::vector<std::uint16_t>& indices) {
    const std::vector<std::string> components = split_string(value, ',');
    indices.clear();
    indices.reserve(components.size());
    for (const std::string& component : components) {
        std::uint16_t index = 0;
        if (!try_parse_u16(component, index)) {
            return false;
        }
        indices.push_back(index);
    }

    return true;
}

std::optional<TextureAssetRecord> load_texture_record(
    foundation::CrashSafeLog& log,
    foundation::ResourceRegistry& resource_registry,
    std::string_view authoring_id,
    std::string_view runtime_label,
    const std::filesystem::path& payload_path) {
    const std::optional<std::string> payload_text = read_text_file(payload_path);
    if (!payload_text) {
        log_message(log, foundation::LogLevel::Error, payload_path, 0, "Unable to read cooked texture payload.");
        return std::nullopt;
    }

    bool fatal_error = false;
    const SectionMap sections = parse_sections(log, payload_path, *payload_text, fatal_error);
    if (fatal_error) {
        return std::nullopt;
    }

    const ParsedKeyValue* width_value = find_value(sections, "texture", "width");
    const ParsedKeyValue* height_value = find_value(sections, "texture", "height");
    const ParsedKeyValue* format_value = find_value(sections, "texture", "format");
    const ParsedKeyValue* pixels_value = find_value(sections, "texture", "pixels");
    if (width_value == nullptr || height_value == nullptr || format_value == nullptr || pixels_value == nullptr) {
        log_message(log, foundation::LogLevel::Error, payload_path, 0, "Texture payload is missing required keys.");
        return std::nullopt;
    }

    TextureAssetRecord record{};
    record.authoring_id = std::string(authoring_id);
    record.runtime_label = std::string(runtime_label);
    record.payload_path = payload_path;
    if (!try_parse_u16(width_value->value, record.width) || !try_parse_u16(height_value->value, record.height) ||
        !try_parse_texture_format(format_value->value, record.format) ||
        !parse_hex_byte_list(pixels_value->value, record.pixel_bytes)) {
        log_message(log, foundation::LogLevel::Error, payload_path, 0, "Texture payload contains invalid values.");
        return std::nullopt;
    }

    const std::size_t expected_bytes = static_cast<std::size_t>(record.width) * record.height * 4u;
    if (record.pixel_bytes.size() != expected_bytes) {
        log_message(log, foundation::LogLevel::Error, payload_path, 0, "Texture payload byte count does not match dimensions.");
        return std::nullopt;
    }

    record.resource = resource_registry.register_resource(
        foundation::ResourceKind::Texture,
        record.authoring_id,
        record.runtime_label);
    return record.resource.valid() ? std::optional<TextureAssetRecord>{std::move(record)} : std::nullopt;
}

std::optional<MeshAssetRecord> load_mesh_record(
    foundation::CrashSafeLog& log,
    foundation::ResourceRegistry& resource_registry,
    std::string_view authoring_id,
    std::string_view runtime_label,
    const std::filesystem::path& payload_path) {
    const std::optional<std::string> payload_text = read_text_file(payload_path);
    if (!payload_text) {
        log_message(log, foundation::LogLevel::Error, payload_path, 0, "Unable to read cooked mesh payload.");
        return std::nullopt;
    }

    bool fatal_error = false;
    const SectionMap sections = parse_sections(log, payload_path, *payload_text, fatal_error);
    if (fatal_error) {
        return std::nullopt;
    }

    const ParsedKeyValue* positions_value = find_value(sections, "mesh", "positions");
    const ParsedKeyValue* indices_value = find_value(sections, "mesh", "indices");
    if (positions_value == nullptr || indices_value == nullptr) {
        log_message(log, foundation::LogLevel::Error, payload_path, 0, "Mesh payload is missing required keys.");
        return std::nullopt;
    }

    MeshAssetRecord record{};
    record.authoring_id = std::string(authoring_id);
    record.runtime_label = std::string(runtime_label);
    record.payload_path = payload_path;
    if (!parse_position_list(positions_value->value, record.positions) ||
        !parse_index_list(indices_value->value, record.indices)) {
        log_message(log, foundation::LogLevel::Error, payload_path, 0, "Mesh payload contains invalid values.");
        return std::nullopt;
    }

    record.resource = resource_registry.register_resource(
        foundation::ResourceKind::Mesh,
        record.authoring_id,
        record.runtime_label);
    return record.resource.valid() ? std::optional<MeshAssetRecord>{std::move(record)} : std::nullopt;
}

std::optional<FontAssetRecord> load_font_record(
    foundation::CrashSafeLog& log,
    foundation::ResourceRegistry& resource_registry,
    std::string_view authoring_id,
    std::string_view runtime_label,
    const std::filesystem::path& payload_path) {
    const std::optional<std::string> payload_text = read_text_file(payload_path);
    if (!payload_text) {
        log_message(log, foundation::LogLevel::Error, payload_path, 0, "Unable to read cooked font payload.");
        return std::nullopt;
    }

    bool fatal_error = false;
    const SectionMap sections = parse_sections(log, payload_path, *payload_text, fatal_error);
    if (fatal_error) {
        return std::nullopt;
    }

    const auto section_it = sections.find("font");
    if (section_it == sections.end()) {
        log_message(log, foundation::LogLevel::Error, payload_path, 0, "Font payload is missing the [font] section.");
        return std::nullopt;
    }

    FontAssetRecord record{};
    record.authoring_id = std::string(authoring_id);
    record.runtime_label = std::string(runtime_label);
    record.payload_path = payload_path;
    const ParsedKeyValue* line_height_value = find_value(sections, "font", "line_height");
    const ParsedKeyValue* atlas_width_value = find_value(sections, "font", "atlas_width");
    const ParsedKeyValue* atlas_height_value = find_value(sections, "font", "atlas_height");
    if (line_height_value == nullptr || atlas_width_value == nullptr || atlas_height_value == nullptr ||
        !try_parse_float(line_height_value->value, record.line_height) ||
        !try_parse_u16(atlas_width_value->value, record.atlas_width) ||
        !try_parse_u16(atlas_height_value->value, record.atlas_height)) {
        log_message(log, foundation::LogLevel::Error, payload_path, 0, "Font payload is missing required metadata.");
        return std::nullopt;
    }

    for (const auto& [key, parsed] : section_it->second) {
        constexpr std::string_view k_glyph_prefix = "glyph.";
        if (key.rfind(k_glyph_prefix, 0) != 0) {
            continue;
        }

        std::uint32_t codepoint = 0;
        if (!try_parse_unsigned(key.substr(k_glyph_prefix.size()), codepoint)) {
            log_message(log, foundation::LogLevel::Error, payload_path, parsed.line, "Font glyph key has an invalid codepoint.");
            return std::nullopt;
        }

        const std::vector<std::string> values = split_string(parsed.value, ',');
        if (values.size() != 7u) {
            log_message(log, foundation::LogLevel::Error, payload_path, parsed.line, "Font glyph entry must contain 7 values.");
            return std::nullopt;
        }

        FontGlyphRecord glyph{.codepoint = static_cast<char32_t>(codepoint)};
        if (!try_parse_float(values[0], glyph.advance) ||
            !try_parse_float(values[1], glyph.bearing_x) ||
            !try_parse_float(values[2], glyph.bearing_y) ||
            !try_parse_float(values[3], glyph.uv_rect[0]) ||
            !try_parse_float(values[4], glyph.uv_rect[1]) ||
            !try_parse_float(values[5], glyph.uv_rect[2]) ||
            !try_parse_float(values[6], glyph.uv_rect[3])) {
            log_message(log, foundation::LogLevel::Error, payload_path, parsed.line, "Font glyph entry contains invalid values.");
            return std::nullopt;
        }

        record.glyphs.push_back(glyph);
    }

    record.resource = resource_registry.register_resource(
        foundation::ResourceKind::Font,
        record.authoring_id,
        record.runtime_label);
    return record.resource.valid() ? std::optional<FontAssetRecord>{std::move(record)} : std::nullopt;
}

} // namespace

bool CookedAssetLibrary::load(
    foundation::ResourceRegistry& resource_registry,
    foundation::CrashSafeLog& log) {
    clear();

    std::optional<std::filesystem::path> manifest_path = configured_manifest_path_from_env();
    if (manifest_path && !std::filesystem::exists(*manifest_path)) {
        log_message(
            log,
            foundation::LogLevel::Warning,
            *manifest_path,
            0,
            "Configured cooked render asset manifest does not exist; falling back to default search.");
        manifest_path.reset();
    }

    if (!manifest_path) {
        manifest_path = find_default_manifest_path();
    }

    if (!manifest_path) {
        log.write(foundation::LogLevel::Warning, "No cooked render asset manifest was found; continuing without cooked assets.");
        return true;
    }

    summary_.manifest_path = *manifest_path;
    summary_.loaded_from_manifest = true;

    const std::optional<std::string> manifest_text = read_text_file(*manifest_path);
    if (!manifest_text) {
        log_message(log, foundation::LogLevel::Error, *manifest_path, 0, "Unable to read cooked render asset manifest.");
        clear();
        return false;
    }

    bool fatal_error = false;
    const SectionMap sections = parse_sections(log, *manifest_path, *manifest_text, fatal_error);
    if (fatal_error) {
        clear();
        return false;
    }

    const std::filesystem::path manifest_directory = manifest_path->parent_path();
    for (const auto& [section_name, values] : sections) {
        const auto runtime_label_it = values.find("runtime_label");
        const auto payload_it = values.find("payload");
        if (runtime_label_it == values.end() || payload_it == values.end()) {
            log_message(log, foundation::LogLevel::Error, *manifest_path, 0, "Cooked asset section is missing runtime_label or payload.");
            clear();
            return false;
        }

        const std::filesystem::path payload_path = std::filesystem::absolute(manifest_directory / payload_it->second.value);
        if (section_name.rfind("texture.", 0) == 0) {
            const std::string authoring_id = section_name.substr(std::string_view("texture.").size());
            std::optional<TextureAssetRecord> record = load_texture_record(
                log,
                resource_registry,
                authoring_id,
                runtime_label_it->second.value,
                payload_path);
            if (!record) {
                clear();
                return false;
            }
            summary_.total_payload_bytes += record->pixel_bytes.size();
            textures_.emplace(record->resource.value(), std::move(*record));
            ++summary_.texture_count;
            continue;
        }

        if (section_name.rfind("mesh.", 0) == 0) {
            const std::string authoring_id = section_name.substr(std::string_view("mesh.").size());
            std::optional<MeshAssetRecord> record = load_mesh_record(
                log,
                resource_registry,
                authoring_id,
                runtime_label_it->second.value,
                payload_path);
            if (!record) {
                clear();
                return false;
            }
            summary_.total_payload_bytes +=
                record->positions.size() * sizeof(std::array<float, 3>) +
                record->indices.size() * sizeof(std::uint16_t);
            meshes_.emplace(record->resource.value(), std::move(*record));
            ++summary_.mesh_count;
            continue;
        }

        if (section_name.rfind("font.", 0) == 0) {
            const std::string authoring_id = section_name.substr(std::string_view("font.").size());
            std::optional<FontAssetRecord> record = load_font_record(
                log,
                resource_registry,
                authoring_id,
                runtime_label_it->second.value,
                payload_path);
            if (!record) {
                clear();
                return false;
            }
            summary_.total_payload_bytes += record->glyphs.size() * sizeof(FontGlyphRecord);
            fonts_.emplace(record->resource.value(), std::move(*record));
            ++summary_.font_count;
            continue;
        }

        log_message(log, foundation::LogLevel::Warning, *manifest_path, 0, "Unknown cooked asset section was ignored.");
    }

    return true;
}

void CookedAssetLibrary::clear() noexcept {
    textures_.clear();
    meshes_.clear();
    fonts_.clear();
    summary_ = {};
}

const TextureAssetRecord* CookedAssetLibrary::try_get_texture(foundation::ResourceHandle resource) const noexcept {
    const auto it = textures_.find(resource.value());
    return it != textures_.end() ? &it->second : nullptr;
}

const MeshAssetRecord* CookedAssetLibrary::try_get_mesh(foundation::ResourceHandle resource) const noexcept {
    const auto it = meshes_.find(resource.value());
    return it != meshes_.end() ? &it->second : nullptr;
}

const FontAssetRecord* CookedAssetLibrary::try_get_font(foundation::ResourceHandle resource) const noexcept {
    const auto it = fonts_.find(resource.value());
    return it != fonts_.end() ? &it->second : nullptr;
}

const CookedAssetLibrarySummary& CookedAssetLibrary::summary() const noexcept {
    return summary_;
}

} // namespace reaktio::render