#include "reaktio/gameplay/TransportController.hpp"

#include <algorithm>
#include <cmath>

namespace reaktio::gameplay {

TransportController::TransportController(double duration_seconds) noexcept {
    set_duration(duration_seconds);
    refresh_diagnostics();
}

const TransportSnapshot& TransportController::snapshot() const noexcept {
    return snapshot_;
}

const TransportDiagnostics& TransportController::diagnostics() const noexcept {
    return diagnostics_;
}

void TransportController::play() noexcept {
    const double previous_position = snapshot_.position_seconds;
    if (snapshot_.playback_mode == TransportPlaybackMode::Preview && snapshot_.preview_region.enabled) {
        snapshot_.position_seconds = clamp_position(
            snapshot_.position_seconds,
            snapshot_.preview_region.start_seconds,
            snapshot_.preview_region.end_seconds);
    } else if (snapshot_.position_seconds >= snapshot_.duration_seconds) {
        snapshot_.position_seconds = snapshot_.loop_region.enabled
            ? snapshot_.loop_region.start_seconds
            : 0.0;
        if (position_changed(previous_position, snapshot_.position_seconds)) {
            record_discontinuity(TransportDiscontinuityReason::Restart, previous_position, snapshot_.position_seconds);
        }
    }

    snapshot_.playback_state = TransportPlaybackState::Playing;
    refresh_diagnostics();
}

void TransportController::pause() noexcept {
    if (snapshot_.playback_state == TransportPlaybackState::Playing) {
        snapshot_.playback_state = TransportPlaybackState::Paused;
        refresh_diagnostics();
    }
}

void TransportController::stop() noexcept {
    const double previous_position = snapshot_.position_seconds;
    const TransportPlaybackState previous_state = snapshot_.playback_state;
    const bool was_preview = snapshot_.playback_mode == TransportPlaybackMode::Preview;
    snapshot_.playback_state = TransportPlaybackState::Stopped;
    snapshot_.position_seconds = 0.0;
    clear_preview_region();
    if (was_preview || previous_state != TransportPlaybackState::Stopped || position_changed(previous_position, 0.0)) {
        record_discontinuity(TransportDiscontinuityReason::Stop, previous_position, snapshot_.position_seconds);
    }
    refresh_diagnostics();
}

void TransportController::restart() noexcept {
    const double previous_position = snapshot_.position_seconds;
    if (snapshot_.playback_mode == TransportPlaybackMode::Preview && snapshot_.preview_region.enabled) {
        snapshot_.position_seconds = snapshot_.preview_region.start_seconds;
    } else if (snapshot_.loop_region.enabled) {
        snapshot_.position_seconds = snapshot_.loop_region.start_seconds;
    } else {
        snapshot_.position_seconds = 0.0;
    }

    snapshot_.playback_state = TransportPlaybackState::Playing;
    record_discontinuity(TransportDiscontinuityReason::Restart, previous_position, snapshot_.position_seconds);
    refresh_diagnostics();
}

void TransportController::seek(double position_seconds) noexcept {
    const double previous_position = snapshot_.position_seconds;
    snapshot_.position_seconds = clamp_position(position_seconds, snapshot_.duration_seconds);
    if (snapshot_.playback_mode == TransportPlaybackMode::Preview && snapshot_.preview_region.enabled) {
        snapshot_.position_seconds = clamp_position(
            snapshot_.position_seconds,
            snapshot_.preview_region.start_seconds,
            snapshot_.preview_region.end_seconds);
    }

    if (position_changed(previous_position, snapshot_.position_seconds)) {
        record_discontinuity(TransportDiscontinuityReason::Seek, previous_position, snapshot_.position_seconds);
    }
    refresh_diagnostics();
}

void TransportController::preview(double start_seconds, double end_seconds) noexcept {
    const double previous_position = snapshot_.position_seconds;
    const double clamped_start = clamp_position(start_seconds, snapshot_.duration_seconds);
    const double clamped_end = clamp_position(end_seconds, snapshot_.duration_seconds);
    if (!is_valid_region(clamped_start, clamped_end)) {
        return;
    }

    snapshot_.preview_region = TransportPreviewRegion{
        .start_seconds = clamped_start,
        .end_seconds = clamped_end,
        .enabled = true,
    };
    snapshot_.playback_mode = TransportPlaybackMode::Preview;
    snapshot_.position_seconds = clamped_start;
    snapshot_.playback_state = TransportPlaybackState::Playing;
    record_discontinuity(TransportDiscontinuityReason::PreviewStart, previous_position, snapshot_.position_seconds);
    refresh_diagnostics();
}

void TransportController::set_loop_region(double start_seconds, double end_seconds) noexcept {
    const double clamped_start = clamp_position(start_seconds, snapshot_.duration_seconds);
    const double clamped_end = clamp_position(end_seconds, snapshot_.duration_seconds);
    if (!is_valid_region(clamped_start, clamped_end)) {
        clear_loop_region();
        return;
    }

    snapshot_.loop_region = TransportLoopRegion{
        .start_seconds = clamped_start,
        .end_seconds = clamped_end,
        .enabled = true,
    };

    if (snapshot_.playback_mode == TransportPlaybackMode::Normal &&
        (snapshot_.position_seconds < snapshot_.loop_region.start_seconds ||
         snapshot_.position_seconds > snapshot_.loop_region.end_seconds)) {
        const double previous_position = snapshot_.position_seconds;
        snapshot_.position_seconds = snapshot_.loop_region.start_seconds;
        record_discontinuity(
            TransportDiscontinuityReason::LoopRegionClamp,
            previous_position,
            snapshot_.position_seconds);
    }
    refresh_diagnostics();
}

void TransportController::clear_loop_region() noexcept {
    snapshot_.loop_region = TransportLoopRegion{};
    refresh_diagnostics();
}

void TransportController::set_duration(double duration_seconds) noexcept {
    const double previous_position = snapshot_.position_seconds;
    snapshot_.duration_seconds = std::max(duration_seconds, 1.0);
    snapshot_.position_seconds = clamp_position(snapshot_.position_seconds, snapshot_.duration_seconds);

    if (snapshot_.loop_region.enabled) {
        const double clamped_start = clamp_position(snapshot_.loop_region.start_seconds, snapshot_.duration_seconds);
        const double clamped_end = clamp_position(snapshot_.loop_region.end_seconds, snapshot_.duration_seconds);
        if (!is_valid_region(clamped_start, clamped_end)) {
            clear_loop_region();
        } else {
            snapshot_.loop_region.start_seconds = clamped_start;
            snapshot_.loop_region.end_seconds = clamped_end;
        }
    }

    if (snapshot_.preview_region.enabled) {
        const double clamped_start = clamp_position(snapshot_.preview_region.start_seconds, snapshot_.duration_seconds);
        const double clamped_end = clamp_position(snapshot_.preview_region.end_seconds, snapshot_.duration_seconds);
        if (!is_valid_region(clamped_start, clamped_end)) {
            clear_preview_region();
        } else {
            snapshot_.preview_region.start_seconds = clamped_start;
            snapshot_.preview_region.end_seconds = clamped_end;
            if (snapshot_.playback_mode == TransportPlaybackMode::Preview) {
                snapshot_.position_seconds = clamp_position(
                    snapshot_.position_seconds,
                    snapshot_.preview_region.start_seconds,
                    snapshot_.preview_region.end_seconds);
            }
        }
    }

    if (position_changed(previous_position, snapshot_.position_seconds)) {
        record_discontinuity(TransportDiscontinuityReason::DurationClamp, previous_position, snapshot_.position_seconds);
    }
    refresh_diagnostics();
}

void TransportController::set_playback_rate(double playback_rate) noexcept {
    if (!std::isfinite(playback_rate)) {
        return;
    }

    snapshot_.playback_rate = std::clamp(playback_rate, 0.01, 8.0);
    refresh_diagnostics();
}

void TransportController::advance(double delta_seconds) noexcept {
    snapshot_.position_authority = TransportPositionAuthority::Simulation;
    if (snapshot_.playback_state != TransportPlaybackState::Playing || delta_seconds <= 0.0) {
        refresh_diagnostics();
        return;
    }

    ++snapshot_.advanced_fixed_steps;
    const double delta_position = delta_seconds * snapshot_.playback_rate;

    if (snapshot_.playback_mode == TransportPlaybackMode::Preview && snapshot_.preview_region.enabled) {
        const double preview_end = snapshot_.preview_region.end_seconds;
        const double next_position = snapshot_.position_seconds + delta_position;
        if (next_position >= preview_end) {
            const double previous_position = snapshot_.position_seconds;
            snapshot_.position_seconds = preview_end;
            snapshot_.playback_state = TransportPlaybackState::Stopped;
            ++snapshot_.completed_previews;
            record_discontinuity(
                TransportDiscontinuityReason::PreviewComplete,
                previous_position,
                snapshot_.position_seconds);
            clear_preview_region();
            refresh_diagnostics();
            return;
        }

        snapshot_.position_seconds = clamp_position(
            next_position,
            snapshot_.preview_region.start_seconds,
            snapshot_.preview_region.end_seconds);
        refresh_diagnostics();
        return;
    }

    snapshot_.position_seconds += delta_position;
    if (snapshot_.loop_region.enabled) {
        const double loop_start = snapshot_.loop_region.start_seconds;
        const double loop_end = snapshot_.loop_region.end_seconds;
        const double loop_length = loop_end - loop_start;
        if (is_valid_region(loop_start, loop_end) && loop_length > 0.0 && snapshot_.position_seconds >= loop_end) {
            const double previous_position = snapshot_.position_seconds;
            const double loop_offset = snapshot_.position_seconds - loop_start;
            const double completed_loops = std::floor(loop_offset / loop_length);
            snapshot_.position_seconds = loop_start + std::fmod(loop_offset, loop_length);
            snapshot_.completed_loops += static_cast<std::uint64_t>(completed_loops);
            record_discontinuity(
                TransportDiscontinuityReason::LoopWrap,
                previous_position,
                snapshot_.position_seconds);
        }
    }

    if (snapshot_.position_seconds >= snapshot_.duration_seconds) {
        snapshot_.position_seconds = snapshot_.duration_seconds;
        snapshot_.playback_state = TransportPlaybackState::Stopped;
    }
    refresh_diagnostics();
}

void TransportController::sync_from_authoritative_snapshot(const TransportSnapshot& snapshot) noexcept {
    snapshot_ = snapshot;
    refresh_diagnostics();
}

double TransportController::clamp_position(double position_seconds, double duration_seconds) noexcept {
    return std::clamp(position_seconds, 0.0, duration_seconds);
}

double TransportController::clamp_position(
    double position_seconds,
    double start_seconds,
    double end_seconds) noexcept {
    return std::clamp(position_seconds, start_seconds, end_seconds);
}

bool TransportController::is_valid_region(double start_seconds, double end_seconds) noexcept {
    return end_seconds - start_seconds >= 0.001;
}

bool TransportController::position_changed(double lhs, double rhs) noexcept {
    return std::abs(lhs - rhs) > 0.000001;
}

void TransportController::refresh_diagnostics() noexcept {
    diagnostics_.using_audio_authority = snapshot_.position_authority == TransportPositionAuthority::AudioOutput;
    diagnostics_.authoritative_position_seconds = snapshot_.position_seconds;
    diagnostics_.simulation_position_seconds = snapshot_.position_seconds;
    diagnostics_.drift_seconds = 0.0;
    diagnostics_.stream_consumed_seconds = snapshot_.position_seconds;
    diagnostics_.reported_output_position_seconds = snapshot_.position_seconds;
    diagnostics_.queued_input_seconds = 0.0;
    diagnostics_.device_latency_seconds = 0.0;
    diagnostics_.total_output_latency_seconds = 0.0;
}

void TransportController::clear_preview_region() noexcept {
    snapshot_.preview_region = TransportPreviewRegion{};
    snapshot_.playback_mode = TransportPlaybackMode::Normal;
}

void TransportController::record_discontinuity(
    TransportDiscontinuityReason reason,
    double from_seconds,
    double to_seconds) noexcept {
    ++snapshot_.discontinuity.timeline_revision;
    ++snapshot_.discontinuity.discontinuity_count;
    snapshot_.discontinuity.last_reason = reason;
    snapshot_.discontinuity.last_from_seconds = from_seconds;
    snapshot_.discontinuity.last_to_seconds = to_seconds;
}

} // namespace reaktio::gameplay