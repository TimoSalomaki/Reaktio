#pragma once

#include "reaktio/gameplay/Transport.hpp"

namespace reaktio::gameplay {

class TransportStub final : public ITransportControl {
  public:
    explicit TransportStub(double duration_seconds = 180.0) noexcept;

    [[nodiscard]] const TransportSnapshot& snapshot() const noexcept override;
    void play() noexcept override;
    void pause() noexcept override;
    void stop() noexcept override;
    void restart() noexcept override;
    void seek(double position_seconds) noexcept override;
    void set_loop_region(double start_seconds, double end_seconds) noexcept override;
    void clear_loop_region() noexcept override;

    void set_duration(double duration_seconds) noexcept;
    void advance(double delta_seconds) noexcept;

  private:
    static double clamp_position(double position_seconds, double duration_seconds) noexcept;
    static bool is_valid_loop_region(double start_seconds, double end_seconds) noexcept;

    TransportSnapshot snapshot_{};
};

} // namespace reaktio::gameplay