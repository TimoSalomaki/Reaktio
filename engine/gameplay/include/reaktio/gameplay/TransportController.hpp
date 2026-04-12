#pragma once

#include "reaktio/gameplay/Transport.hpp"

namespace reaktio::gameplay {

class TransportController final : public ITransportControl {
  public:
    explicit TransportController(double duration_seconds = 180.0) noexcept;

    [[nodiscard]] const TransportSnapshot& snapshot() const noexcept override;
    void play() noexcept override;
    void pause() noexcept override;
    void stop() noexcept override;
    void restart() noexcept override;
    void seek(double position_seconds) noexcept override;
    void preview(double start_seconds, double end_seconds) noexcept override;
    void set_loop_region(double start_seconds, double end_seconds) noexcept override;
    void clear_loop_region() noexcept override;

    void set_duration(double duration_seconds) noexcept;
    void set_playback_rate(double playback_rate) noexcept;
    void advance(double delta_seconds) noexcept;
    void sync_from_authoritative_snapshot(const TransportSnapshot& snapshot) noexcept;

  private:
    static double clamp_position(double position_seconds, double duration_seconds) noexcept;
    static double clamp_position(double position_seconds, double start_seconds, double end_seconds) noexcept;
    static bool is_valid_region(double start_seconds, double end_seconds) noexcept;
    static bool position_changed(double lhs, double rhs) noexcept;

    void clear_preview_region() noexcept;
    void record_discontinuity(
      TransportDiscontinuityReason reason,
      double from_seconds,
      double to_seconds) noexcept;

    TransportSnapshot snapshot_{};
};

} // namespace reaktio::gameplay