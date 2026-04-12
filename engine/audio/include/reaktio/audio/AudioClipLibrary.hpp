#pragma once

#include "reaktio/foundation/ResourceRegistry.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reaktio::foundation {
class CrashSafeLog;
}

namespace reaktio::audio {

enum class AudioClipSourceFormat : std::uint8_t {
    Wave,
};

[[nodiscard]] constexpr std::string_view to_string(AudioClipSourceFormat format) noexcept {
    switch (format) {
    case AudioClipSourceFormat::Wave:
        return "wave";
    }

    return "unknown";
}

struct AudioClipRecord {
    foundation::ResourceHandle resource{};
    std::string authoring_id;
    std::string runtime_label;
    std::filesystem::path source_path;
    AudioClipSourceFormat source_format{AudioClipSourceFormat::Wave};
    std::uint32_t sample_rate_hz{};
    std::uint16_t channel_count{};
    std::uint64_t frame_count{};
    double duration_seconds{};
    std::vector<float> interleaved_samples;
};

struct AudioClipLibrarySummary {
    bool loaded_from_manifest{};
    std::filesystem::path manifest_path;
    std::size_t clip_count{};
    std::uint64_t total_frames{};
    std::size_t total_sample_values{};
    std::size_t total_sample_bytes{};
    double total_duration_seconds{};
};

class AudioClipLibrary {
  public:
    [[nodiscard]] bool load(
        foundation::ResourceRegistry& resource_registry,
        foundation::CrashSafeLog& log);
    void clear() noexcept;

    [[nodiscard]] const AudioClipRecord* try_get_clip(foundation::ResourceHandle resource) const noexcept;
    [[nodiscard]] const AudioClipRecord* first_clip() const noexcept;
    [[nodiscard]] const AudioClipLibrarySummary& summary() const noexcept;

  private:
    std::unordered_map<std::uint64_t, AudioClipRecord> clips_;
        std::vector<std::uint64_t> clip_load_order_;
    AudioClipLibrarySummary summary_{};
};

} // namespace reaktio::audio