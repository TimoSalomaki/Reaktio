#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace {

struct ParsedKeyValue {
    std::string value;
    std::size_t line{};
};

using SectionValues = std::unordered_map<std::string, ParsedKeyValue>;
using SectionMap = std::unordered_map<std::string, SectionValues>;

struct FontDefinition {
    std::string authoring_id;
    std::string runtime_label;
    std::filesystem::path source_path;
    std::string output_name;
    std::uint32_t pixel_height{};
    std::uint32_t atlas_width{512};
    std::uint32_t atlas_height{512};
    std::uint32_t atlas_padding{2};
    bool sdf{};
    float line_spacing{1.0f};
    std::vector<std::string> fallback_ids;
    std::vector<std::uint32_t> codepoints;
};

struct LoadedFontFace {
    std::vector<unsigned char> file_bytes;
    stbtt_fontinfo info{};
};

struct BakedGlyph {
    std::uint32_t codepoint{};
    float advance{};
    float bearing_x{};
    float bearing_y{};
    float width{};
    float height{};
    std::array<float, 4> uv_rect{};
};

struct RasterizedGlyph {
    BakedGlyph glyph;
    std::vector<std::uint8_t> bitmap;
    std::size_t packed_x{};
    std::size_t packed_y{};
};

struct BakedFont {
    FontDefinition definition;
    float line_height{};
    float ascent{};
    float descent{};
    float line_gap{};
    std::vector<std::uint8_t> atlas_bytes;
    std::vector<BakedGlyph> glyphs;
};

struct ProgramOptions {
    std::filesystem::path manifest_path;
    std::filesystem::path cooked_root;
    std::optional<std::filesystem::path> stamp_path;
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

    while (!sanitized.empty() && sanitized.front() == '-') {
        sanitized.erase(sanitized.begin());
    }
    while (!sanitized.empty() && sanitized.back() == '-') {
        sanitized.pop_back();
    }

    return sanitized.empty() ? std::string("font") : sanitized;
}

void print_error(const std::filesystem::path& source_path, std::size_t line, std::string_view message) {
    std::cerr << "error: " << message;
    if (!source_path.empty()) {
        std::cerr << " [" << source_path.string();
        if (line > 0) {
            std::cerr << ':' << line;
        }
        std::cerr << ']';
    }
    std::cerr << '\n';
}

void print_warning(const std::filesystem::path& source_path, std::size_t line, std::string_view message) {
    std::cerr << "warning: " << message;
    if (!source_path.empty()) {
        std::cerr << " [" << source_path.string();
        if (line > 0) {
            std::cerr << ':' << line;
        }
        std::cerr << ']';
    }
    std::cerr << '\n';
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

std::optional<std::vector<unsigned char>> read_binary_file(const std::filesystem::path& path) {
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
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!input) {
            return std::nullopt;
        }
    }

    return bytes;
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

bool write_binary_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::error_code error_code;
    std::filesystem::create_directories(path.parent_path(), error_code);
    if (error_code) {
        return false;
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }

    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    return output.good();
}

SectionMap parse_sections(
    std::string_view text,
    const std::filesystem::path& source_path,
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
                print_error(source_path, line_number, "Encountered an empty section name.");
            }
            continue;
        }

        if (current_section.empty()) {
            fatal_error = true;
            print_error(source_path, line_number, "Entry appeared before a section header.");
            continue;
        }

        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            fatal_error = true;
            print_error(source_path, line_number, "Entry is missing '='.");
            continue;
        }

        const std::string key = trim_copy(std::string_view(line).substr(0, separator));
        const std::string value = trim_copy(std::string_view(line).substr(separator + 1));
        if (key.empty()) {
            fatal_error = true;
            print_error(source_path, line_number, "Entry key is empty.");
            continue;
        }

        auto& section = sections[current_section];
        if (section.contains(key)) {
            print_warning(source_path, line_number, "Duplicate key in section; last value wins.");
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

bool try_parse_uint32(std::string_view value, std::uint32_t& parsed) noexcept {
    const std::string buffer = trim_copy(value);
    if (buffer.empty() || buffer.front() == '-') {
        return false;
    }

    char* end = nullptr;
    const unsigned long converted = std::strtoul(buffer.c_str(), &end, 0);
    if (end == nullptr || *end != '\0' || converted > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    parsed = static_cast<std::uint32_t>(converted);
    return true;
}

bool try_parse_float(std::string_view value, float& parsed) noexcept {
    const std::string buffer = trim_copy(value);
    if (buffer.empty()) {
        return false;
    }

    char* end = nullptr;
    parsed = std::strtof(buffer.c_str(), &end);
    return end != nullptr && *end == '\0';
}

bool try_parse_bool(std::string_view value, bool& parsed) noexcept {
    const std::string lowered = lowercase_copy(value);
    if (lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on") {
        parsed = true;
        return true;
    }

    if (lowered == "false" || lowered == "0" || lowered == "no" || lowered == "off") {
        parsed = false;
        return true;
    }

    return false;
}

bool try_parse_codepoint(std::string_view value, std::uint32_t& codepoint) noexcept {
    const std::string buffer = trim_copy(value);
    if (buffer.empty()) {
        return false;
    }

    int base = 10;
    const char* digits = buffer.c_str();
    if (buffer.size() > 2 && (buffer[0] == 'U' || buffer[0] == 'u') && buffer[1] == '+') {
        base = 16;
        digits += 2;
    } else if (buffer.size() > 2 && buffer[0] == '0' && (buffer[1] == 'x' || buffer[1] == 'X')) {
        base = 16;
    }

    char* end = nullptr;
    const unsigned long converted = std::strtoul(digits, &end, base);
    if (end == nullptr || *end != '\0' || converted > 0x10ffffu) {
        return false;
    }

    codepoint = static_cast<std::uint32_t>(converted);
    return true;
}

bool parse_codepoint_range_token(std::string_view token, std::uint32_t& range_start, std::uint32_t& range_end) {
    const std::string buffer = trim_copy(token);
    const std::size_t separator = buffer.find('-');
    if (separator == std::string::npos) {
        if (!try_parse_codepoint(buffer, range_start)) {
            return false;
        }
        range_end = range_start;
        return true;
    }

    if (!try_parse_codepoint(std::string_view(buffer).substr(0, separator), range_start) ||
        !try_parse_codepoint(std::string_view(buffer).substr(separator + 1), range_end) ||
        range_end < range_start) {
        return false;
    }

    return true;
}

bool parse_codepoint_ranges(std::string_view value, std::set<std::uint32_t>& codepoints) {
    for (const std::string& token : split_string(value, ',')) {
        std::uint32_t range_start = 0;
        std::uint32_t range_end = 0;
        if (!parse_codepoint_range_token(token, range_start, range_end)) {
            return false;
        }
        for (std::uint32_t codepoint = range_start; codepoint <= range_end; ++codepoint) {
            codepoints.insert(codepoint);
            if (codepoint == 0x10ffffu) {
                break;
            }
        }
    }
    return true;
}

bool parse_codepoint_list(std::string_view value, std::set<std::uint32_t>& codepoints) {
    for (const std::string& token : split_string(value, ',')) {
        std::uint32_t codepoint = 0;
        if (!try_parse_codepoint(token, codepoint)) {
            return false;
        }
        codepoints.insert(codepoint);
    }
    return true;
}

std::string join_strings(const std::vector<std::string>& values, std::string_view delimiter) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            stream << delimiter;
        }
        stream << values[index];
    }
    return stream.str();
}

bool parse_suffixed_section(std::string_view section_name, std::string_view prefix, std::string& font_id, std::string& suffix) {
    if (section_name.rfind(prefix, 0) != 0) {
        return false;
    }

    const std::string_view remainder = section_name.substr(prefix.size());
    const std::size_t separator = remainder.rfind('.');
    if (separator == std::string_view::npos || separator == 0 || separator + 1 >= remainder.size()) {
        return false;
    }

    font_id = std::string(remainder.substr(0, separator));
    suffix = std::string(remainder.substr(separator + 1));
    return true;
}

bool parse_arguments(int argc, char** argv, ProgramOptions& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--manifest") {
            if (index + 1 >= argc) {
                std::cerr << "Missing value for --manifest\n";
                return false;
            }
            options.manifest_path = std::filesystem::absolute(std::filesystem::path(argv[++index]));
            continue;
        }
        if (argument == "--cooked-root") {
            if (index + 1 >= argc) {
                std::cerr << "Missing value for --cooked-root\n";
                return false;
            }
            options.cooked_root = std::filesystem::absolute(std::filesystem::path(argv[++index]));
            continue;
        }
        if (argument == "--stamp") {
            if (index + 1 >= argc) {
                std::cerr << "Missing value for --stamp\n";
                return false;
            }
            options.stamp_path = std::filesystem::absolute(std::filesystem::path(argv[++index]));
            continue;
        }
        if (argument == "--help" || argument == "-h") {
            std::cout << "Usage: reaktio_font_cooker --manifest <path> --cooked-root <path> [--stamp <path>]\n";
            return false;
        }

        std::cerr << "Unknown argument: " << argument << '\n';
        return false;
    }

    if (options.manifest_path.empty() || options.cooked_root.empty()) {
        std::cerr << "Both --manifest and --cooked-root are required.\n";
        return false;
    }

    return true;
}

bool load_font_definitions(
    const std::filesystem::path& manifest_path,
    std::vector<FontDefinition>& definitions) {
    const std::optional<std::string> text = read_text_file(manifest_path);
    if (!text) {
        print_error(manifest_path, 0, "Unable to read font authoring manifest.");
        return false;
    }

    bool fatal_error = false;
    const SectionMap sections = parse_sections(*text, manifest_path, fatal_error);
    if (fatal_error) {
        return false;
    }

    const std::filesystem::path manifest_dir = manifest_path.parent_path();
    std::unordered_map<std::string, FontDefinition> fonts;
    std::unordered_map<std::string, std::set<std::uint32_t>> codepoints_by_font;
    std::unordered_map<std::string, std::vector<std::pair<int, std::string>>> fallbacks_by_font;

    for (const auto& [section_name, values] : sections) {
        if (section_name == "meta") {
            continue;
        }

        if (section_name.rfind("font.", 0) == 0) {
            const ParsedKeyValue* runtime_label_value = find_value(sections, section_name, "runtime_label");
            const ParsedKeyValue* source_value = find_value(sections, section_name, "source");
            const ParsedKeyValue* pixel_height_value = find_value(sections, section_name, "pixel_height");
            if (runtime_label_value == nullptr || source_value == nullptr || pixel_height_value == nullptr) {
                print_error(manifest_path, 0, "Font section is missing runtime_label, source, or pixel_height.");
                return false;
            }

            FontDefinition definition{};
            definition.authoring_id = section_name.substr(std::string_view("font.").size());
            definition.runtime_label = runtime_label_value->value;
            definition.source_path = std::filesystem::absolute(manifest_dir / source_value->value);
            if (!try_parse_uint32(pixel_height_value->value, definition.pixel_height) || definition.pixel_height == 0) {
                print_error(manifest_path, pixel_height_value->line, "Font pixel_height must be a positive integer.");
                return false;
            }

            if (const ParsedKeyValue* atlas_width_value = find_value(sections, section_name, "atlas_width");
                atlas_width_value != nullptr) {
                if (!try_parse_uint32(atlas_width_value->value, definition.atlas_width) || definition.atlas_width == 0) {
                    print_error(manifest_path, atlas_width_value->line, "Font atlas_width must be a positive integer.");
                    return false;
                }
            }
            if (const ParsedKeyValue* atlas_height_value = find_value(sections, section_name, "atlas_height");
                atlas_height_value != nullptr) {
                if (!try_parse_uint32(atlas_height_value->value, definition.atlas_height) || definition.atlas_height == 0) {
                    print_error(manifest_path, atlas_height_value->line, "Font atlas_height must be a positive integer.");
                    return false;
                }
            }
            if (const ParsedKeyValue* atlas_padding_value = find_value(sections, section_name, "atlas_padding");
                atlas_padding_value != nullptr) {
                if (!try_parse_uint32(atlas_padding_value->value, definition.atlas_padding)) {
                    print_error(manifest_path, atlas_padding_value->line, "Font atlas_padding must be a non-negative integer.");
                    return false;
                }
            }
            if (const ParsedKeyValue* sdf_value = find_value(sections, section_name, "sdf"); sdf_value != nullptr) {
                if (!try_parse_bool(sdf_value->value, definition.sdf)) {
                    print_error(manifest_path, sdf_value->line, "Font sdf value is invalid.");
                    return false;
                }
            }
            if (const ParsedKeyValue* line_spacing_value = find_value(sections, section_name, "line_spacing");
                line_spacing_value != nullptr) {
                if (!try_parse_float(line_spacing_value->value, definition.line_spacing) || definition.line_spacing <= 0.0f) {
                    print_error(manifest_path, line_spacing_value->line, "Font line_spacing must be a positive number.");
                    return false;
                }
            }
            if (const ParsedKeyValue* output_name_value = find_value(sections, section_name, "output_name");
                output_name_value != nullptr) {
                definition.output_name = sanitize_file_stem(output_name_value->value);
            } else {
                definition.output_name = sanitize_file_stem(definition.authoring_id);
            }

            if (!std::filesystem::exists(definition.source_path)) {
                print_error(definition.source_path, 0, "Font source path does not exist.");
                return false;
            }

            fonts[definition.authoring_id] = std::move(definition);
            continue;
        }

        std::string font_id;
        std::string suffix;
        if (parse_suffixed_section(section_name, "charset.", font_id, suffix)) {
            const auto font_it = fonts.find(font_id);
            if (font_it == fonts.end()) {
                print_error(manifest_path, 0, "Charset section references an unknown font id.");
                return false;
            }

            std::set<std::uint32_t>& codepoints = codepoints_by_font[font_id];
            bool found_definition = false;
            if (const ParsedKeyValue* ranges_value = find_value(sections, section_name, "ranges"); ranges_value != nullptr) {
                if (!parse_codepoint_ranges(ranges_value->value, codepoints)) {
                    print_error(manifest_path, ranges_value->line, "Charset ranges entry is invalid.");
                    return false;
                }
                found_definition = true;
            }
            if (const ParsedKeyValue* codepoints_value = find_value(sections, section_name, "codepoints"); codepoints_value != nullptr) {
                if (!parse_codepoint_list(codepoints_value->value, codepoints)) {
                    print_error(manifest_path, codepoints_value->line, "Charset codepoints entry is invalid.");
                    return false;
                }
                found_definition = true;
            }
            if (!found_definition) {
                print_error(manifest_path, 0, "Charset section must define ranges or codepoints.");
                return false;
            }
            continue;
        }

        if (parse_suffixed_section(section_name, "fallback.", font_id, suffix)) {
            if (fonts.find(font_id) == fonts.end()) {
                print_error(manifest_path, 0, "Fallback section references an unknown font id.");
                return false;
            }
            int order = 0;
            try {
                order = std::stoi(suffix);
            } catch (...) {
                print_error(manifest_path, 0, "Fallback section suffix must be an integer order.");
                return false;
            }
            const ParsedKeyValue* fallback_font_value = find_value(sections, section_name, "font");
            if (fallback_font_value == nullptr) {
                print_error(manifest_path, 0, "Fallback section is missing the font key.");
                return false;
            }
            fallbacks_by_font[font_id].push_back({order, fallback_font_value->value});
            continue;
        }

        print_warning(manifest_path, 0, "Unknown font manifest section was ignored.");
    }

    if (fonts.empty()) {
        print_error(manifest_path, 0, "Font manifest did not define any fonts.");
        return false;
    }

    definitions.clear();
    definitions.reserve(fonts.size());
    for (auto& [authoring_id, definition] : fonts) {
        std::set<std::uint32_t>& codepoints = codepoints_by_font[authoring_id];
        if (codepoints.empty()) {
            for (std::uint32_t codepoint = 0x20u; codepoint <= 0x7eu; ++codepoint) {
                codepoints.insert(codepoint);
            }
        }
        definition.codepoints.assign(codepoints.begin(), codepoints.end());

        if (const auto fallback_it = fallbacks_by_font.find(authoring_id); fallback_it != fallbacks_by_font.end()) {
            std::vector<std::pair<int, std::string>> ordered = fallback_it->second;
            std::sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.first < rhs.first;
            });
            for (const auto& [order, fallback_id] : ordered) {
                (void)order;
                if (fonts.find(fallback_id) == fonts.end()) {
                    print_error(manifest_path, 0, "Fallback entry references an unknown fallback font id.");
                    return false;
                }
                definition.fallback_ids.push_back(fallback_id);
            }
        }

        definitions.push_back(definition);
    }

    std::sort(definitions.begin(), definitions.end(), [](const FontDefinition& lhs, const FontDefinition& rhs) {
        return lhs.authoring_id < rhs.authoring_id;
    });
    return true;
}

bool load_font_faces(
    const std::vector<FontDefinition>& definitions,
    std::unordered_map<std::string, LoadedFontFace>& faces) {
    faces.clear();
    faces.reserve(definitions.size());
    for (const FontDefinition& definition : definitions) {
        const std::optional<std::vector<unsigned char>> bytes = read_binary_file(definition.source_path);
        if (!bytes || bytes->empty()) {
            print_error(definition.source_path, 0, "Unable to read font source file.");
            return false;
        }

        LoadedFontFace face{};
        face.file_bytes = *bytes;
        if (!stbtt_InitFont(&face.info, face.file_bytes.data(), stbtt_GetFontOffsetForIndex(face.file_bytes.data(), 0))) {
            print_error(definition.source_path, 0, "Unable to initialize font source.");
            return false;
        }

        faces.emplace(definition.authoring_id, std::move(face));
    }

    return true;
}

const LoadedFontFace* resolve_face_for_codepoint(
    const FontDefinition& definition,
    std::uint32_t codepoint,
    const std::unordered_map<std::string, LoadedFontFace>& faces) {
    const auto primary_it = faces.find(definition.authoring_id);
    if (primary_it == faces.end()) {
        return nullptr;
    }

    if (stbtt_FindGlyphIndex(&primary_it->second.info, static_cast<int>(codepoint)) != 0) {
        return &primary_it->second;
    }

    for (const std::string& fallback_id : definition.fallback_ids) {
        const auto fallback_it = faces.find(fallback_id);
        if (fallback_it != faces.end() &&
            stbtt_FindGlyphIndex(&fallback_it->second.info, static_cast<int>(codepoint)) != 0) {
            return &fallback_it->second;
        }
    }

    return nullptr;
}

std::optional<RasterizedGlyph> rasterize_glyph(
    const FontDefinition& definition,
    const LoadedFontFace& face,
    std::uint32_t codepoint) {
    const float scale = stbtt_ScaleForPixelHeight(&face.info, static_cast<float>(definition.pixel_height));

    int advance_width = 0;
    int left_side_bearing = 0;
    stbtt_GetCodepointHMetrics(&face.info, static_cast<int>(codepoint), &advance_width, &left_side_bearing);

    RasterizedGlyph glyph{};
    glyph.glyph.codepoint = codepoint;
    glyph.glyph.advance = static_cast<float>(advance_width) * scale;

    if (definition.sdf) {
        const int padding = std::max<int>(1, static_cast<int>(definition.atlas_padding));
        const unsigned char onedge_value = 180;
        const float pixel_dist_scale = static_cast<float>(onedge_value) / static_cast<float>(padding);

        int width = 0;
        int height = 0;
        int xoff = 0;
        int yoff = 0;
        unsigned char* sdf_bitmap = stbtt_GetCodepointSDF(
            &face.info,
            scale,
            static_cast<int>(codepoint),
            padding,
            onedge_value,
            pixel_dist_scale,
            &width,
            &height,
            &xoff,
            &yoff);
        if (sdf_bitmap == nullptr) {
            return std::nullopt;
        }

        glyph.glyph.bearing_x = static_cast<float>(xoff);
        glyph.glyph.bearing_y = static_cast<float>(-yoff);
        glyph.glyph.width = static_cast<float>(width);
        glyph.glyph.height = static_cast<float>(height);
        glyph.bitmap.assign(sdf_bitmap, sdf_bitmap + static_cast<std::size_t>(width * height));
        stbtt_FreeSDF(sdf_bitmap, nullptr);
        return glyph;
    }

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetCodepointBitmapBox(
        &face.info,
        static_cast<int>(codepoint),
        scale,
        scale,
        &x0,
        &y0,
        &x1,
        &y1);

    const int width = std::max(0, x1 - x0);
    const int height = std::max(0, y1 - y0);
    glyph.glyph.bearing_x = static_cast<float>(x0);
    glyph.glyph.bearing_y = static_cast<float>(-y0);
    glyph.glyph.width = static_cast<float>(width);
    glyph.glyph.height = static_cast<float>(height);
    glyph.bitmap.resize(static_cast<std::size_t>(width * height), 0u);
    if (width > 0 && height > 0) {
        stbtt_MakeCodepointBitmap(
            &face.info,
            glyph.bitmap.data(),
            width,
            height,
            width,
            scale,
            scale,
            static_cast<int>(codepoint));
    }

    return glyph;
}

bool pack_glyphs(
    const FontDefinition& definition,
    std::vector<RasterizedGlyph>& glyphs,
    std::vector<std::uint8_t>& atlas_bytes) {
    atlas_bytes.assign(static_cast<std::size_t>(definition.atlas_width) * definition.atlas_height, 0u);

    std::vector<std::size_t> pack_order;
    pack_order.reserve(glyphs.size());
    for (std::size_t index = 0; index < glyphs.size(); ++index) {
        if (!glyphs[index].bitmap.empty()) {
            pack_order.push_back(index);
        }
    }

    std::sort(pack_order.begin(), pack_order.end(), [&glyphs](std::size_t lhs, std::size_t rhs) {
        if (glyphs[lhs].glyph.height != glyphs[rhs].glyph.height) {
            return glyphs[lhs].glyph.height > glyphs[rhs].glyph.height;
        }
        return glyphs[lhs].glyph.width > glyphs[rhs].glyph.width;
    });

    const std::size_t spacing = std::max<std::size_t>(1u, definition.atlas_padding);
    std::size_t cursor_x = spacing;
    std::size_t cursor_y = spacing;
    std::size_t row_height = 0;
    for (const std::size_t glyph_index : pack_order) {
        RasterizedGlyph& glyph = glyphs[glyph_index];
        const std::size_t glyph_width = static_cast<std::size_t>(glyph.glyph.width);
        const std::size_t glyph_height = static_cast<std::size_t>(glyph.glyph.height);
        if (glyph_width == 0 || glyph_height == 0) {
            continue;
        }

        if (cursor_x + glyph_width > definition.atlas_width - spacing) {
            cursor_x = spacing;
            cursor_y += row_height + spacing;
            row_height = 0;
        }
        if (cursor_y + glyph_height > definition.atlas_height - spacing) {
            return false;
        }

        glyph.packed_x = cursor_x;
        glyph.packed_y = cursor_y;
        row_height = std::max(row_height, glyph_height);

        for (std::size_t row = 0; row < glyph_height; ++row) {
            const std::size_t atlas_offset = (cursor_y + row) * definition.atlas_width + cursor_x;
            const std::size_t bitmap_offset = row * glyph_width;
            std::copy_n(glyph.bitmap.begin() + static_cast<std::ptrdiff_t>(bitmap_offset), glyph_width, atlas_bytes.begin() + static_cast<std::ptrdiff_t>(atlas_offset));
        }

        cursor_x += glyph_width + spacing;
    }

    for (RasterizedGlyph& glyph : glyphs) {
        if (glyph.bitmap.empty() || glyph.glyph.width == 0.0f || glyph.glyph.height == 0.0f) {
            glyph.glyph.uv_rect = {0.0f, 0.0f, 0.0f, 0.0f};
            continue;
        }

        const float atlas_width = static_cast<float>(definition.atlas_width);
        const float atlas_height = static_cast<float>(definition.atlas_height);
        glyph.glyph.uv_rect = {
            static_cast<float>(glyph.packed_x) / atlas_width,
            static_cast<float>(glyph.packed_y) / atlas_height,
            static_cast<float>(glyph.packed_x + static_cast<std::size_t>(glyph.glyph.width)) / atlas_width,
            static_cast<float>(glyph.packed_y + static_cast<std::size_t>(glyph.glyph.height)) / atlas_height,
        };
    }

    return true;
}

std::optional<BakedFont> bake_font(
    const FontDefinition& definition,
    const std::unordered_map<std::string, LoadedFontFace>& faces) {
    const auto primary_it = faces.find(definition.authoring_id);
    if (primary_it == faces.end()) {
        return std::nullopt;
    }

    BakedFont baked{};
    baked.definition = definition;
    const float scale = stbtt_ScaleForPixelHeight(&primary_it->second.info, static_cast<float>(definition.pixel_height));
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&primary_it->second.info, &ascent, &descent, &line_gap);
    baked.ascent = static_cast<float>(ascent) * scale;
    baked.descent = static_cast<float>(descent) * scale;
    baked.line_gap = static_cast<float>(line_gap) * scale;
    baked.line_height = (baked.ascent - baked.descent + baked.line_gap) * definition.line_spacing;

    std::vector<RasterizedGlyph> glyphs;
    glyphs.reserve(definition.codepoints.size());
    for (const std::uint32_t codepoint : definition.codepoints) {
        const LoadedFontFace* face = resolve_face_for_codepoint(definition, codepoint, faces);
        if (face == nullptr) {
            print_error(definition.source_path, 0, "Font bake encountered a codepoint with no glyph in the primary font or fallbacks.");
            return std::nullopt;
        }

        std::optional<RasterizedGlyph> glyph = rasterize_glyph(definition, *face, codepoint);
        if (!glyph) {
            print_error(definition.source_path, 0, "Font bake failed to rasterize a glyph.");
            return std::nullopt;
        }
        glyphs.push_back(std::move(*glyph));
    }

    if (!pack_glyphs(definition, glyphs, baked.atlas_bytes)) {
        print_error(definition.source_path, 0, "Font atlas is too small for the requested glyph set.");
        return std::nullopt;
    }

    baked.glyphs.reserve(glyphs.size());
    for (RasterizedGlyph& glyph : glyphs) {
        baked.glyphs.push_back(std::move(glyph.glyph));
    }
    std::sort(baked.glyphs.begin(), baked.glyphs.end(), [](const BakedGlyph& lhs, const BakedGlyph& rhs) {
        return lhs.codepoint < rhs.codepoint;
    });
    return baked;
}

std::string serialize_font_metadata(const BakedFont& baked) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(4);
    stream << "[font]\n";
    stream << "storage = raw-r8\n";
    stream << "atlas_payload = " << baked.definition.output_name << ".fontatlas.bin\n";
    stream << "pixel_height = " << baked.definition.pixel_height << "\n";
    stream << "line_height = " << baked.line_height << "\n";
    stream << "atlas_width = " << baked.definition.atlas_width << "\n";
    stream << "atlas_height = " << baked.definition.atlas_height << "\n";
    stream << "line_spacing = " << baked.definition.line_spacing << "\n";
    stream << "ascent = " << baked.ascent << "\n";
    stream << "descent = " << baked.descent << "\n";
    stream << "line_gap = " << baked.line_gap << "\n";
    stream << "sdf = " << (baked.definition.sdf ? "true" : "false") << "\n";
    if (!baked.definition.fallback_ids.empty()) {
        stream << "fallbacks = " << join_strings(baked.definition.fallback_ids, ",") << "\n";
    }

    for (const BakedGlyph& glyph : baked.glyphs) {
        stream << "glyph." << glyph.codepoint << " = "
               << glyph.advance << ", "
               << glyph.bearing_x << ", "
               << glyph.bearing_y << ", "
               << glyph.width << ", "
               << glyph.height << ", "
               << glyph.uv_rect[0] << ", "
               << glyph.uv_rect[1] << ", "
               << glyph.uv_rect[2] << ", "
               << glyph.uv_rect[3] << "\n";
    }

    return stream.str();
}

bool cook_fonts(const ProgramOptions& options) {
    std::vector<FontDefinition> definitions;
    if (!load_font_definitions(options.manifest_path, definitions)) {
        return false;
    }

    std::unordered_map<std::string, LoadedFontFace> faces;
    if (!load_font_faces(definitions, faces)) {
        return false;
    }

    std::size_t total_glyphs = 0;
    std::size_t total_atlas_bytes = 0;
    for (const FontDefinition& definition : definitions) {
        std::optional<BakedFont> baked = bake_font(definition, faces);
        if (!baked) {
            return false;
        }

        const std::filesystem::path atlas_output_path = options.cooked_root / (definition.output_name + ".fontatlas.bin");
        const std::filesystem::path metadata_output_path = options.cooked_root / (definition.output_name + ".font.ini");
        if (!write_binary_file(atlas_output_path, baked->atlas_bytes)) {
            print_error(atlas_output_path, 0, "Unable to write cooked font atlas payload.");
            return false;
        }
        if (!write_text_file(metadata_output_path, serialize_font_metadata(*baked))) {
            print_error(metadata_output_path, 0, "Unable to write cooked font metadata.");
            return false;
        }

        total_glyphs += baked->glyphs.size();
        total_atlas_bytes += baked->atlas_bytes.size();
        std::cout << "Cooked font id=" << definition.authoring_id
                  << " glyphs=" << baked->glyphs.size()
                  << " atlas=" << baked->definition.atlas_width << 'x' << baked->definition.atlas_height
                  << " sdf=" << (baked->definition.sdf ? 1 : 0)
                  << " payload=" << atlas_output_path.string() << '\n';
    }

    if (options.stamp_path) {
        std::ostringstream stamp_text;
        stamp_text << "fonts=" << definitions.size() << " glyphs=" << total_glyphs << " atlas_bytes=" << total_atlas_bytes << '\n';
        if (!write_text_file(*options.stamp_path, stamp_text.str())) {
            print_error(*options.stamp_path, 0, "Unable to write font cook stamp file.");
            return false;
        }
    }

    return true;
}

} // namespace

int main(int argc, char** argv) {
    ProgramOptions options{};
    if (!parse_arguments(argc, argv, options)) {
        return 1;
    }

    return cook_fonts(options) ? 0 : 1;
}