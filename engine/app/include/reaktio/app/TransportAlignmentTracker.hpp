#pragma once

#include "reaktio/gameplay/Transport.hpp"
#include "reaktio/gameplay/TransportController.hpp"
#include "reaktio/platform/AudioStreamSource.hpp"

namespace reaktio::app {

class TransportAlignmentTracker {
  public:
    void reset_to_authoritative(
        const gameplay::TransportSnapshot& authoritative_snapshot,
        bool clear_history = true) noexcept;
    void update_from_audio(
        double simulation_delta_seconds,
        const gameplay::TransportSnapshot& authoritative_snapshot,
        const platform::AudioPlaybackProgress& playback_progress) noexcept;
    void update_from_simulation(const gameplay::TransportSnapshot& simulation_snapshot) noexcept;

    [[nodiscard]] const gameplay::TransportDiagnostics& diagnostics() const noexcept;

  private:
    static gameplay::TransportSnapshot make_simulation_snapshot(
        const gameplay::TransportSnapshot& authoritative_snapshot) noexcept;

    void reset_history() noexcept;
    void sync_simulation_transport(
        const gameplay::TransportSnapshot& authoritative_snapshot,
        gameplay::TransportCorrectionType correction_type) noexcept;
    void record_correction(
        gameplay::TransportCorrectionType correction_type,
        double authoritative_position_seconds,
        double simulation_position_before_seconds,
        double simulation_position_after_seconds,
        double drift_before_seconds) noexcept;

    gameplay::TransportController simulation_transport_{};
    gameplay::TransportDiagnostics diagnostics_{};
    std::uint64_t correction_sequence_{};
    std::uint64_t last_authoritative_timeline_revision_{};
    gameplay::TransportPlaybackState last_authoritative_playback_state_{};
    gameplay::TransportPlaybackMode last_authoritative_playback_mode_{};
};

} // namespace reaktio::app