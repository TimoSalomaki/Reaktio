#pragma once

#include "reaktio/platform/ApplicationConfig.hpp"

#include <cstdint>

namespace reaktio::platform {

struct FrameTiming {
    std::uint64_t frame_index{};
    std::uint64_t wall_clock_ns{};
    double frame_delta_seconds{};
    double fixed_step_seconds{};
    double fixed_step_accumulator_seconds{};
    std::uint32_t fixed_steps_this_frame{};
    double interpolation_alpha{};
    bool frame_delta_clamped{false};
};

class FrameClock {
  public:
    explicit FrameClock(MainLoopConfig config) noexcept;

    void begin_frame();
    [[nodiscard]] bool should_run_fixed_step() const noexcept;
    void consume_fixed_step() noexcept;

    [[nodiscard]] std::uint64_t ns_until_next_fixed_step() const noexcept;
    [[nodiscard]] const FrameTiming& timing() const noexcept;

  private:
    void update_interpolation_alpha() noexcept;

    MainLoopConfig config_;
    std::uint64_t performance_frequency_{};
    std::uint64_t last_counter_{};
    double fixed_step_accumulator_seconds_{};
    FrameTiming timing_{};
};

} // namespace reaktio::platform