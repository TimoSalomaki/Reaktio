#include "reaktio/app/RuntimeConfiguration.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
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

void load_input_binding_section(
    RuntimeConfigurationLoadResult& result,
    std::string_view section_name,
    const SectionValues& values,
    platform::InputBindingsConfig& input_bindings) {
    constexpr std::string_view k_prefix = "input_binding.";
    const std::string action_id = std::string(section_name.substr(k_prefix.size()));
    if (action_id.empty()) {
        add_issue(result, true, 0, "Input-binding section name is missing an action id.");
        return;
    }

    std::string primary;
    std::string secondary;
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

    warn_unknown_keys(result, section_name, values, {"primary", "secondary"});
    input_bindings.set_action_binding(action_id, primary, secondary);
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
    configuration.startup_mode_id = "mode.reference.sandbox";
    configuration.random_seed = 0x5245414b54494f32ull;

    configuration.input_bindings.set_action_binding("toggle_fullscreen", "keyboard:F11");
    configuration.input_bindings.set_action_binding("request_quit", "keyboard:Escape");
    configuration.input_bindings.set_action_binding("transport_pause", "keyboard:Space");
    configuration.input_bindings.set_action_binding("transport_restart", "keyboard:R");

    configuration.mode_configuration.set("mode.reference.sandbox", "velocity_scale", "1.0");
    configuration.mode_configuration.set("mode.reference.sandbox", "hit_window_half_width", "32.0");
    configuration.mode_configuration.set("mode.reference.sandbox", "hit_window_half_height", "24.0");
    configuration.mode_configuration.set(
        "mode.reference.sandbox",
        "cue_material_authoring_id",
        "reference.sandbox.material.cue");
    configuration.mode_configuration.set(
        "mode.reference.sandbox",
        "debug_font_authoring_id",
        "reference.sandbox.font.debug");

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
            load_input_binding_section(result, section_name, values, result.configuration.input_bindings);
            continue;
        }

        if (section_name.rfind("mode.", 0) == 0) {
            processed_sections.insert(section_name);
            load_mode_configuration_section(result, section_name, values, result.configuration.mode_configuration);
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