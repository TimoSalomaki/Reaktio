#pragma once

#include <cstdint>
#include <string_view>

namespace reaktio::gameplay {

enum class TransportPlaybackState {
    Stopped,
    Playing,
    Paused,
};

struct TransportLoopRegion {
    double start_seconds{};
    double end_seconds{};
    bool enabled{false};
};

struct TransportSnapshot {
    TransportPlaybackState playback_state{TransportPlaybackState::Stopped};
    double position_seconds{};
    double duration_seconds{180.0};
    double playback_rate{1.0};
    TransportLoopRegion loop_region{};
    std::uint64_t completed_loops{};
    std::uint64_t advanced_fixed_steps{};
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

} // namespace reaktio::gameplay