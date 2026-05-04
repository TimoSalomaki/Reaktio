#include "reaktio/app/RuntimeConfiguration.hpp"

#include "reaktio/gameplay/Modifiers.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace reaktio::app {

namespace {

struct ParsedKeyValue {
    std::string value;
    std::size_t line{};
};

using SectionValues = std::unordered_map<std::string, ParsedKeyValue>;
using SectionMap = std::unordered_map<std::string, SectionValues>;

constexpr std::string_view k_default_config_relative_path = "content/cooked/config/runtime-smoke.ini";

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

void add_issue(
    RuntimeConfigurationLoadResult& result,
    bool fatal,
    std::size_t line,
    std::string message) {
    result.issues.push_back(RuntimeConfigurationIssue{
        .source_path = result.source_path,
        .line = line,
        .message = std::move(message),
        .fatal = fatal,
    });
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

std::optional<std::filesystem::path> configured_config_path_from_env() {
    if (const std::optional<std::string> configured_path = try_get_environment_value("REAKTIO_CONFIG_PATH"); configured_path) {
        return std::filesystem::absolute(std::filesystem::path(*configured_path));
    }

    return std::nullopt;
}

std::optional<std::filesystem::path> find_default_config_path() {
    std::filesystem::path current = std::filesystem::current_path();
    while (true) {
        const std::filesystem::path candidate = current / k_default_config_relative_path;
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

SectionMap parse_sections(RuntimeConfigurationLoadResult& result, std::string_view text) {
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
                add_issue(result, true, line_number, "Encountered an empty configuration section name.");
            }
            continue;
        }

        if (current_section.empty()) {
            add_issue(result, true, line_number, "Configuration entry appeared before any section header.");
            continue;
        }

        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            add_issue(result, true, line_number, "Configuration entry is missing '='.");
            continue;
        }

        const std::string key = trim_copy(std::string_view(line).substr(0, separator));
        const std::string value = trim_copy(std::string_view(line).substr(separator + 1));
        if (key.empty()) {
            add_issue(result, true, line_number, "Configuration entry key is empty.");
            continue;
        }

        auto& section = sections[current_section];
        if (section.contains(key)) {
            add_issue(
                result,
                false,
                line_number,
                "Duplicate configuration key '" + key + "' in section [" + current_section + "]; last value wins.");
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

void warn_unknown_keys(
    RuntimeConfigurationLoadResult& result,
    std::string_view section_name,
    const SectionValues& values,
    std::initializer_list<std::string_view> known_keys) {
    for (const auto& [key, parsed] : values) {
        const bool known = std::find(known_keys.begin(), known_keys.end(), key) != known_keys.end();
        if (!known) {
            add_issue(
                result,
                false,
                parsed.line,
                "Unknown configuration key '" + key + "' in section [" + std::string(section_name) + "].");
        }
    }
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

template <typename Unsigned>
bool try_parse_unsigned(std::string_view value, Unsigned& parsed) noexcept {
    if (value.empty() || value.front() == '-') {
        return false;
    }

    std::string buffer = trim_copy(value);
    errno = 0;
    char* end = nullptr;
    const unsigned long long converted = std::strtoull(buffer.c_str(), &end, 0);
    if (errno == ERANGE || end == nullptr || *end != '\0' ||
        converted > static_cast<unsigned long long>(std::numeric_limits<Unsigned>::max())) {
        return false;
    }

    parsed = static_cast<Unsigned>(converted);
    return true;
}

bool try_parse_double(std::string_view value, double& parsed) noexcept {
    std::string buffer = trim_copy(value);
    char* end = nullptr;
    parsed = std::strtod(buffer.c_str(), &end);
    return end != nullptr && *end == '\0';
}

bool try_parse_renderer_backend(std::string_view value, platform::RendererBackendPreference& parsed) noexcept {
    const std::string lowered = lowercase_copy(value);
    if (lowered == "automatic") {
        parsed = platform::RendererBackendPreference::Automatic;
    } else if (lowered == "noop") {
        parsed = platform::RendererBackendPreference::Noop;
    } else if (lowered == "direct3d11") {
        parsed = platform::RendererBackendPreference::Direct3D11;
    } else if (lowered == "direct3d12") {
        parsed = platform::RendererBackendPreference::Direct3D12;
    } else if (lowered == "vulkan") {
        parsed = platform::RendererBackendPreference::Vulkan;
    } else if (lowered == "opengl") {
        parsed = platform::RendererBackendPreference::OpenGL;
    } else if (lowered == "opengles") {
        parsed = platform::RendererBackendPreference::OpenGLES;
    } else if (lowered == "metal") {
        parsed = platform::RendererBackendPreference::Metal;
    } else if (lowered == "webgpu") {
        parsed = platform::RendererBackendPreference::WebGPU;
    } else {
        return false;
    }

    return true;
}

bool try_parse_window_mode(std::string_view value, platform::WindowMode& parsed) noexcept {
    const std::string lowered = lowercase_copy(value);
    if (lowered == "windowed") {
        parsed = platform::WindowMode::Windowed;
    } else if (lowered == "borderless-windowed") {
        parsed = platform::WindowMode::BorderlessWindowed;
    } else if (lowered == "fullscreen") {
        parsed = platform::WindowMode::Fullscreen;
    } else {
        return false;
    }

    return true;
}

bool try_parse_audio_sample_format(std::string_view value, platform::AudioSampleFormat& parsed) noexcept {
    const std::string lowered = lowercase_copy(value);
    if (lowered == "f32") {
        parsed = platform::AudioSampleFormat::F32;
    } else if (lowered == "s16") {
        parsed = platform::AudioSampleFormat::S16;
    } else {
        return false;
    }

    return true;
}

template <typename Value, typename ParseFunc>
void load_typed_value(
    RuntimeConfigurationLoadResult& result,
    const SectionMap& sections,
    std::string_view section_name,
    std::string_view key_name,
    std::string_view description,
    Value& target,
    ParseFunc&& parse) {
    if (const ParsedKeyValue* parsed_value = find_value(sections, section_name, key_name)) {
        Value parsed = target;
        if (!parse(parsed_value->value, parsed)) {
            add_issue(
                result,
                true,
                parsed_value->line,
                "Invalid " + std::string(description) + " for [" + std::string(section_name) + "] " +
                    std::string(key_name) + ".");
            return;
        }

        target = parsed;
    }
}

void load_string_value(
    const SectionMap& sections,
    std::string_view section_name,
    std::string_view key_name,
    std::string& target) {
    if (const ParsedKeyValue* parsed_value = find_value(sections, section_name, key_name)) {
        target = parsed_value->value;
    }
}

std::vector<std::string> split_csv(std::string_view value) {
    std::vector<std::string> values;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find(',', start);
        std::string item = trim_copy(end == std::string_view::npos ? value.substr(start) : value.substr(start, end - start));
        if (!item.empty()) {
            values.push_back(std::move(item));
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return values;
}

void set_runtime_action_binding(
    RuntimeConfiguration& configuration,
    std::string_view context_id,
    std::string_view action_id,
    std::string_view primary,
    std::string_view secondary = {},
    std::string_view device_profile_id = gameplay::k_default_input_device_profile_id) {
    configuration.input_bindings.set_action_binding(action_id, primary, secondary);
    configuration.input_action_maps.set_binding(context_id, action_id, primary, secondary, device_profile_id);
}

void load_path_value(
    const std::filesystem::path& base_directory,
    const SectionMap& sections,
    std::string_view section_name,
    std::string_view key_name,
    std::filesystem::path& target) {
    if (const ParsedKeyValue* parsed_value = find_value(sections, section_name, key_name)) {
        const std::filesystem::path parsed_path(parsed_value->value);
        target = parsed_path.is_absolute()
            ? std::filesystem::absolute(parsed_path)
            : std::filesystem::absolute(base_directory / parsed_path);
    }
}

void load_input_binding_section(
    RuntimeConfigurationLoadResult& result,
    std::string_view section_name,
    const SectionValues& values) {
    constexpr std::string_view k_prefix = "input_binding.";
    std::string context_id{gameplay::k_default_input_context_id};
    std::string action_id = std::string(section_name.substr(k_prefix.size()));
    if (const std::size_t separator = action_id.find('.'); separator != std::string::npos) {
        context_id = action_id.substr(0, separator);
        action_id = action_id.substr(separator + 1);
    }

    if (const auto it = values.find("context"); it != values.end()) {
        context_id = it->second.value;
    }
    if (const auto it = values.find("action"); it != values.end()) {
        action_id = it->second.value;
    }
    if (action_id.empty()) {
        add_issue(result, true, 0, "Input-binding section name is missing an action id.");
        return;
    }
    if (context_id.empty()) {
        add_issue(result, true, 0, "Input-binding section is missing an input context id.");
        return;
    }

    std::string device_profile_id{gameplay::k_default_input_device_profile_id};
    std::string primary;
    std::string secondary;
    if (const auto it = values.find("profile"); it != values.end()) {
        device_profile_id = it->second.value;
    }
    if (const auto it = values.find("device_profile"); it != values.end()) {
        device_profile_id = it->second.value;
    }
    if (const auto it = values.find("primary"); it != values.end()) {
        primary = it->second.value;
    }
    if (const auto it = values.find("secondary"); it != values.end()) {
        secondary = it->second.value;
    }

    if (primary.empty() && secondary.empty()) {
        add_issue(
            result,
            false,
            0,
            "Input-binding section [" + std::string(section_name) + "] did not define any bindings.");
        return;
    }

    warn_unknown_keys(result, section_name, values, {"context", "action", "profile", "device_profile", "primary", "secondary"});
    set_runtime_action_binding(result.configuration, context_id, action_id, primary, secondary, device_profile_id);
}

void load_input_section(
    RuntimeConfigurationLoadResult& result,
    const SectionValues& values) {
    if (const auto it = values.find("active_device_profile"); it != values.end()) {
        if (it->second.value.empty()) {
            add_issue(result, true, it->second.line, "Input active_device_profile cannot be empty.");
        } else {
            result.configuration.input_action_maps.set_active_device_profile(it->second.value);
        }
    }

    if (const auto it = values.find("active_contexts"); it != values.end()) {
        result.configuration.input_action_maps.clear_active_contexts();
        for (const std::string& context_id : split_csv(it->second.value)) {
            result.configuration.input_action_maps.set_context_active(context_id, true);
        }
    }

    warn_unknown_keys(result, "input", values, {"active_device_profile", "active_contexts"});
}

void load_input_context_section(
    RuntimeConfigurationLoadResult& result,
    std::string_view section_name,
    const SectionValues& values) {
    constexpr std::string_view k_prefix = "input_context.";
    const std::string context_id = std::string(section_name.substr(k_prefix.size()));
    if (context_id.empty()) {
        add_issue(result, true, 0, "Input-context section name is missing a context id.");
        return;
    }

    bool active = result.configuration.input_action_maps.is_context_active(context_id);
    if (const auto it = values.find("active"); it != values.end() && !try_parse_bool(it->second.value, active)) {
        add_issue(result, true, it->second.line, "Invalid active flag for [" + std::string(section_name) + "].");
    }
    result.configuration.input_action_maps.set_context_active(context_id, active);

    warn_unknown_keys(result, section_name, values, {"active"});
}

void load_input_device_profile_section(
    RuntimeConfigurationLoadResult& result,
    std::string_view section_name,
    const SectionValues& values) {
    constexpr std::string_view k_prefix = "input_device_profile.";
    const std::string profile_id = std::string(section_name.substr(k_prefix.size()));
    if (profile_id.empty()) {
        add_issue(result, true, 0, "Input-device-profile section name is missing a profile id.");
        return;
    }

    std::string display_name = profile_id;
    if (const auto it = values.find("display_name"); it != values.end()) {
        display_name = it->second.value;
    }
    result.configuration.input_action_maps.add_device_profile(profile_id, display_name);

    warn_unknown_keys(result, section_name, values, {"display_name"});
}

void load_mode_configuration_section(
    RuntimeConfigurationLoadResult& result,
    std::string_view section_name,
    const SectionValues& values,
    gameplay::ModeConfigurationStore& mode_configuration) {
    constexpr std::string_view k_prefix = "mode.";
    const std::string mode_id = std::string(section_name.substr(k_prefix.size()));
    if (mode_id.empty()) {
        add_issue(result, true, 0, "Mode-configuration section name is missing a mode id.");
        return;
    }

    for (const auto& [key, parsed] : values) {
        mode_configuration.set(mode_id, key, parsed.value);
    }
}

void load_modifier_section(
    RuntimeConfigurationLoadResult& result,
    std::string_view section_name,
    const SectionValues& values,
    gameplay::ModifierStore& modifiers) {
    constexpr std::string_view k_prefix = "modifier.";
    const std::string mode_id = std::string(section_name.substr(k_prefix.size()));
    if (mode_id.empty()) {
        add_issue(result, true, 0, "Modifier section name is missing a mode id.");
        return;
    }

    for (const auto& [key, parsed] : values) {
        gameplay::ModifierEntry entry{};
        entry.id = key;
        entry.kind = gameplay::classify_modifier_id(key);

        switch (entry.kind) {
        case gameplay::ModifierKind::SpeedMultiplier: {
            double numeric = 1.0;
            if (!try_parse_double(parsed.value, numeric)) {
                add_issue(result, true, parsed.line,
                    "Modifier '" + std::string(key) + "' in section [" + std::string(section_name) +
                    "] expects a numeric multiplier.");
                continue;
            }
            entry.numeric_parameter = numeric;
            entry.enabled = std::isfinite(numeric) && std::abs(numeric - 1.0) > 1e-6;
            break;
        }
        case gameplay::ModifierKind::MirrorChannels: {
            std::uint64_t lane_count = 0;
            if (!try_parse_unsigned<std::uint64_t>(parsed.value, lane_count)) {
                add_issue(result, true, parsed.line,
                    "Modifier '" + std::string(key) + "' in section [" + std::string(section_name) +
                    "] expects an unsigned lane count.");
                continue;
            }
            entry.integer_parameter = static_cast<std::int64_t>(lane_count);
            entry.enabled = lane_count > 1;
            break;
        }
        case gameplay::ModifierKind::Autoplay:
        case gameplay::ModifierKind::NoFail:
        case gameplay::ModifierKind::PracticeAssist: {
            bool boolean = false;
            if (!try_parse_bool(parsed.value, boolean)) {
                add_issue(result, true, parsed.line,
                    "Modifier '" + std::string(key) + "' in section [" + std::string(section_name) +
                    "] expects a boolean value.");
                continue;
            }
            entry.boolean_parameter = boolean;
            entry.enabled = boolean;
            break;
        }
        case gameplay::ModifierKind::Custom: {
            double numeric = 0.0;
            bool boolean = false;
            if (try_parse_double(parsed.value, numeric)) {
                entry.numeric_parameter = numeric;
                entry.enabled = std::isfinite(numeric) && numeric != 0.0;
            } else if (try_parse_bool(parsed.value, boolean)) {
                entry.boolean_parameter = boolean;
                entry.enabled = boolean;
            } else {
                add_issue(result, false, parsed.line,
                    "Custom modifier '" + std::string(key) + "' in section [" + std::string(section_name) +
                    "] could not be parsed as numeric or boolean; storing as disabled string entry.");
                entry.enabled = false;
            }
            break;
        }
        }

        modifiers.set(mode_id, std::move(entry));
    }
}

} // namespace

bool RuntimeConfigurationLoadResult::success() const noexcept {
    return std::none_of(issues.begin(), issues.end(), [](const RuntimeConfigurationIssue& issue) {
        return issue.fatal;
    });
}

RuntimeConfiguration make_default_runtime_configuration() {
    RuntimeConfiguration configuration{};
    configuration.runtime_budget = foundation::make_bootstrap_budget();
    configuration.application_config = platform::make_smoke_application_config();
    configuration.hot_reload.enabled = true;
    configuration.hot_reload.poll_interval_seconds = 0.25;
    configuration.hot_reload.watch_charts = true;
    configuration.hot_reload.watch_shaders = true;
    configuration.hot_reload.watch_materials = true;
    configuration.hot_reload.watch_selected_content = true;
    configuration.startup_mode_id = "mode.reference.sandbox";
    configuration.random_seed = 0x5245414b54494f32ull;

    configuration.input_action_maps.add_device_profile("keyboard_mouse", "Keyboard and Mouse");
    configuration.input_action_maps.add_device_profile("gamepad", "Gamepad");
    configuration.input_action_maps.set_active_device_profile("keyboard_mouse");
    configuration.input_action_maps.set_context_active("system", true);
    configuration.input_action_maps.set_context_active("gameplay", true);

    set_runtime_action_binding(configuration, "system", "toggle_fullscreen", "keyboard:F11");
    set_runtime_action_binding(configuration, "system", "request_quit", "keyboard:Escape");
    set_runtime_action_binding(configuration, "gameplay", "transport_pause", "keyboard:Space");
    set_runtime_action_binding(configuration, "gameplay", "transport_restart", "keyboard:R");
    set_runtime_action_binding(configuration, "gameplay", "calibration_output_mode", "keyboard:O");
    set_runtime_action_binding(configuration, "gameplay", "calibration_input_mode", "keyboard:I");
    set_runtime_action_binding(configuration, "gameplay", "calibration_commit", "keyboard:Return");
    set_runtime_action_binding(configuration, "gameplay", "calibration_clear", "keyboard:Backspace");
    set_runtime_action_binding(configuration, "gameplay", "calibration_adjust_negative", "keyboard:Left");
    set_runtime_action_binding(configuration, "gameplay", "calibration_adjust_positive", "keyboard:Right");
    set_runtime_action_binding(configuration, "gameplay", "practice_speed_decrease", "keyboard:Z");
    set_runtime_action_binding(configuration, "gameplay", "practice_speed_increase", "keyboard:X");
    set_runtime_action_binding(configuration, "gameplay", "practice_speed_reset", "keyboard:C");
    set_runtime_action_binding(configuration, "gameplay", "practice_loop_mark_start", "keyboard:J");
    set_runtime_action_binding(configuration, "gameplay", "practice_loop_mark_end", "keyboard:K");
    set_runtime_action_binding(configuration, "gameplay", "practice_loop_apply", "keyboard:L");
    set_runtime_action_binding(configuration, "gameplay", "practice_loop_clear", "keyboard:U");
    set_runtime_action_binding(configuration, "gameplay", "practice_offset_visualization_toggle", "keyboard:V");

    configuration.mode_configuration.set("mode.reference.sandbox", "velocity_scale", "1.0");
    configuration.mode_configuration.set("mode.reference.sandbox", "hit_window_half_width", "32.0");
    configuration.mode_configuration.set("mode.reference.sandbox", "hit_window_half_height", "24.0");
    configuration.mode_configuration.set("mode.reference.sandbox", "practice_scroll_speed_multiplier", "1.25");
    configuration.mode_configuration.set("mode.reference.sandbox", "practice_offset_visualization_enabled", "true");
    configuration.mode_configuration.set("mode.reference.sandbox", "practice_loop_start_seconds", "0.75");
    configuration.mode_configuration.set("mode.reference.sandbox", "practice_loop_end_seconds", "1.25");
    configuration.mode_configuration.set(
        "mode.reference.sandbox",
        "cue_material_authoring_id",
        "reference.sandbox.material.cue");
    configuration.mode_configuration.set(
        "mode.reference.sandbox",
        "debug_font_authoring_id",
        "reference.sandbox.font.debug");

    {
        gameplay::ModifierEntry speed_entry{};
        speed_entry.id = std::string(gameplay::modifier_ids::k_speed_multiplier);
        speed_entry.kind = gameplay::ModifierKind::SpeedMultiplier;
        speed_entry.numeric_parameter = 1.0;
        speed_entry.enabled = false;
        configuration.modifiers.set("mode.reference.sandbox", speed_entry);

        gameplay::ModifierEntry practice_entry{};
        practice_entry.id = std::string(gameplay::modifier_ids::k_practice_assist);
        practice_entry.kind = gameplay::ModifierKind::PracticeAssist;
        practice_entry.boolean_parameter = true;
        practice_entry.enabled = true;
        configuration.modifiers.set("mode.reference.sandbox", practice_entry);
    }

    return configuration;
}

RuntimeConfigurationLoadResult load_runtime_configuration() {
    RuntimeConfigurationLoadResult result{};
    result.configuration = make_default_runtime_configuration();

    std::optional<std::filesystem::path> config_path = configured_config_path_from_env();
    if (config_path && !std::filesystem::exists(*config_path)) {
        result.source_path = *config_path;
        add_issue(result, false, 0, "Configured runtime configuration path does not exist; falling back to default search.");
        config_path.reset();
    }

    if (!config_path) {
        config_path = find_default_config_path();
    }

    if (!config_path) {
        add_issue(result, false, 0, "No runtime configuration file was found; using built-in defaults.");
        return result;
    }

    result.source_path = *config_path;
    const std::optional<std::string> text = read_text_file(*config_path);
    if (!text) {
        add_issue(result, true, 0, "Unable to read runtime configuration file.");
        return result;
    }

    result.loaded_from_file = true;
    const SectionMap sections = parse_sections(result, *text);
    std::unordered_set<std::string> processed_sections;

    auto process_known_section = [&](std::string_view section_name, auto&& processor) {
        const auto it = sections.find(std::string(section_name));
        if (it == sections.end()) {
            return;
        }

        processed_sections.insert(it->first);
        processor(it->second);
    };

    process_known_section("app", [&](const SectionValues& values) {
        load_string_value(sections, "app", "startup_mode_id", result.configuration.startup_mode_id);
        load_typed_value(
            result,
            sections,
            "app",
            "random_seed",
            "random seed",
            result.configuration.random_seed,
            [](std::string_view value, std::uint64_t& parsed) { return try_parse_unsigned(value, parsed); });
        load_string_value(sections, "app", "app_identifier", result.configuration.application_config.app_identifier);
        load_string_value(sections, "app", "log_file_name", result.configuration.application_config.log_file_name);
        warn_unknown_keys(result, "app", values, {"startup_mode_id", "random_seed", "app_identifier", "log_file_name"});
    });

    process_known_section("window", [&](const SectionValues& values) {
        load_string_value(sections, "window", "title", result.configuration.application_config.window.title);
        load_typed_value(
            result,
            sections,
            "window",
            "width",
            "window width",
            result.configuration.application_config.window.width,
            [](std::string_view value, int& parsed) {
                std::uint32_t converted = 0;
                if (!try_parse_unsigned(value, converted) || converted > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
                    return false;
                }
                parsed = static_cast<int>(converted);
                return true;
            });
        load_typed_value(
            result,
            sections,
            "window",
            "height",
            "window height",
            result.configuration.application_config.window.height,
            [](std::string_view value, int& parsed) {
                std::uint32_t converted = 0;
                if (!try_parse_unsigned(value, converted) || converted > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
                    return false;
                }
                parsed = static_cast<int>(converted);
                return true;
            });
        load_typed_value(
            result,
            sections,
            "window",
            "mode",
            "window mode",
            result.configuration.application_config.window.mode,
            try_parse_window_mode);
        load_typed_value(result, sections, "window", "resizable", "window resizable flag", result.configuration.application_config.window.resizable, try_parse_bool);
        load_typed_value(result, sections, "window", "high_pixel_density", "window high-pixel-density flag", result.configuration.application_config.window.high_pixel_density, try_parse_bool);
        load_typed_value(result, sections, "window", "start_hidden", "window start-hidden flag", result.configuration.application_config.window.start_hidden, try_parse_bool);
        load_typed_value(result, sections, "window", "enable_text_input", "window text-input flag", result.configuration.application_config.window.enable_text_input, try_parse_bool);
        warn_unknown_keys(
            result,
            "window",
            values,
            {"title", "width", "height", "mode", "resizable", "high_pixel_density", "start_hidden", "enable_text_input"});
    });

    process_known_section("main_loop", [&](const SectionValues& values) {
        load_typed_value(result, sections, "main_loop", "fixed_step_seconds", "fixed-step seconds", result.configuration.application_config.main_loop.fixed_step_seconds, try_parse_double);
        load_typed_value(result, sections, "main_loop", "max_frame_delta_seconds", "maximum frame delta seconds", result.configuration.application_config.main_loop.max_frame_delta_seconds, try_parse_double);
        load_typed_value(result, sections, "main_loop", "max_fixed_steps_per_frame", "maximum fixed steps per frame", result.configuration.application_config.main_loop.max_fixed_steps_per_frame, [](std::string_view value, std::uint32_t& parsed) {
            return try_parse_unsigned(value, parsed);
        });
        load_typed_value(result, sections, "main_loop", "max_frame_count", "maximum frame count", result.configuration.application_config.main_loop.max_frame_count, [](std::string_view value, std::uint64_t& parsed) {
            return try_parse_unsigned(value, parsed);
        });
        warn_unknown_keys(
            result,
            "main_loop",
            values,
            {"fixed_step_seconds", "max_frame_delta_seconds", "max_fixed_steps_per_frame", "max_frame_count"});
    });

    process_known_section("audio", [&](const SectionValues& values) {
        load_typed_value(result, sections, "audio", "enable_playback_device", "audio playback enabled flag", result.configuration.application_config.audio.enable_playback_device, try_parse_bool);
        load_typed_value(result, sections, "audio", "fail_if_unavailable", "audio device required flag", result.configuration.application_config.audio.fail_if_unavailable, try_parse_bool);
        load_typed_value(result, sections, "audio", "preferred_sample_rate", "audio sample rate", result.configuration.application_config.audio.preferred_sample_rate, [](std::string_view value, int& parsed) {
            std::uint32_t converted = 0;
            if (!try_parse_unsigned(value, converted) || converted > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
                return false;
            }
            parsed = static_cast<int>(converted);
            return parsed > 0;
        });
        load_typed_value(result, sections, "audio", "preferred_channels", "audio channel count", result.configuration.application_config.audio.preferred_channels, [](std::string_view value, int& parsed) {
            std::uint32_t converted = 0;
            if (!try_parse_unsigned(value, converted) || converted > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
                return false;
            }
            parsed = static_cast<int>(converted);
            return parsed > 0;
        });
        load_typed_value(result, sections, "audio", "preferred_buffer_frames", "audio buffer frames", result.configuration.application_config.audio.preferred_buffer_frames, [](std::string_view value, int& parsed) {
            std::uint32_t converted = 0;
            if (!try_parse_unsigned(value, converted) || converted > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
                return false;
            }
            parsed = static_cast<int>(converted);
            return parsed > 0;
        });
        load_typed_value(result, sections, "audio", "preferred_format", "audio sample format", result.configuration.application_config.audio.preferred_format, try_parse_audio_sample_format);
        load_typed_value(result, sections, "audio", "start_paused", "audio start-paused flag", result.configuration.application_config.audio.start_paused, try_parse_bool);
        load_typed_value(result, sections, "audio", "device_gain", "audio device gain", result.configuration.application_config.audio.device_gain, [](std::string_view value, float& parsed) {
            double converted = static_cast<double>(parsed);
            if (!try_parse_double(value, converted) || converted < 0.0) {
                return false;
            }
            parsed = static_cast<float>(converted);
            return true;
        });
        warn_unknown_keys(
            result,
            "audio",
            values,
            {"enable_playback_device", "fail_if_unavailable", "preferred_sample_rate", "preferred_channels", "preferred_buffer_frames", "preferred_format", "start_paused", "device_gain"});
    });

    process_known_section("renderer", [&](const SectionValues& values) {
        load_typed_value(result, sections, "renderer", "backend", "renderer backend", result.configuration.application_config.renderer_backend, try_parse_renderer_backend);
        load_typed_value(result, sections, "renderer", "vsync_enabled", "vsync flag", result.configuration.application_config.vsync_enabled, try_parse_bool);
        warn_unknown_keys(result, "renderer", values, {"backend", "vsync_enabled"});
    });

    process_known_section("debug", [&](const SectionValues& values) {
        load_typed_value(result, sections, "debug", "enable_startup_diagnostics", "startup diagnostics flag", result.configuration.application_config.debug.enable_startup_diagnostics, try_parse_bool);
        load_typed_value(result, sections, "debug", "enable_debug_overlay", "debug overlay flag", result.configuration.application_config.debug.enable_debug_overlay, try_parse_bool);
        load_typed_value(result, sections, "debug", "enable_input_diagnostics", "input diagnostics flag", result.configuration.application_config.debug.enable_input_diagnostics, try_parse_bool);
        load_typed_value(result, sections, "debug", "enable_gpu_debug", "gpu debug flag", result.configuration.application_config.debug.enable_gpu_debug, try_parse_bool);
        warn_unknown_keys(
            result,
            "debug",
            values,
            {"enable_startup_diagnostics", "enable_debug_overlay", "enable_input_diagnostics", "enable_gpu_debug"});
    });

    process_known_section("input", [&](const SectionValues& values) {
        load_input_section(result, values);
    });

    process_known_section("post_process", [&](const SectionValues& values) {
        load_typed_value(result, sections, "post_process", "enabled", "post-process enabled flag", result.configuration.application_config.post_process.enabled, try_parse_bool);
        load_typed_value(result, sections, "post_process", "bloom_threshold", "bloom threshold", result.configuration.application_config.post_process.bloom_threshold, [](std::string_view value, float& parsed) {
            double converted = static_cast<double>(parsed);
            if (!try_parse_double(value, converted)) {
                return false;
            }

            parsed = static_cast<float>(converted);
            return parsed >= 0.0f && parsed <= 1.0f;
        });
        load_typed_value(result, sections, "post_process", "bloom_intensity", "bloom intensity", result.configuration.application_config.post_process.bloom_intensity, [](std::string_view value, float& parsed) {
            double converted = static_cast<double>(parsed);
            if (!try_parse_double(value, converted) || converted < 0.0) {
                return false;
            }

            parsed = static_cast<float>(converted);
            return true;
        });
        load_typed_value(result, sections, "post_process", "bloom_blur_scale", "bloom blur scale", result.configuration.application_config.post_process.bloom_blur_scale, [](std::string_view value, float& parsed) {
            double converted = static_cast<double>(parsed);
            if (!try_parse_double(value, converted) || converted <= 0.0) {
                return false;
            }

            parsed = static_cast<float>(converted);
            return true;
        });
        load_typed_value(result, sections, "post_process", "exposure", "post-process exposure", result.configuration.application_config.post_process.exposure, [](std::string_view value, float& parsed) {
            double converted = static_cast<double>(parsed);
            if (!try_parse_double(value, converted) || converted < 0.0) {
                return false;
            }

            parsed = static_cast<float>(converted);
            return true;
        });
        load_typed_value(result, sections, "post_process", "saturation", "post-process saturation", result.configuration.application_config.post_process.saturation, [](std::string_view value, float& parsed) {
            double converted = static_cast<double>(parsed);
            if (!try_parse_double(value, converted) || converted < 0.0) {
                return false;
            }

            parsed = static_cast<float>(converted);
            return true;
        });
        load_typed_value(result, sections, "post_process", "contrast", "post-process contrast", result.configuration.application_config.post_process.contrast, [](std::string_view value, float& parsed) {
            double converted = static_cast<double>(parsed);
            if (!try_parse_double(value, converted) || converted < 0.0) {
                return false;
            }

            parsed = static_cast<float>(converted);
            return true;
        });
        load_typed_value(result, sections, "post_process", "vignette_intensity", "vignette intensity", result.configuration.application_config.post_process.vignette_intensity, [](std::string_view value, float& parsed) {
            double converted = static_cast<double>(parsed);
            if (!try_parse_double(value, converted) || converted < 0.0 || converted > 1.0) {
                return false;
            }

            parsed = static_cast<float>(converted);
            return true;
        });
        load_typed_value(result, sections, "post_process", "feedback_mix", "feedback mix", result.configuration.application_config.post_process.feedback_mix, [](std::string_view value, float& parsed) {
            double converted = static_cast<double>(parsed);
            if (!try_parse_double(value, converted) || converted < 0.0 || converted > 1.0) {
                return false;
            }

            parsed = static_cast<float>(converted);
            return true;
        });
        load_typed_value(result, sections, "post_process", "feedback_decay", "feedback decay", result.configuration.application_config.post_process.feedback_decay, [](std::string_view value, float& parsed) {
            double converted = static_cast<double>(parsed);
            if (!try_parse_double(value, converted) || converted < 0.0 || converted > 1.0) {
                return false;
            }

            parsed = static_cast<float>(converted);
            return true;
        });
        load_typed_value(result, sections, "post_process", "feedback_scale", "feedback scale", result.configuration.application_config.post_process.feedback_scale, [](std::string_view value, float& parsed) {
            double converted = static_cast<double>(parsed);
            if (!try_parse_double(value, converted) || converted <= 0.0) {
                return false;
            }

            parsed = static_cast<float>(converted);
            return true;
        });
        load_typed_value(result, sections, "post_process", "color_grade_r", "red color-grade multiplier", result.configuration.application_config.post_process.color_grade_r, [](std::string_view value, float& parsed) {
            double converted = static_cast<double>(parsed);
            if (!try_parse_double(value, converted) || converted < 0.0) {
                return false;
            }

            parsed = static_cast<float>(converted);
            return true;
        });
        load_typed_value(result, sections, "post_process", "color_grade_g", "green color-grade multiplier", result.configuration.application_config.post_process.color_grade_g, [](std::string_view value, float& parsed) {
            double converted = static_cast<double>(parsed);
            if (!try_parse_double(value, converted) || converted < 0.0) {
                return false;
            }

            parsed = static_cast<float>(converted);
            return true;
        });
        load_typed_value(result, sections, "post_process", "color_grade_b", "blue color-grade multiplier", result.configuration.application_config.post_process.color_grade_b, [](std::string_view value, float& parsed) {
            double converted = static_cast<double>(parsed);
            if (!try_parse_double(value, converted) || converted < 0.0) {
                return false;
            }

            parsed = static_cast<float>(converted);
            return true;
        });
        warn_unknown_keys(
            result,
            "post_process",
            values,
            {"enabled",
                "bloom_threshold",
                "bloom_intensity",
                "bloom_blur_scale",
                "exposure",
                "saturation",
                "contrast",
                "vignette_intensity",
                "feedback_mix",
                "feedback_decay",
                "feedback_scale",
                "color_grade_r",
                "color_grade_g",
                "color_grade_b"});
    });

    process_known_section("hot_reload", [&](const SectionValues& values) {
        load_typed_value(result, sections, "hot_reload", "enabled", "hot-reload enabled flag", result.configuration.hot_reload.enabled, try_parse_bool);
        load_typed_value(result, sections, "hot_reload", "poll_interval_seconds", "hot-reload poll interval", result.configuration.hot_reload.poll_interval_seconds, [](std::string_view value, double& parsed) {
            return try_parse_double(value, parsed) && parsed > 0.0;
        });
        load_typed_value(result, sections, "hot_reload", "watch_charts", "chart hot-reload flag", result.configuration.hot_reload.watch_charts, try_parse_bool);
        load_typed_value(result, sections, "hot_reload", "watch_shaders", "shader hot-reload flag", result.configuration.hot_reload.watch_shaders, try_parse_bool);
        load_typed_value(result, sections, "hot_reload", "watch_materials", "material hot-reload flag", result.configuration.hot_reload.watch_materials, try_parse_bool);
        load_typed_value(result, sections, "hot_reload", "watch_selected_content", "selected-content hot-reload flag", result.configuration.hot_reload.watch_selected_content, try_parse_bool);
        const std::filesystem::path config_directory = result.source_path.empty()
            ? std::filesystem::current_path()
            : result.source_path.parent_path();
        load_path_value(config_directory, sections, "hot_reload", "chart_manifest_path", result.configuration.hot_reload.chart_manifest_path);
        load_path_value(config_directory, sections, "hot_reload", "shader_manifest_path", result.configuration.hot_reload.shader_manifest_path);
        load_path_value(config_directory, sections, "hot_reload", "material_manifest_path", result.configuration.hot_reload.material_manifest_path);
        load_path_value(config_directory, sections, "hot_reload", "selected_content_manifest_path", result.configuration.hot_reload.selected_content_manifest_path);
        warn_unknown_keys(
            result,
            "hot_reload",
            values,
            {"enabled", "poll_interval_seconds", "watch_charts", "watch_shaders", "watch_materials", "watch_selected_content", "chart_manifest_path", "shader_manifest_path", "material_manifest_path", "selected_content_manifest_path"});
    });

    process_known_section("budget", [&](const SectionValues& values) {
        load_typed_value(result, sections, "budget", "target_frame_ms", "target frame budget", result.configuration.runtime_budget.target_frame_ms, try_parse_double);
        load_typed_value(result, sections, "budget", "simulation_budget_ms", "simulation budget", result.configuration.runtime_budget.simulation_budget_ms, try_parse_double);
        load_typed_value(result, sections, "budget", "render_submission_budget_ms", "render submission budget", result.configuration.runtime_budget.render_submission_budget_ms, try_parse_double);
        load_typed_value(result, sections, "budget", "audio_callback_budget_ms", "audio callback budget", result.configuration.runtime_budget.audio_callback_budget_ms, try_parse_double);
        load_typed_value(result, sections, "budget", "resident_memory_budget_mib", "resident memory budget", result.configuration.runtime_budget.resident_memory_budget_mib, [](std::string_view value, std::size_t& parsed) {
            return try_parse_unsigned(value, parsed);
        });
        load_typed_value(result, sections, "budget", "draw_call_budget", "draw-call budget", result.configuration.runtime_budget.draw_call_budget, [](std::string_view value, std::uint32_t& parsed) {
            return try_parse_unsigned(value, parsed);
        });
        load_typed_value(result, sections, "budget", "visible_cue_budget", "visible-cue budget", result.configuration.runtime_budget.visible_cue_budget, [](std::string_view value, std::uint32_t& parsed) {
            return try_parse_unsigned(value, parsed);
        });
        warn_unknown_keys(
            result,
            "budget",
            values,
            {"target_frame_ms", "simulation_budget_ms", "render_submission_budget_ms", "audio_callback_budget_ms", "resident_memory_budget_mib", "draw_call_budget", "visible_cue_budget"});
    });

    for (const auto& [section_name, values] : sections) {
        if (processed_sections.contains(section_name)) {
            continue;
        }

        if (section_name.rfind("input_binding.", 0) == 0) {
            processed_sections.insert(section_name);
            load_input_binding_section(result, section_name, values);
            continue;
        }

        if (section_name.rfind("input_context.", 0) == 0) {
            processed_sections.insert(section_name);
            load_input_context_section(result, section_name, values);
            continue;
        }

        if (section_name.rfind("input_device_profile.", 0) == 0) {
            processed_sections.insert(section_name);
            load_input_device_profile_section(result, section_name, values);
            continue;
        }

        if (section_name.rfind("mode.", 0) == 0) {
            processed_sections.insert(section_name);
            load_mode_configuration_section(result, section_name, values, result.configuration.mode_configuration);
            continue;
        }

        if (section_name.rfind("modifier.", 0) == 0) {
            processed_sections.insert(section_name);
            load_modifier_section(result, section_name, values, result.configuration.modifiers);
            continue;
        }
    }

    for (const auto& [section_name, values] : sections) {
        if (!processed_sections.contains(section_name)) {
            add_issue(
                result,
                false,
                values.empty() ? 0u : values.begin()->second.line,
                "Unknown configuration section [" + section_name + "].");
        }
    }

    return result;
}

} // namespace reaktio::app