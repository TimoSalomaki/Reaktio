#pragma once

#include <cstdint>
#include <string_view>

namespace reaktio::gameplay {

enum class TransportPlaybackState {
    Stopped,
    Playing,
    Paused,
};

enum class TransportPlaybackMode {
    Normal,
    Preview,
};

enum class TransportPositionAuthority {
    Simulation,
    AudioOutput,
};

enum class TransportDiscontinuityReason {
    None,
    Stop,
    Restart,
    Seek,
    PreviewStart,
    PreviewComplete,
    LoopWrap,
    LoopRegionClamp,
    DurationClamp,
};

struct TransportLoopRegion {
    double start_seconds{};
    double end_seconds{};
    bool enabled{false};
};

struct TransportPreviewRegion {
    double start_seconds{};
    double end_seconds{};
    bool enabled{false};
};

struct TransportDiscontinuityState {
    std::uint64_t timeline_revision{};
    std::uint64_t discontinuity_count{};
    TransportDiscontinuityReason last_reason{TransportDiscontinuityReason::None};
    double last_from_seconds{};
    double last_to_seconds{};
};

struct TransportSnapshot {
    TransportPlaybackState playback_state{TransportPlaybackState::Stopped};
    TransportPlaybackMode playback_mode{TransportPlaybackMode::Normal};
    TransportPositionAuthority position_authority{TransportPositionAuthority::Simulation};
    double position_seconds{};
    double duration_seconds{180.0};
    double playback_rate{1.0};
    TransportLoopRegion loop_region{};
    TransportPreviewRegion preview_region{};
    std::uint64_t completed_loops{};
    std::uint64_t completed_previews{};
    std::uint64_t advanced_fixed_steps{};
    TransportDiscontinuityState discontinuity{};
};

class ITransportControl {
  public:
    virtual ~ITransportControl() = default;

    [[nodiscard]] virtual const TransportSnapshot& snapshot() const noexcept = 0;
    virtual void play() noexcept = 0;
    virtual void pause() noexcept = 0;
    virtual void stop() noexcept = 0;
    virtual void restart() noexcept = 0;
    virtual void seek(double position_seconds) noexcept = 0;
    virtual void preview(double start_seconds, double end_seconds) noexcept = 0;
    virtual void set_loop_region(double start_seconds, double end_seconds) noexcept = 0;
    virtual void clear_loop_region() noexcept = 0;
};

[[nodiscard]] inline constexpr std::string_view to_string(TransportPlaybackState playback_state) noexcept {
    switch (playback_state) {
    case TransportPlaybackState::Stopped:
        return "stopped";
    case TransportPlaybackState::Playing:
        return "playing";
    case TransportPlaybackState::Paused:
        return "paused";
    }

    return "unknown";
}

[[nodiscard]] inline constexpr std::string_view to_string(TransportPlaybackMode playback_mode) noexcept {
    switch (playback_mode) {
    case TransportPlaybackMode::Normal:
        return "normal";
    case TransportPlaybackMode::Preview:
        return "preview";
    }

    return "unknown";
}

[[nodiscard]] inline constexpr std::string_view to_string(TransportPositionAuthority authority) noexcept {
    switch (authority) {
    case TransportPositionAuthority::Simulation:
        return "simulation";
    case TransportPositionAuthority::AudioOutput:
        return "audio-output";
    }

    return "unknown";
}

[[nodiscard]] inline constexpr std::string_view to_string(TransportDiscontinuityReason reason) noexcept {
    switch (reason) {
    case TransportDiscontinuityReason::None:
        return "none";
    case TransportDiscontinuityReason::Stop:
        return "stop";
    case TransportDiscontinuityReason::Restart:
        return "restart";
    case TransportDiscontinuityReason::Seek:
        return "seek";
    case TransportDiscontinuityReason::PreviewStart:
        return "preview-start";
    case TransportDiscontinuityReason::PreviewComplete:
        return "preview-complete";
    case TransportDiscontinuityReason::LoopWrap:
        return "loop-wrap";
    case TransportDiscontinuityReason::LoopRegionClamp:
        return "loop-region-clamp";
    case TransportDiscontinuityReason::DurationClamp:
        return "duration-clamp";
    }

    return "unknown";
}

} // namespace reaktio::gameplay