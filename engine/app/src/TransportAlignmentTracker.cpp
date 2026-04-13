#include "reaktio/app/TransportAlignmentTracker.hpp"

#include <algorithm>
#include <cmath>

namespace reaktio::app {

namespace {

constexpr double k_position_epsilon_seconds = 0.000001;

double absolute_value(double value) noexcept {
    return std::abs(value);
}

} // namespace

void TransportAlignmentTracker::reset_to_authoritative(
    const gameplay::TransportSnapshot& authoritative_snapshot,
    bool clear_history) noexcept {
    simulation_transport_ = gameplay::TransportController{authoritative_snapshot.duration_seconds};
    simulation_transport_.sync_from_authoritative_snapshot(make_simulation_snapshot(authoritative_snapshot));

    if (clear_history) {
        reset_history();
    }

    last_authoritative_timeline_revision_ = authoritative_snapshot.discontinuity.timeline_revision;
    last_authoritative_playback_state_ = authoritative_snapshot.playback_state;
    last_authoritative_playback_mode_ = authoritative_snapshot.playback_mode;

    diagnostics_.using_audio_authority = authoritative_snapshot.position_authority == gameplay::TransportPositionAuthority::AudioOutput;
    diagnostics_.authoritative_position_seconds = authoritative_snapshot.position_seconds;
    diagnostics_.simulation_position_seconds = authoritative_snapshot.position_seconds;
    diagnostics_.drift_seconds = 0.0;
    diagnostics_.stream_consumed_seconds = authoritative_snapshot.position_seconds;
    diagnostics_.reported_output_position_seconds = authoritative_snapshot.position_seconds;
    diagnostics_.queued_input_seconds = 0.0;
    diagnostics_.device_latency_seconds = 0.0;
    diagnostics_.total_output_latency_seconds = 0.0;
}

void TransportAlignmentTracker::update_from_audio(
    double simulation_delta_seconds,
    const gameplay::TransportSnapshot& authoritative_snapshot,
    const platform::AudioPlaybackProgress& playback_progress) noexcept {
    bool snapped_to_authoritative = false;
    if (simulation_transport_.snapshot().duration_seconds != authoritative_snapshot.duration_seconds) {
        sync_simulation_transport(authoritative_snapshot, gameplay::TransportCorrectionType::DiscontinuitySnap);
        snapped_to_authoritative = true;
    }

    const bool discontinuity_changed =
        authoritative_snapshot.discontinuity.timeline_revision != last_authoritative_timeline_revision_;
    const bool playback_mode_changed = authoritative_snapshot.playback_mode != last_authoritative_playback_mode_;
    const bool playback_state_changed = authoritative_snapshot.playback_state != last_authoritative_playback_state_;
    if (!snapped_to_authoritative && (discontinuity_changed || playback_mode_changed || playback_state_changed)) {
        sync_simulation_transport(authoritative_snapshot, gameplay::TransportCorrectionType::DiscontinuitySnap);
        snapped_to_authoritative = true;
    }

    if (!snapped_to_authoritative && simulation_delta_seconds > 0.0) {
        simulation_transport_.advance(simulation_delta_seconds);
    }

    double simulation_position = simulation_transport_.snapshot().position_seconds;
    double drift_seconds = authoritative_snapshot.position_seconds - simulation_position;

    if (!snapped_to_authoritative && authoritative_snapshot.playback_state == gameplay::TransportPlaybackState::Playing &&
        simulation_delta_seconds > 0.0) {
        const double absolute_drift = absolute_value(drift_seconds);
        if (absolute_drift >= diagnostics_.correction_policy.hard_snap_threshold_seconds) {
            sync_simulation_transport(authoritative_snapshot, gameplay::TransportCorrectionType::HardSnap);
            simulation_position = simulation_transport_.snapshot().position_seconds;
            drift_seconds = authoritative_snapshot.position_seconds - simulation_position;
        } else if (absolute_drift >= diagnostics_.correction_policy.soft_correction_threshold_seconds) {
            const double simulation_position_before = simulation_position;
            const double correction_step = std::clamp(
                drift_seconds,
                -diagnostics_.correction_policy.max_soft_correction_step_seconds,
                diagnostics_.correction_policy.max_soft_correction_step_seconds);
            simulation_transport_.seek(simulation_position + correction_step);
            simulation_position = simulation_transport_.snapshot().position_seconds;
            record_correction(
                gameplay::TransportCorrectionType::SoftNudge,
                authoritative_snapshot.position_seconds,
                simulation_position_before,
                simulation_position,
                drift_seconds);
            drift_seconds = authoritative_snapshot.position_seconds - simulation_position;
        }
    }

    diagnostics_.using_audio_authority = true;
    diagnostics_.authoritative_position_seconds = authoritative_snapshot.position_seconds;
    diagnostics_.simulation_position_seconds = simulation_position;
    diagnostics_.drift_seconds = drift_seconds;
    diagnostics_.stream_consumed_seconds = playback_progress.stream_consumed_seconds;
    diagnostics_.reported_output_position_seconds = playback_progress.authoritative_position_seconds;
    diagnostics_.queued_input_seconds = playback_progress.queued_input_seconds;
    diagnostics_.device_latency_seconds = playback_progress.device_latency_seconds;
    diagnostics_.total_output_latency_seconds = playback_progress.total_output_latency_seconds;

    last_authoritative_timeline_revision_ = authoritative_snapshot.discontinuity.timeline_revision;
    last_authoritative_playback_state_ = authoritative_snapshot.playback_state;
    last_authoritative_playback_mode_ = authoritative_snapshot.playback_mode;
}

void TransportAlignmentTracker::update_from_simulation(const gameplay::TransportSnapshot& simulation_snapshot) noexcept {
    simulation_transport_ = gameplay::TransportController{simulation_snapshot.duration_seconds};
    simulation_transport_.sync_from_authoritative_snapshot(make_simulation_snapshot(simulation_snapshot));
    last_authoritative_timeline_revision_ = simulation_snapshot.discontinuity.timeline_revision;
    last_authoritative_playback_state_ = simulation_snapshot.playback_state;
    last_authoritative_playback_mode_ = simulation_snapshot.playback_mode;

    diagnostics_.using_audio_authority = false;
    diagnostics_.authoritative_position_seconds = simulation_snapshot.position_seconds;
    diagnostics_.simulation_position_seconds = simulation_snapshot.position_seconds;
    diagnostics_.drift_seconds = 0.0;
    diagnostics_.stream_consumed_seconds = simulation_snapshot.position_seconds;
    diagnostics_.reported_output_position_seconds = simulation_snapshot.position_seconds;
    diagnostics_.queued_input_seconds = 0.0;
    diagnostics_.device_latency_seconds = 0.0;
    diagnostics_.total_output_latency_seconds = 0.0;
}

const gameplay::TransportDiagnostics& TransportAlignmentTracker::diagnostics() const noexcept {
    return diagnostics_;
}

gameplay::TransportSnapshot TransportAlignmentTracker::make_simulation_snapshot(
    const gameplay::TransportSnapshot& authoritative_snapshot) noexcept {
    gameplay::TransportSnapshot simulation_snapshot = authoritative_snapshot;
    simulation_snapshot.position_authority = gameplay::TransportPositionAuthority::Simulation;
    return simulation_snapshot;
}

void TransportAlignmentTracker::reset_history() noexcept {
    correction_sequence_ = 0;
    diagnostics_.correction_count = 0;
    diagnostics_.recent_correction_count = 0;
    diagnostics_.recent_corrections.fill({});
}

void TransportAlignmentTracker::sync_simulation_transport(
    const gameplay::TransportSnapshot& authoritative_snapshot,
    gameplay::TransportCorrectionType correction_type) noexcept {
    const double simulation_position_before = simulation_transport_.snapshot().position_seconds;
    simulation_transport_ = gameplay::TransportController{authoritative_snapshot.duration_seconds};
    simulation_transport_.sync_from_authoritative_snapshot(make_simulation_snapshot(authoritative_snapshot));
    const double simulation_position_after = simulation_transport_.snapshot().position_seconds;
    if (correction_type != gameplay::TransportCorrectionType::None &&
        absolute_value(simulation_position_after - simulation_position_before) > k_position_epsilon_seconds) {
        record_correction(
            correction_type,
            authoritative_snapshot.position_seconds,
            simulation_position_before,
            simulation_position_after,
            authoritative_snapshot.position_seconds - simulation_position_before);
    }
}

void TransportAlignmentTracker::record_correction(
    gameplay::TransportCorrectionType correction_type,
    double authoritative_position_seconds,
    double simulation_position_before_seconds,
    double simulation_position_after_seconds,
    double drift_before_seconds) noexcept {
    if (correction_type == gameplay::TransportCorrectionType::None) {
        return;
    }

    ++correction_sequence_;
    ++diagnostics_.correction_count;
    const std::size_t history_capacity = diagnostics_.recent_corrections.size();
    const std::size_t shifted_count = std::min(diagnostics_.recent_correction_count, history_capacity - 1u);
    for (std::size_t index = shifted_count; index > 0; --index) {
        diagnostics_.recent_corrections[index] = diagnostics_.recent_corrections[index - 1u];
    }

    diagnostics_.recent_corrections[0] = gameplay::TransportCorrectionEvent{
        .sequence = correction_sequence_,
        .correction_type = correction_type,
        .authoritative_position_seconds = authoritative_position_seconds,
        .simulation_position_before_seconds = simulation_position_before_seconds,
        .simulation_position_after_seconds = simulation_position_after_seconds,
        .drift_before_seconds = drift_before_seconds,
        .correction_applied_seconds = simulation_position_after_seconds - simulation_position_before_seconds,
    };
    diagnostics_.recent_correction_count = std::min(diagnostics_.recent_correction_count + 1u, history_capacity);
}

} // namespace reaktio::app