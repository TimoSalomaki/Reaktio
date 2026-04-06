#include "reaktio/platform/FrameClock.hpp"

#include <SDL3/SDL_timer.h>

#include <algorithm>

namespace reaktio::platform {

FrameClock::FrameClock(MainLoopConfig config) noexcept
    : config_(config),
      performance_frequency_(SDL_GetPerformanceFrequency()) {
    timing_.fixed_step_seconds = config_.fixed_step_seconds;
}

void FrameClock::begin_frame() {
    const std::uint64_t current_counter = SDL_GetPerformanceCounter();
    const std::uint64_t current_wall_clock = SDL_GetTicksNS();

    double frame_delta_seconds = 0.0;
    if (last_counter_ != 0 && performance_frequency_ != 0) {
        frame_delta_seconds = static_cast<double>(current_counter - last_counter_) /
                              static_cast<double>(performance_frequency_);
    }

    last_counter_ = current_counter;

    timing_.frame_index += 1;
    timing_.wall_clock_ns = current_wall_clock;
    timing_.frame_delta_clamped = frame_delta_seconds > config_.max_frame_delta_seconds;
    timing_.frame_delta_seconds = std::min(frame_delta_seconds, config_.max_frame_delta_seconds);
    timing_.fixed_steps_this_frame = 0;

    fixed_step_accumulator_seconds_ += timing_.frame_delta_seconds;
    timing_.fixed_step_accumulator_seconds = fixed_step_accumulator_seconds_;
    update_interpolation_alpha();
}

bool FrameClock::should_run_fixed_step() const noexcept {
    return fixed_step_accumulator_seconds_ >= config_.fixed_step_seconds &&
           timing_.fixed_steps_this_frame < config_.max_fixed_steps_per_frame;
}

void FrameClock::consume_fixed_step() noexcept {
    if (!should_run_fixed_step()) {
        return;
    }

    fixed_step_accumulator_seconds_ -= config_.fixed_step_seconds;
    timing_.fixed_steps_this_frame += 1;
    timing_.fixed_step_accumulator_seconds = fixed_step_accumulator_seconds_;
    update_interpolation_alpha();
}

std::uint64_t FrameClock::ns_until_next_fixed_step() const noexcept {
    if (config_.fixed_step_seconds <= fixed_step_accumulator_seconds_) {
        return 0;
    }

    const double seconds_until_next_step = config_.fixed_step_seconds - fixed_step_accumulator_seconds_;
    const double nanoseconds = seconds_until_next_step * 1'000'000'000.0;
    return nanoseconds <= 0.0 ? 0 : static_cast<std::uint64_t>(nanoseconds);
}

const FrameTiming& FrameClock::timing() const noexcept {
    return timing_;
}

void FrameClock::update_interpolation_alpha() noexcept {
    if (config_.fixed_step_seconds <= 0.0) {
        timing_.interpolation_alpha = 0.0;
        return;
    }

    timing_.interpolation_alpha = std::clamp(
        fixed_step_accumulator_seconds_ / config_.fixed_step_seconds,
        0.0,
        1.0);
}

} // namespace reaktio::platform