#pragma once

#include "reaktio/platform/ApplicationConfig.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace reaktio::platform {

struct AudioSpec {
    AudioSampleFormat format{AudioSampleFormat::Unknown};
    int channels{};
    int sample_rate_hz{};
    int bytes_per_frame{};
};

enum class AudioPlaybackPositionMode : std::uint8_t {
    StreamConsumed,
    OutputLatencyCompensated,
};

struct AudioPlaybackProgress {
    bool source_bound{};
    bool device_paused{true};
    std::uint64_t submitted_input_frames{};
    std::uint64_t queued_input_frames{};
    std::uint64_t consumed_input_frames{};
    double stream_consumed_seconds{};
    double queued_input_seconds{};
    double device_latency_seconds{};
    double total_output_latency_seconds{};
    double authoritative_position_seconds{};
    AudioPlaybackPositionMode authoritative_position_mode{AudioPlaybackPositionMode::OutputLatencyCompensated};
};

class IAudioStreamSource {
  public:
    virtual ~IAudioStreamSource() = default;

    virtual int render_audio_frames(
        std::byte* destination,
        int requested_frames,
        const AudioSpec& input_spec) noexcept = 0;
};

[[nodiscard]] inline constexpr std::string_view to_string(AudioPlaybackPositionMode mode) noexcept {
    switch (mode) {
    case AudioPlaybackPositionMode::StreamConsumed:
        return "stream-consumed";
    case AudioPlaybackPositionMode::OutputLatencyCompensated:
        return "output-latency-compensated";
    }

    return "unknown";
}

} // namespace reaktio::platform