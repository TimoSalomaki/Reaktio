#include "reaktio/audio/AudioClipLibrary.hpp"

#include "reaktio/foundation/CrashSafeLog.hpp"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>

namespace reaktio::audio {

namespace {

struct ParsedKeyValue {
    std::string value;
    std::size_t line{};
};

using SectionValues = std::unordered_map<std::string, ParsedKeyValue>;
using SectionMap = std::unordered_map<std::string, SectionValues>;

constexpr std::string_view k_default_manifest_relative_path = "content/raw/audio/manifest.ini";

struct SdlBuffer {
    Uint8* bytes{};

    ~SdlBuffer() {
        if (bytes != nullptr) {
            SDL_free(bytes);
        }
    }
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
            try_get_environment_value("REAKTIO_AUDIO_CLIP_MANIFEST_PATH");
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

std::optional<AudioClipSourceFormat> detect_audio_source_format(const std::filesystem::path& path) {
    const std::string extension = lowercase_copy(path.extension().string());
    if (extension == ".wav" || extension == ".wave") {
        return AudioClipSourceFormat::Wave;
    }

    return std::nullopt;
}

std::optional<AudioClipRecord> decode_wave_clip(
    foundation::CrashSafeLog& log,
    foundation::ResourceRegistry& resource_registry,
    std::string_view authoring_id,
    std::string_view runtime_label,
    const std::filesystem::path& source_path) {
    SDL_AudioSpec source_spec{};
    SdlBuffer source_buffer{};
    Uint32 source_length = 0;
    const std::string source_path_string = source_path.string();
    if (!SDL_LoadWAV(source_path_string.c_str(), &source_spec, &source_buffer.bytes, &source_length)) {
        log_message(log, foundation::LogLevel::Error, source_path, 0, "Unable to decode WAV audio clip.");
        return std::nullopt;
    }

    if (source_spec.freq <= 0 || source_spec.channels <= 0) {
        log_message(log, foundation::LogLevel::Error, source_path, 0, "Decoded WAV clip reported an invalid audio spec.");
        return std::nullopt;
    }

    const SDL_AudioSpec target_spec{
        .format = SDL_AUDIO_F32,
        .channels = source_spec.channels,
        .freq = source_spec.freq,
    };
    SdlBuffer converted_buffer{};
    int converted_length = 0;
    if (!SDL_ConvertAudioSamples(
            &source_spec,
            source_buffer.bytes,
            static_cast<int>(source_length),
            &target_spec,
            &converted_buffer.bytes,
            &converted_length)) {
        log_message(log, foundation::LogLevel::Error, source_path, 0, "Unable to convert decoded WAV clip to canonical float32 samples.");
        return std::nullopt;
    }

    const int bytes_per_frame = static_cast<int>(SDL_AUDIO_FRAMESIZE(target_spec));
    if (bytes_per_frame <= 0 || converted_length <= 0 || (converted_length % bytes_per_frame) != 0) {
        log_message(log, foundation::LogLevel::Error, source_path, 0, "Canonical decoded WAV clip produced an invalid frame layout.");
        return std::nullopt;
    }

    const std::size_t sample_value_count = static_cast<std::size_t>(converted_length) / sizeof(float);
    std::vector<float> interleaved_samples(sample_value_count);
    std::memcpy(interleaved_samples.data(), converted_buffer.bytes, static_cast<std::size_t>(converted_length));

    const std::uint64_t frame_count = static_cast<std::uint64_t>(converted_length / bytes_per_frame);
    const foundation::ResourceHandle resource = resource_registry.register_resource(
        foundation::ResourceKind::AudioClip,
        authoring_id,
        runtime_label);
    if (!resource.valid()) {
        log_message(log, foundation::LogLevel::Error, source_path, 0, "Failed to register decoded audio clip resource.");
        return std::nullopt;
    }

    return AudioClipRecord{
        .resource = resource,
        .authoring_id = std::string(authoring_id),
        .runtime_label = std::string(runtime_label),
        .source_path = source_path,
        .source_format = AudioClipSourceFormat::Wave,
        .sample_rate_hz = static_cast<std::uint32_t>(target_spec.freq),
        .channel_count = static_cast<std::uint16_t>(target_spec.channels),
        .frame_count = frame_count,
        .duration_seconds = static_cast<double>(frame_count) / static_cast<double>(target_spec.freq),
        .interleaved_samples = std::move(interleaved_samples),
    };
}

} // namespace

bool AudioClipLibrary::load(
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
            "Configured authoring audio manifest does not exist; falling back to default search.");
        manifest_path.reset();
    }

    if (!manifest_path) {
        manifest_path = find_default_manifest_path();
    }

    if (!manifest_path) {
        log.write(foundation::LogLevel::Warning, "No authoring audio manifest was found; continuing without decoded audio clips.");
        return true;
    }

    summary_.manifest_path = *manifest_path;
    summary_.loaded_from_manifest = true;

    const std::optional<std::string> manifest_text = read_text_file(*manifest_path);
    if (!manifest_text) {
        log_message(log, foundation::LogLevel::Error, *manifest_path, 0, "Unable to read authoring audio manifest.");
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
        if (section_name.rfind("clip.", 0) != 0) {
            log_message(log, foundation::LogLevel::Warning, *manifest_path, 0, "Unknown audio manifest section was ignored.");
            continue;
        }

        const auto runtime_label_it = values.find("runtime_label");
        const auto source_it = values.find("source");
        if (runtime_label_it == values.end() || source_it == values.end()) {
            log_message(log, foundation::LogLevel::Error, *manifest_path, 0, "Audio clip section is missing runtime_label or source.");
            clear();
            return false;
        }

        const std::string authoring_id = section_name.substr(std::string_view("clip.").size());
        const std::filesystem::path source_path = std::filesystem::absolute(manifest_directory / source_it->second.value);
        if (!std::filesystem::exists(source_path)) {
            log_message(log, foundation::LogLevel::Error, source_path, source_it->second.line, "Audio clip source path does not exist.");
            clear();
            return false;
        }

        const std::optional<AudioClipSourceFormat> source_format = detect_audio_source_format(source_path);
        if (!source_format) {
            log_message(log, foundation::LogLevel::Error, source_path, source_it->second.line, "Unsupported authoring audio source format.");
            clear();
            return false;
        }

        std::optional<AudioClipRecord> record;
        switch (*source_format) {
        case AudioClipSourceFormat::Wave:
            record = decode_wave_clip(
                log,
                resource_registry,
                authoring_id,
                runtime_label_it->second.value,
                source_path);
            break;
        }

        if (!record) {
            clear();
            return false;
        }

        summary_.clip_count += 1;
        summary_.total_frames += record->frame_count;
        summary_.total_sample_values += record->interleaved_samples.size();
        summary_.total_sample_bytes += record->interleaved_samples.size() * sizeof(float);
        summary_.total_duration_seconds += record->duration_seconds;
        clip_load_order_.push_back(record->resource.value());
        clips_.emplace(record->resource.value(), std::move(*record));
    }

    return true;
}

void AudioClipLibrary::clear() noexcept {
    clips_.clear();
    clip_load_order_.clear();
    summary_ = {};
}

const AudioClipRecord* AudioClipLibrary::try_get_clip(foundation::ResourceHandle resource) const noexcept {
    const auto it = clips_.find(resource.value());
    return it != clips_.end() ? &it->second : nullptr;
}

const AudioClipRecord* AudioClipLibrary::first_clip() const noexcept {
    if (clip_load_order_.empty()) {
        return nullptr;
    }

    const auto it = clips_.find(clip_load_order_.front());
    return it != clips_.end() ? &it->second : nullptr;
}

const AudioClipLibrarySummary& AudioClipLibrary::summary() const noexcept {
    return summary_;
}

} // namespace reaktio::audio