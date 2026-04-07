#include "reaktio/gameplay/TransportStub.hpp"

#include <algorithm>
#include <cmath>

namespace reaktio::gameplay {

TransportStub::TransportStub(double duration_seconds) noexcept {
    set_duration(duration_seconds);
}

const TransportSnapshot& TransportStub::snapshot() const noexcept {
    return snapshot_;
}

void TransportStub::play() noexcept {
    if (snapshot_.position_seconds >= snapshot_.duration_seconds) {
        snapshot_.position_seconds = 0.0;
    }

    snapshot_.playback_state = TransportPlaybackState::Playing;
}

void TransportStub::pause() noexcept {
    if (snapshot_.playback_state == TransportPlaybackState::Playing) {
        snapshot_.playback_state = TransportPlaybackState::Paused;
    }
}

void TransportStub::stop() noexcept {
    snapshot_.playback_state = TransportPlaybackState::Stopped;
    snapshot_.position_seconds = 0.0;
}

void TransportStub::restart() noexcept {
    snapshot_.position_seconds = 0.0;
    snapshot_.playback_state = TransportPlaybackState::Playing;
}

void TransportStub::seek(double position_seconds) noexcept {
    snapshot_.position_seconds = clamp_position(position_seconds, snapshot_.duration_seconds);
}

void TransportStub::set_loop_region(double start_seconds, double end_seconds) noexcept {
    const double clamped_start = clamp_position(start_seconds, snapshot_.duration_seconds);
    const double clamped_end = clamp_position(end_seconds, snapshot_.duration_seconds);
    if (!is_valid_loop_region(clamped_start, clamped_end)) {
        clear_loop_region();
        return;
    }

    snapshot_.loop_region = TransportLoopRegion{
        .start_seconds = clamped_start,
        .end_seconds = clamped_end,
        .enabled = true,
    };

    if (snapshot_.position_seconds < snapshot_.loop_region.start_seconds ||
        snapshot_.position_seconds > snapshot_.loop_region.end_seconds) {
        snapshot_.position_seconds = snapshot_.loop_region.start_seconds;
    }
}

void TransportStub::clear_loop_region() noexcept {
    snapshot_.loop_region = TransportLoopRegion{};
}

void TransportStub::set_duration(double duration_seconds) noexcept {
    snapshot_.duration_seconds = std::max(duration_seconds, 1.0);
    snapshot_.position_seconds = clamp_position(snapshot_.position_seconds, snapshot_.duration_seconds);
    if (snapshot_.loop_region.enabled) {
        set_loop_region(snapshot_.loop_region.start_seconds, snapshot_.loop_region.end_seconds);
    }
}

void TransportStub::advance(double delta_seconds) noexcept {
    if (snapshot_.playback_state != TransportPlaybackState::Playing || delta_seconds <= 0.0) {
        return;
    }

    ++snapshot_.advanced_fixed_steps;
    snapshot_.position_seconds += delta_seconds * snapshot_.playback_rate;

    if (snapshot_.loop_region.enabled) {
        const double loop_start = snapshot_.loop_region.start_seconds;
        const double loop_end = snapshot_.loop_region.end_seconds;
        const double loop_length = loop_end - loop_start;

        if (is_valid_loop_region(loop_start, loop_end) && loop_length > 0.0) {
            if (snapshot_.position_seconds >= loop_end) {
                const double loop_offset = snapshot_.position_seconds - loop_start;
                const double completed_loops = std::floor(loop_offset / loop_length);
                snapshot_.position_seconds = loop_start + std::fmod(loop_offset, loop_length);
                snapshot_.completed_loops += static_cast<std::uint64_t>(completed_loops);
            }
        }
    }

    if (snapshot_.position_seconds >= snapshot_.duration_seconds) {
        snapshot_.position_seconds = snapshot_.duration_seconds;
        snapshot_.playback_state = TransportPlaybackState::Stopped;
    }
}

double TransportStub::clamp_position(double position_seconds, double duration_seconds) noexcept {
    return std::clamp(position_seconds, 0.0, duration_seconds);
}

bool TransportStub::is_valid_loop_region(double start_seconds, double end_seconds) noexcept {
    return end_seconds - start_seconds >= 0.001;
}

} // namespace reaktio::gameplay