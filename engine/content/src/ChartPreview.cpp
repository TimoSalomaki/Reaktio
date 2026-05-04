#include "reaktio/content/ChartPreview.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace reaktio::content {

namespace {

struct ParsedKeyValue {
    std::string value;
    std::size_t line{};
};

using SectionValues = std::unordered_map<std::string, ParsedKeyValue>;
using SectionMap = std::unordered_map<std::string, SectionValues>;

constexpr std::string_view k_cooked_chart_manifest_schema = "reaktio.cooked.chart_manifest.v1";
constexpr std::string_view k_cooked_chart_schema = "reaktio.cooked.chart.v1";

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

std::optional<std::string> read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string make_error(const std::filesystem::path& path, std::size_t line, std::string_view message) {
    std::ostringstream stream;
    stream << message;
    if (!path.empty()) {
        stream << " [" << path.string();
        if (line > 0) {
            stream << ':' << line;
        }
        stream << ']';
    }
    return stream.str();
}

SectionMap parse_sections(
    std::string_view text,
    const std::filesystem::path& source_path,
    std::string& error_message) {
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
                error_message = make_error(source_path, line_number, "Encountered an empty section name.");
                return {};
            }
            continue;
        }

        if (current_section.empty()) {
            error_message = make_error(source_path, line_number, "Entry appeared before a section header.");
            return {};
        }

        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            error_message = make_error(source_path, line_number, "Entry is missing '='.");
            return {};
        }

        const std::string key = trim_copy(std::string_view(line).substr(0, separator));
        const std::string value = trim_copy(std::string_view(line).substr(separator + 1));
        if (key.empty()) {
            error_message = make_error(source_path, line_number, "Entry key is empty.");
            return {};
        }

        sections[current_section][key] = ParsedKeyValue{.value = value, .line = line_number};
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

std::vector<AuthoredField> collect_unknown_fields(
    const SectionValues& values,
    std::initializer_list<std::string_view> known_keys) {
    return collect_unknown_fields(values, std::span<const std::string_view>(known_keys.begin(), known_keys.size()));
}

std::optional<std::filesystem::path> find_project_root_from_manifest(const std::filesystem::path& manifest_path) {
    std::filesystem::path current = std::filesystem::absolute(manifest_path).parent_path();
    while (!current.empty()) {
        if (std::filesystem::exists(current / "content" / "raw") && std::filesystem::exists(current / "content" / "cooked")) {
            return current;
        }

        const std::filesystem::path parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }

    return std::nullopt;
}

std::filesystem::path resolve_relative_to(
    const std::filesystem::path& root,
    const std::string& value) {
    const std::filesystem::path path_value(value);
    if (path_value.is_absolute()) {
        return std::filesystem::absolute(path_value);
    }
    return std::filesystem::absolute(root / path_value);
}

void parse_dependency_hash_map(
    std::string_view value,
    std::unordered_map<std::string, std::string>& hashes) {
    hashes.clear();
    for (const std::string& token : split_string(value, ',')) {
        const std::size_t separator = token.find('=');
        if (separator == std::string::npos || separator == 0 || separator + 1 >= token.size()) {
            continue;
        }
        hashes.emplace(trim_copy(std::string_view(token).substr(0, separator)), trim_copy(std::string_view(token).substr(separator + 1)));
    }
}

struct CookedChartParser {
    const std::filesystem::path& source_path;
    const SectionMap& sections;
    std::string& error_message;
    std::unordered_set<std::string> scroll_profile_ids;
    std::unordered_set<std::string> event_ids;

    bool parse(ChartDocument& document) {
        if (!parse_chart_metadata(document) || !parse_audio(document) || !parse_tempo(document) || !parse_sections_into_document(document)) {
            return false;
        }

        if (!document.default_scroll_profile_id.empty() &&
            find_scroll_profile(document, document.default_scroll_profile_id) == nullptr) {
            error_message = make_error(source_path, 0, "Default scroll profile id does not exist in the cooked chart document.");
            return false;
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
                error_message = make_error(source_path, 0, "Cooked chart event references an unknown scroll profile id.");
                return false;
            }
        }

        rhythm::TempoMap tempo_map;
        if (!tempo_map.rebuild(document.tempo_map)) {
            error_message = make_error(source_path, 0, std::string("Cooked chart tempo map failed validation: ") + std::string(tempo_map.last_error()));
            return false;
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
        return true;
    }

  private:
    bool add_unique_id(
        std::unordered_set<std::string>& ids,
        std::string_view id,
        std::size_t line,
        std::string_view kind) {
        if (!ids.insert(std::string(id)).second) {
            error_message = make_error(source_path, line, std::string("Duplicate ") + std::string(kind) + " id '" + std::string(id) + "'.");
            return false;
        }
        return true;
    }

    bool parse_chart_metadata(ChartDocument& document) {
        const ParsedKeyValue* schema = find_value(sections, "chart", "schema");
        const ParsedKeyValue* id = find_value(sections, "chart", "id");
        const ParsedKeyValue* display_name = find_value(sections, "chart", "display_name");
        if (schema == nullptr || id == nullptr || display_name == nullptr) {
            error_message = make_error(source_path, 0, "Cooked chart [chart] section is missing required keys.");
            return false;
        }

        document.metadata.schema = schema->value;
        document.metadata.id = id->value;
        document.metadata.display_name = display_name->value;
        if (const ParsedKeyValue* author = find_value(sections, "chart", "author")) {
            document.metadata.author = author->value;
        }
        if (const ParsedKeyValue* description = find_value(sections, "chart", "description")) {
            document.metadata.description = description->value;
        }
        if (const ParsedKeyValue* tags = find_value(sections, "chart", "tags")) {
            document.metadata.tags = split_string(tags->value, ',');
        }
        if (const ParsedKeyValue* source_revision = find_value(sections, "chart", "source_revision")) {
            document.metadata.source_revision = source_revision->value;
        }
        if (document.metadata.schema != k_cooked_chart_schema) {
            error_message = make_error(source_path, schema->line, "Cooked chart schema id is not the expected cooked schema version.");
            return false;
        }
        return true;
    }

    bool parse_audio(ChartDocument& document) {
        const ParsedKeyValue* clip_id = find_value(sections, "audio", "clip_id");
        const ParsedKeyValue* preview_start_ms = find_value(sections, "audio", "preview_start_ms");
        const ParsedKeyValue* preview_end_ms = find_value(sections, "audio", "preview_end_ms");
        if (clip_id == nullptr || preview_start_ms == nullptr || preview_end_ms == nullptr) {
            error_message = make_error(source_path, 0, "Cooked chart [audio] section is missing required keys.");
            return false;
        }

        document.audio.clip_id = clip_id->value;
        if (!try_parse_integer(preview_start_ms->value, document.audio.preview_start_ms) ||
            !try_parse_integer(preview_end_ms->value, document.audio.preview_end_ms)) {
            error_message = make_error(source_path, preview_start_ms->line, "Cooked chart [audio] section contains invalid preview timing values.");
            return false;
        }
        if (const ParsedKeyValue* lead_in_ms = find_value(sections, "audio", "lead_in_ms")) {
            if (!try_parse_integer(lead_in_ms->value, document.audio.lead_in_ms)) {
                error_message = make_error(source_path, lead_in_ms->line, "Cooked chart [audio] lead_in_ms is invalid.");
                return false;
            }
        }
        if (const ParsedKeyValue* tail_out_ms = find_value(sections, "audio", "tail_out_ms")) {
            if (!try_parse_integer(tail_out_ms->value, document.audio.tail_out_ms)) {
                error_message = make_error(source_path, tail_out_ms->line, "Cooked chart [audio] tail_out_ms is invalid.");
                return false;
            }
        }
        if (document.audio.preview_end_ms < document.audio.preview_start_ms) {
            error_message = make_error(source_path, preview_end_ms->line, "Cooked chart preview_end_ms cannot be earlier than preview_start_ms.");
            return false;
        }
        return true;
    }

    bool parse_tempo(ChartDocument& document) {
        const ParsedKeyValue* ticks_per_quarter = find_value(sections, "tempo", "ticks_per_quarter");
        const ParsedKeyValue* beat_zero_offset_ms = find_value(sections, "tempo", "beat_zero_offset_ms");
        if (ticks_per_quarter == nullptr || beat_zero_offset_ms == nullptr) {
            error_message = make_error(source_path, 0, "Cooked chart [tempo] section is missing required keys.");
            return false;
        }

        if (!try_parse_integer(ticks_per_quarter->value, document.tempo_map.config.ticks_per_quarter_note) ||
            !try_parse_integer(beat_zero_offset_ms->value, document.beat_zero_offset_ms)) {
            error_message = make_error(source_path, ticks_per_quarter->line, "Cooked chart [tempo] section contains invalid values.");
            return false;
        }

        document.tempo_map.config.sample_rate_hz = 48000;
        if (const ParsedKeyValue* default_scroll_profile = find_value(sections, "tempo", "default_scroll_profile")) {
            document.default_scroll_profile_id = default_scroll_profile->value;
        }
        return true;
    }

    bool parse_sections_into_document(ChartDocument& document) {
        for (const auto& [section_name, values] : sections) {
            if (section_name == "chart" || section_name == "audio" || section_name == "tempo") {
                continue;
            }
            if (section_name.rfind("scroll_profile.", 0) == 0) {
                if (!parse_scroll_profile(section_name, values, document)) {
                    return false;
                }
                continue;
            }
            if (section_name.rfind("tempo_change.", 0) == 0) {
                if (!parse_tempo_change(values, document)) {
                    return false;
                }
                continue;
            }
            if (section_name.rfind("time_signature.", 0) == 0) {
                if (!parse_time_signature(values, document)) {
                    return false;
                }
                continue;
            }
            if (section_name.rfind("stop.", 0) == 0) {
                if (!parse_stop(values, document)) {
                    return false;
                }
                continue;
            }
            if (section_name.rfind("warp.", 0) == 0) {
                if (!parse_warp(values, document)) {
                    return false;
                }
                continue;
            }
            if (section_name.rfind("note.", 0) == 0) {
                if (!parse_note(section_name, values, document)) {
                    return false;
                }
                continue;
            }
            if (section_name.rfind("hold.", 0) == 0) {
                if (!parse_hold(section_name, values, document)) {
                    return false;
                }
                continue;
            }
            if (section_name.rfind("hazard.", 0) == 0) {
                if (!parse_hazard(section_name, values, document)) {
                    return false;
                }
                continue;
            }
            if (section_name.rfind("trigger.", 0) == 0) {
                if (!parse_trigger(section_name, values, document)) {
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
        }

        return true;
    }

    bool parse_scroll_profile(std::string_view section_name, const SectionValues& values, ChartDocument& document) {
        const std::string id(section_name.substr(std::string_view("scroll_profile.").size()));
        if (!add_unique_id(scroll_profile_ids, id, 0, "scroll-profile")) {
            return false;
        }

        const auto units_it = values.find("units_per_second");
        const auto spawn_it = values.find("spawn_lead_ticks");
        if (units_it == values.end() || spawn_it == values.end()) {
            error_message = make_error(source_path, 0, "Cooked chart scroll profile section is missing required keys.");
            return false;
        }

        ScrollProfileDefinition profile{};
        profile.id = id;
        if (!try_parse_double(units_it->second.value, profile.units_per_second) ||
            !try_parse_integer(spawn_it->second.value, profile.spawn_lead_ticks)) {
            error_message = make_error(source_path, units_it->second.line, "Cooked chart scroll profile section contains invalid values.");
            return false;
        }
        if (const auto release_it = values.find("release_tail_ticks"); release_it != values.end()) {
            if (!try_parse_integer(release_it->second.value, profile.release_tail_ticks)) {
                error_message = make_error(source_path, release_it->second.line, "Cooked chart scroll profile release_tail_ticks is invalid.");
                return false;
            }
        }
        profile.extensions = collect_unknown_fields(values, {"units_per_second", "spawn_lead_ticks", "release_tail_ticks"});
        document.scroll_profiles.push_back(std::move(profile));
        return true;
    }

    bool parse_tempo_change(const SectionValues& values, ChartDocument& document) {
        rhythm::TempoChange change{};
        const auto tick_it = values.find("tick");
        const auto micros_it = values.find("microseconds_per_quarter_note");
        if (tick_it == values.end() || micros_it == values.end() ||
            !try_parse_integer(tick_it->second.value, change.start_tick) ||
            !try_parse_integer(micros_it->second.value, change.microseconds_per_quarter_note) ||
            change.microseconds_per_quarter_note <= 0) {
            error_message = make_error(source_path, 0, "Cooked chart tempo_change section contains invalid values.");
            return false;
        }
        document.tempo_map.tempo_changes.push_back(change);
        return true;
    }

    bool parse_time_signature(const SectionValues& values, ChartDocument& document) {
        rhythm::TimeSignatureChange signature{};
        const auto tick_it = values.find("tick");
        const auto numerator_it = values.find("numerator");
        const auto denominator_it = values.find("denominator");
        if (tick_it == values.end() || numerator_it == values.end() || denominator_it == values.end() ||
            !try_parse_integer(tick_it->second.value, signature.start_tick) ||
            !try_parse_integer(numerator_it->second.value, signature.numerator) ||
            !try_parse_integer(denominator_it->second.value, signature.denominator) ||
            signature.numerator <= 0 || signature.denominator <= 0) {
            error_message = make_error(source_path, 0, "Cooked chart time_signature section contains invalid values.");
            return false;
        }
        document.tempo_map.time_signature_changes.push_back(signature);
        return true;
    }

    bool parse_stop(const SectionValues& values, ChartDocument& document) {
        rhythm::StopSegment stop{};
        const auto tick_it = values.find("tick");
        const auto duration_it = values.find("duration_microseconds");
        if (tick_it == values.end() || duration_it == values.end() ||
            !try_parse_integer(tick_it->second.value, stop.start_tick) ||
            !try_parse_integer(duration_it->second.value, stop.duration_microseconds) ||
            stop.duration_microseconds < 0) {
            error_message = make_error(source_path, 0, "Cooked chart stop section contains invalid values.");
            return false;
        }
        document.tempo_map.stops.push_back(stop);
        return true;
    }

    bool parse_warp(const SectionValues& values, ChartDocument& document) {
        rhythm::WarpSegment warp{};
        const auto tick_it = values.find("tick");
        const auto duration_it = values.find("duration_ticks");
        if (tick_it == values.end() || duration_it == values.end() ||
            !try_parse_integer(tick_it->second.value, warp.start_tick) ||
            !try_parse_integer(duration_it->second.value, warp.duration_ticks) ||
            warp.duration_ticks <= 0) {
            error_message = make_error(source_path, 0, "Cooked chart warp section contains invalid values.");
            return false;
        }
        document.tempo_map.warps.push_back(warp);
        return true;
    }

    bool parse_routed_cue_common(
        std::string_view section_name,
        std::string_view prefix,
        const SectionValues& values,
        ChartDocument& document,
        RoutedCueData& cue,
        bool require_positive_duration,
        std::initializer_list<std::string_view> extra_known_fields = {}) {
        const auto tick_it = values.find("tick");
        if (tick_it == values.end()) {
            error_message = make_error(source_path, 0, "Cooked chart cue section is missing tick.");
            return false;
        }

        cue.event.id = std::string(section_name.substr(prefix.size()));
        if (!add_unique_id(event_ids, cue.event.id, tick_it->second.line, "event")) {
            return false;
        }
        if (!try_parse_integer(tick_it->second.value, cue.event.placement.start_tick)) {
            error_message = make_error(source_path, tick_it->second.line, "Cooked chart cue tick is invalid.");
            return false;
        }
        if (const auto duration_it = values.find("duration_ticks"); duration_it != values.end()) {
            if (!try_parse_integer(duration_it->second.value, cue.event.placement.duration_ticks)) {
                error_message = make_error(source_path, duration_it->second.line, "Cooked chart cue duration_ticks is invalid.");
                return false;
            }
        }
        if (require_positive_duration && cue.event.placement.duration_ticks <= 0) {
            error_message = make_error(source_path, tick_it->second.line, "Cooked chart hold cue requires a positive duration_ticks value.");
            return false;
        }
        if (const auto lane_it = values.find("lane"); lane_it != values.end()) {
            std::uint32_t lane = 0;
            if (!try_parse_uint32(lane_it->second.value, lane)) {
                error_message = make_error(source_path, lane_it->second.line, "Cooked chart lane value is invalid.");
                return false;
            }
            cue.route.lane_index = lane;
        }
        if (const auto channel_it = values.find("channel"); channel_it != values.end()) {
            std::uint32_t channel = 0;
            if (!try_parse_uint32(channel_it->second.value, channel)) {
                error_message = make_error(source_path, channel_it->second.line, "Cooked chart channel value is invalid.");
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

        std::vector<std::string_view> known_fields = {
            "tick",
            "duration_ticks",
            "lane",
            "channel",
            "scroll_profile",
            "judgement_profile",
        };
        known_fields.insert(known_fields.end(), extra_known_fields.begin(), extra_known_fields.end());
        cue.event.extensions = collect_unknown_fields(values, std::span<const std::string_view>(known_fields.data(), known_fields.size()));
        (void)document;
        return true;
    }

    bool parse_note(std::string_view section_name, const SectionValues& values, ChartDocument& document) {
        RoutedCueData cue{};
        cue.scroll_profile_id = document.default_scroll_profile_id;
        if (!parse_routed_cue_common(section_name, "note.", values, document, cue, false)) {
            return false;
        }
        cue.event.placement.duration_ticks = 0;
        document.events.push_back(NoteCue{.cue = std::move(cue)});
        return true;
    }

    bool parse_hold(std::string_view section_name, const SectionValues& values, ChartDocument& document) {
        RoutedCueData cue{};
        cue.scroll_profile_id = document.default_scroll_profile_id;
        if (!parse_routed_cue_common(section_name, "hold.", values, document, cue, true)) {
            return false;
        }
        document.events.push_back(HoldCue{.cue = std::move(cue)});
        return true;
    }

    bool parse_hazard(std::string_view section_name, const SectionValues& values, ChartDocument& document) {
        RoutedCueData cue{};
        cue.scroll_profile_id = document.default_scroll_profile_id;
        if (!parse_routed_cue_common(section_name, "hazard.", values, document, cue, false, {"hazard_profile"})) {
            return false;
        }
        HazardCue hazard{.cue = std::move(cue)};
        if (const auto hazard_it = values.find("hazard_profile"); hazard_it != values.end()) {
            hazard.hazard_profile_id = hazard_it->second.value;
        }
        document.events.push_back(std::move(hazard));
        return true;
    }

    bool parse_trigger(std::string_view section_name, const SectionValues& values, ChartDocument& document) {
        const auto tick_it = values.find("tick");
        const auto trigger_it = values.find("trigger_id");
        if (tick_it == values.end() || trigger_it == values.end()) {
            error_message = make_error(source_path, 0, "Cooked chart trigger section is missing required keys.");
            return false;
        }
        TriggerEvent event{};
        event.event.id = std::string(section_name.substr(std::string_view("trigger.").size()));
        if (!add_unique_id(event_ids, event.event.id, tick_it->second.line, "event")) {
            return false;
        }
        if (!try_parse_integer(tick_it->second.value, event.event.placement.start_tick)) {
            error_message = make_error(source_path, tick_it->second.line, "Cooked chart trigger tick is invalid.");
            return false;
        }
        if (const auto duration_it = values.find("duration_ticks"); duration_it != values.end()) {
            if (!try_parse_integer(duration_it->second.value, event.event.placement.duration_ticks)) {
                error_message = make_error(source_path, duration_it->second.line, "Cooked chart trigger duration_ticks is invalid.");
                return false;
            }
        }
        event.trigger_id = trigger_it->second.value;
        if (const auto payload_it = values.find("payload"); payload_it != values.end()) {
            event.payload = payload_it->second.value;
        }
        event.event.extensions = collect_unknown_fields(values, {"tick", "duration_ticks", "trigger_id", "payload"});
        document.events.push_back(std::move(event));
        return true;
    }

    bool parse_camera(std::string_view section_name, const SectionValues& values, ChartDocument& document) {
        const auto tick_it = values.find("tick");
        const auto action_it = values.find("camera_action_id");
        if (tick_it == values.end() || action_it == values.end()) {
            error_message = make_error(source_path, 0, "Cooked chart camera section is missing required keys.");
            return false;
        }
        CameraEvent event{};
        event.event.id = std::string(section_name.substr(std::string_view("camera.").size()));
        if (!add_unique_id(event_ids, event.event.id, tick_it->second.line, "event")) {
            return false;
        }
        if (!try_parse_integer(tick_it->second.value, event.event.placement.start_tick)) {
            error_message = make_error(source_path, tick_it->second.line, "Cooked chart camera tick is invalid.");
            return false;
        }
        if (const auto duration_it = values.find("duration_ticks"); duration_it != values.end()) {
            if (!try_parse_integer(duration_it->second.value, event.event.placement.duration_ticks)) {
                error_message = make_error(source_path, duration_it->second.line, "Cooked chart camera duration_ticks is invalid.");
                return false;
            }
        }
        event.camera_action_id = action_it->second.value;
        if (const auto payload_it = values.find("payload"); payload_it != values.end()) {
            event.payload = payload_it->second.value;
        }
        event.event.extensions = collect_unknown_fields(values, {"tick", "duration_ticks", "camera_action_id", "payload"});
        document.events.push_back(std::move(event));
        return true;
    }

    bool parse_text_prompt(std::string_view section_name, const SectionValues& values, ChartDocument& document) {
        const auto tick_it = values.find("tick");
        if (tick_it == values.end()) {
            error_message = make_error(source_path, 0, "Cooked chart text_prompt section is missing tick.");
            return false;
        }
        TextPromptEvent event{};
        event.event.id = std::string(section_name.substr(std::string_view("text_prompt.").size()));
        if (!add_unique_id(event_ids, event.event.id, tick_it->second.line, "event")) {
            return false;
        }
        if (!try_parse_integer(tick_it->second.value, event.event.placement.start_tick)) {
            error_message = make_error(source_path, tick_it->second.line, "Cooked chart text_prompt tick is invalid.");
            return false;
        }
        if (const auto duration_it = values.find("duration_ticks"); duration_it != values.end()) {
            if (!try_parse_integer(duration_it->second.value, event.event.placement.duration_ticks)) {
                error_message = make_error(source_path, duration_it->second.line, "Cooked chart text_prompt duration_ticks is invalid.");
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
        event.event.extensions = collect_unknown_fields(values, {"tick", "duration_ticks", "prompt_text", "prompt_token", "locale_table_id"});
        document.events.push_back(std::move(event));
        return true;
    }

    bool parse_vfx(std::string_view section_name, const SectionValues& values, ChartDocument& document) {
        const auto tick_it = values.find("tick");
        const auto effect_it = values.find("effect_id");
        if (tick_it == values.end() || effect_it == values.end()) {
            error_message = make_error(source_path, 0, "Cooked chart vfx section is missing required keys.");
            return false;
        }
        VfxEvent event{};
        event.event.id = std::string(section_name.substr(std::string_view("vfx.").size()));
        if (!add_unique_id(event_ids, event.event.id, tick_it->second.line, "event")) {
            return false;
        }
        if (!try_parse_integer(tick_it->second.value, event.event.placement.start_tick)) {
            error_message = make_error(source_path, tick_it->second.line, "Cooked chart vfx tick is invalid.");
            return false;
        }
        if (const auto duration_it = values.find("duration_ticks"); duration_it != values.end()) {
            if (!try_parse_integer(duration_it->second.value, event.event.placement.duration_ticks)) {
                error_message = make_error(source_path, duration_it->second.line, "Cooked chart vfx duration_ticks is invalid.");
                return false;
            }
        }
        event.effect_id = effect_it->second.value;
        if (const auto payload_it = values.find("payload"); payload_it != values.end()) {
            event.payload = payload_it->second.value;
        }
        event.event.extensions = collect_unknown_fields(values, {"tick", "duration_ticks", "effect_id", "payload"});
        document.events.push_back(std::move(event));
        return true;
    }
};

std::string make_event_detail(const ChartEvent& event) {
    return std::visit(
        [](const auto& value) {
            std::ostringstream stream;
            using EventType = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<EventType, NoteCue>) {
                stream << "note";
            } else if constexpr (std::is_same_v<EventType, HoldCue>) {
                stream << "hold duration=" << value.cue.event.placement.duration_ticks;
            } else if constexpr (std::is_same_v<EventType, HazardCue>) {
                stream << "hazard";
                if (!value.hazard_profile_id.empty()) {
                    stream << " profile=" << value.hazard_profile_id;
                }
            } else if constexpr (std::is_same_v<EventType, TriggerEvent>) {
                stream << "trigger=" << value.trigger_id;
                if (!value.payload.empty()) {
                    stream << " payload=" << value.payload;
                }
            } else if constexpr (std::is_same_v<EventType, CameraEvent>) {
                stream << "camera_action=" << value.camera_action_id;
                if (!value.payload.empty()) {
                    stream << " payload=" << value.payload;
                }
            } else if constexpr (std::is_same_v<EventType, TextPromptEvent>) {
                if (!value.prompt_text.empty()) {
                    stream << "text=" << value.prompt_text;
                } else if (!value.prompt_token.empty()) {
                    stream << "token=" << value.prompt_token;
                } else if (!value.locale_table_id.empty()) {
                    stream << "locale=" << value.locale_table_id;
                } else {
                    stream << "text-prompt";
                }
            } else {
                stream << "effect=" << value.effect_id;
                if (!value.payload.empty()) {
                    stream << " payload=" << value.payload;
                }
            }
            return stream.str();
        },
        event);
}

std::optional<std::uint32_t> event_lane_index(const ChartEvent& event) {
    return std::visit(
        [](const auto& value) -> std::optional<std::uint32_t> {
            using EventType = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<EventType, NoteCue> || std::is_same_v<EventType, HoldCue> ||
                          std::is_same_v<EventType, HazardCue>) {
                return value.cue.route.lane_index;
            } else {
                return std::nullopt;
            }
        },
        event);
}

std::optional<std::uint32_t> event_channel_index(const ChartEvent& event) {
    return std::visit(
        [](const auto& value) -> std::optional<std::uint32_t> {
            using EventType = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<EventType, NoteCue> || std::is_same_v<EventType, HoldCue> ||
                          std::is_same_v<EventType, HazardCue>) {
                return value.cue.route.channel_index;
            } else {
                return std::nullopt;
            }
        },
        event);
}

std::string event_scroll_profile_id(const ChartEvent& event) {
    return std::visit(
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
}

std::string event_judgement_profile_id(const ChartEvent& event) {
    return std::visit(
        [](const auto& value) -> std::string {
            using EventType = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<EventType, NoteCue> || std::is_same_v<EventType, HoldCue> ||
                          std::is_same_v<EventType, HazardCue>) {
                return value.cue.judgement_profile_id;
            } else {
                return {};
            }
        },
        event);
}

} // namespace

std::optional<std::filesystem::path> find_default_cooked_chart_manifest_path() {
    std::filesystem::path current = std::filesystem::current_path();
    while (true) {
        const std::filesystem::path candidate = current / "content" / "cooked" / "charts" / "manifest.ini";
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

bool load_cooked_chart_manifest(
    const std::filesystem::path& manifest_path,
    std::vector<CookedChartManifestRecord>& records,
    std::string& error_message) {
    records.clear();
    const std::optional<std::string> text = read_text_file(manifest_path);
    if (!text) {
        error_message = make_error(manifest_path, 0, "Unable to read cooked chart manifest.");
        return false;
    }

    const SectionMap sections = parse_sections(*text, manifest_path, error_message);
    if (!error_message.empty()) {
        return false;
    }

    if (const ParsedKeyValue* schema_value = find_value(sections, "meta", "schema");
        schema_value != nullptr && lowercase_copy(schema_value->value) != k_cooked_chart_manifest_schema) {
        error_message = make_error(manifest_path, schema_value->line, "Cooked chart manifest schema is unexpected.");
        return false;
    }

    const std::filesystem::path manifest_directory = std::filesystem::absolute(manifest_path).parent_path();
    const std::optional<std::filesystem::path> project_root = find_project_root_from_manifest(manifest_path);

    for (const auto& [section_name, values] : sections) {
        if (section_name == "meta") {
            continue;
        }
        if (section_name.rfind("chart.", 0) != 0) {
            continue;
        }

        const auto runtime_label_it = values.find("runtime_label");
        const auto payload_it = values.find("payload");
        if (runtime_label_it == values.end() || payload_it == values.end()) {
            error_message = make_error(manifest_path, 0, "Cooked chart manifest section is missing runtime_label or payload.");
            return false;
        }

        CookedChartManifestRecord record{};
        record.authoring_id = section_name.substr(std::string_view("chart.").size());
        record.runtime_label = runtime_label_it->second.value;
        record.payload_path = resolve_relative_to(manifest_directory, payload_it->second.value);
        if (const auto source_it = values.find("source"); source_it != values.end()) {
            record.source_path = project_root ? resolve_relative_to(*project_root, source_it->second.value)
                                              : resolve_relative_to(manifest_directory, source_it->second.value);
        }
        if (const auto source_hash_it = values.find("source_hash"); source_hash_it != values.end()) {
            record.source_hash = source_hash_it->second.value;
        }
        if (const auto payload_hash_it = values.find("payload_hash"); payload_hash_it != values.end()) {
            record.payload_hash = payload_hash_it->second.value;
        }

        std::unordered_map<std::string, std::string> dependency_hashes;
        if (const auto dependency_hashes_it = values.find("dependency_hashes"); dependency_hashes_it != values.end()) {
            parse_dependency_hash_map(dependency_hashes_it->second.value, dependency_hashes);
        }
        if (const auto dependencies_it = values.find("dependencies"); dependencies_it != values.end()) {
            for (const std::string& dependency_token : split_string(dependencies_it->second.value, ',')) {
                record.dependencies.push_back(CookedChartDependencyRecord{
                    .path = resolve_relative_to(manifest_directory, dependency_token),
                    .hash = dependency_hashes.contains(dependency_token) ? dependency_hashes[dependency_token] : std::string{},
                });
            }
        }

        records.push_back(std::move(record));
    }

    std::sort(records.begin(), records.end(), [](const CookedChartManifestRecord& lhs, const CookedChartManifestRecord& rhs) {
        return lhs.authoring_id < rhs.authoring_id;
    });
    return true;
}

bool load_cooked_chart_document(
    const std::filesystem::path& cooked_chart_path,
    ChartDocument& document,
    std::string& error_message) {
    error_message.clear();
    const std::optional<std::string> text = read_text_file(cooked_chart_path);
    if (!text) {
        error_message = make_error(cooked_chart_path, 0, "Unable to read cooked chart payload.");
        return false;
    }

    const SectionMap sections = parse_sections(*text, cooked_chart_path, error_message);
    if (!error_message.empty()) {
        return false;
    }

    ChartDocument parsed_document{};
    CookedChartParser parser{.source_path = cooked_chart_path, .sections = sections, .error_message = error_message};
    if (!parser.parse(parsed_document)) {
        return false;
    }

    document = std::move(parsed_document);
    return true;
}

bool build_chart_preview_snapshot(
    const ChartDocument& document,
    rhythm::ChartTick cursor_tick,
    const ChartPreviewWindow& window,
    ChartPreviewSnapshot& snapshot,
    std::string& error_message) {
    error_message.clear();

    rhythm::TempoMap tempo_map;
    if (!tempo_map.rebuild(document.tempo_map)) {
        error_message = std::string("Chart tempo map failed validation: ") + std::string(tempo_map.last_error());
        return false;
    }

    const rhythm::ChartTick clamped_cursor_tick = std::max<rhythm::ChartTick>(0, cursor_tick);
    const rhythm::ChartTick before_ticks = std::max<rhythm::ChartTick>(0, window.before_ticks);
    const rhythm::ChartTick after_ticks = std::max<rhythm::ChartTick>(0, window.after_ticks);
    const rhythm::ChartTick window_start = std::max<rhythm::ChartTick>(0, clamped_cursor_tick - before_ticks);
    const rhythm::ChartTick window_end = clamped_cursor_tick + after_ticks;
    const std::size_t max_events = std::max<std::size_t>(window.max_events, 1u);

    std::vector<const ChartEvent*> in_window;
    in_window.reserve(document.events.size());
    for (const ChartEvent& event : document.events) {
        if (event_end_tick_exclusive(event) <= window_start || event_start_tick(event) > window_end) {
            continue;
        }
        in_window.push_back(&event);
    }

    std::size_t slice_start = 0;
    if (in_window.size() > max_events) {
        const auto pivot_it = std::lower_bound(
            in_window.begin(),
            in_window.end(),
            clamped_cursor_tick,
            [](const ChartEvent* event, rhythm::ChartTick tick) {
                return event_start_tick(*event) < tick;
            });
        const std::size_t pivot_index = static_cast<std::size_t>(std::distance(in_window.begin(), pivot_it));
        const std::size_t half_window = max_events / 2u;
        slice_start = pivot_index > half_window ? pivot_index - half_window : 0u;
        if (slice_start + max_events > in_window.size()) {
            slice_start = in_window.size() - max_events;
        }
    }

    snapshot = {};
    snapshot.summary = summarize_chart_document(document);
    snapshot.cursor = tempo_map.position_from_tick(clamped_cursor_tick);
    snapshot.window_start_tick = window_start;
    snapshot.window_end_tick = window_end;
    snapshot.total_window_event_count = in_window.size();
    snapshot.truncated = in_window.size() > max_events;

    const std::size_t slice_end = std::min<std::size_t>(in_window.size(), slice_start + max_events);
    snapshot.events.reserve(slice_end - slice_start);
    for (std::size_t index = slice_start; index < slice_end; ++index) {
        const ChartEvent& event = *in_window[index];
        const rhythm::ChartTick start_tick = event_start_tick(event);
        const rhythm::ChartTick end_tick = event_end_tick_exclusive(event);

        ChartPreviewEvent row{};
        row.kind = event_kind(event);
        row.id = event_id(event);
        row.start_tick = start_tick;
        row.end_tick_exclusive = end_tick;
        row.delta_ticks = start_tick - clamped_cursor_tick;
        row.delta_seconds = tempo_map.seconds_from_tick(start_tick) - tempo_map.seconds_from_tick(clamped_cursor_tick);
        row.start_position = tempo_map.position_from_tick(start_tick);
        row.end_position = tempo_map.position_from_tick(end_tick);
        row.lane_index = event_lane_index(event);
        row.channel_index = event_channel_index(event);
        row.scroll_profile_id = event_scroll_profile_id(event);
        row.judgement_profile_id = event_judgement_profile_id(event);
        row.detail = make_event_detail(event);
        row.relation = clamped_cursor_tick < start_tick ? ChartPreviewEventRelation::Upcoming
                                                        : (clamped_cursor_tick >= end_tick ? ChartPreviewEventRelation::Past
                                                                                           : ChartPreviewEventRelation::Active);
        snapshot.events.push_back(std::move(row));
    }

    return true;
}

std::string_view to_string(ChartPreviewEventRelation relation) noexcept {
    switch (relation) {
    case ChartPreviewEventRelation::Past:
        return "past";
    case ChartPreviewEventRelation::Active:
        return "active";
    case ChartPreviewEventRelation::Upcoming:
        return "upcoming";
    }

    return "unknown";
}

} // namespace reaktio::content