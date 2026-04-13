#pragma once

#include "reaktio/rhythm/TempoMap.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace reaktio::rhythm {

struct ScheduledCue {
    ChartTick hit_tick{};
    std::uint32_t channel_index{};
};

struct CueTravelWindow {
    ChartTick pre_hit_visible_ticks{960};
    ChartTick post_hit_visible_ticks{240};
};

enum class CueTravelPhase : std::uint8_t {
    Hidden,
    Approaching,
    Release,
};

struct CueTravelState {
    ScheduledCue cue{};
    CueTravelPhase phase{CueTravelPhase::Hidden};
    ChartTick delta_ticks{};
    TimelineMicroseconds delta_microseconds{};
    AudioSampleIndex delta_samples{};
    double delta_seconds{};
    double normalized_progress{};
    double signed_normalized_offset{};
    bool visible{};
};

struct LinearCueTravelPath {
    float spawn_x{-180.0f};
    float hit_x{};
    float release_x{96.0f};
};

[[nodiscard]] CueTravelState sample_cue_travel(
    const TempoMap& tempo_map,
    ChartTick current_tick,
    const ScheduledCue& cue,
    const CueTravelWindow& window) noexcept;
[[nodiscard]] float sample_linear_cue_position_x(
    const CueTravelState& state,
    const LinearCueTravelPath& path) noexcept;
[[nodiscard]] std::size_t collect_upcoming_cues(
    std::span<const ScheduledCue> schedule,
    ChartTick current_tick,
    ChartTick lookahead_ticks,
    std::span<ScheduledCue> output) noexcept;
[[nodiscard]] const ScheduledCue* find_nearest_cue_by_time(
    std::span<const ScheduledCue> schedule,
    const TempoMap& tempo_map,
    TimelineMicroseconds current_microseconds) noexcept;

[[nodiscard]] inline constexpr std::string_view to_string(CueTravelPhase phase) noexcept {
    switch (phase) {
    case CueTravelPhase::Hidden:
        return "hidden";
    case CueTravelPhase::Approaching:
        return "approaching";
    case CueTravelPhase::Release:
        return "release";
    }

    return "unknown";
}

} // namespace reaktio::rhythm