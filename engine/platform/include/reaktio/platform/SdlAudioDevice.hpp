#pragma once

#include "reaktio/platform/AudioStreamSource.hpp"
#include "reaktio/platform/ApplicationConfig.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace reaktio::platform {

enum class AudioDeviceOpenState : std::uint8_t {
    Disabled,
    Opened,
    Unavailable,
};

enum class AudioLatencyQueryMode : std::uint8_t {
    None,
    DevicePeriodOnly,
    DevicePeriodPlusQueuedStream,
};

struct AudioLatencyReport {
    int device_buffer_frames{};
    int queued_stream_frames{};
    double device_period_ms{};
    double queued_stream_latency_ms{};
    double total_output_latency_ms{};
    AudioLatencyQueryMode query_mode{AudioLatencyQueryMode::None};
};

struct AudioDeviceInfo {
    AudioDeviceOpenState state{AudioDeviceOpenState::Disabled};
    bool playback{true};
    bool using_default_device{true};
    bool paused{true};
    std::uint32_t logical_device_id{};
    std::string driver_name{"<uninitialized>"};
    std::string device_name{"<none>"};
    std::string status_message{"playback device disabled by configuration"};
    AudioSpec requested_spec{};
    AudioSpec actual_spec{};
    AudioLatencyReport latency{};
    float gain{1.0f};
};

[[nodiscard]] inline constexpr std::string_view to_string(AudioDeviceOpenState state) noexcept {
    switch (state) {
    case AudioDeviceOpenState::Disabled:
        return "disabled";
    case AudioDeviceOpenState::Opened:
        return "opened";
    case AudioDeviceOpenState::Unavailable:
        return "unavailable";
    }

    return "unknown";
}

[[nodiscard]] inline constexpr std::string_view to_string(AudioLatencyQueryMode mode) noexcept {
    switch (mode) {
    case AudioLatencyQueryMode::None:
        return "none";
    case AudioLatencyQueryMode::DevicePeriodOnly:
        return "device-period-only";
    case AudioLatencyQueryMode::DevicePeriodPlusQueuedStream:
        return "device-period-plus-queued-stream";
    }

    return "unknown";
}

class SdlAudioDevice {
  public:
    explicit SdlAudioDevice(AudioConfig config);
    ~SdlAudioDevice();

    SdlAudioDevice(const SdlAudioDevice&) = delete;
    SdlAudioDevice& operator=(const SdlAudioDevice&) = delete;

    bool initialize();
    void shutdown() noexcept;

        bool bind_playback_source(IAudioStreamSource& source) noexcept;
        void unbind_playback_source() noexcept;
        bool clear_stream() noexcept;
        void set_input_frame_origin(std::uint64_t frame_index) noexcept;
        bool pause_playback() noexcept;
        bool resume_playback() noexcept;

    [[nodiscard]] const AudioConfig& config() const noexcept;
    [[nodiscard]] const AudioDeviceInfo& info() const noexcept;
        [[nodiscard]] AudioPlaybackProgress playback_progress() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace reaktio::platform