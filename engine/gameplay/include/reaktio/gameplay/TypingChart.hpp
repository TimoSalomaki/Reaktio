#pragma once

#include "reaktio/gameplay/CueScheduler.hpp"
#include "reaktio/gameplay/TypingPrompt.hpp"
#include "reaktio/rhythm/TempoMap.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace reaktio::gameplay {

// Deterministic falling-token chart for typing-family modes. Lives in the
// gameplay layer so any falling-character mode (typing trainer, piano-tile,
// finger-drum, lyric-typing) can build cues from a TypingPrompt without
// re-implementing density and travel scheduling.

struct TypingChartConfig {
    std::uint32_t lane_count{1};
    rhythm::ChartTick ticks_per_grapheme{480};   // density: lower = denser cues per beat.
    rhythm::ChartTick travel_lead_ticks{960};    // time the cue is on screen before judgement.
    rhythm::ChartTick first_judge_tick{960};     // when the first grapheme should be judged.
    bool round_robin_lanes{true};                // false -> hash-based assignment.
};

struct TypingChartCue {
    std::size_t prompt_index{};
    std::uint32_t lane_index{};
    rhythm::ChartTick spawn_tick{};
    rhythm::ChartTick judge_tick{};
};

struct TypingChart {
    TypingChartConfig config{};
    std::vector<TypingChartCue> cues;
    rhythm::ChartTick last_judge_tick{};
};

[[nodiscard]] TypingChart make_typing_chart(
    const TypingPrompt& prompt,
    const TypingChartConfig& config);

[[nodiscard]] std::vector<rhythm::ScheduledCue> make_scheduler_cues_from_typing_chart(
    const TypingChart& chart);

[[nodiscard]] const TypingChartCue* find_cue_by_prompt_index(
    const TypingChart& chart,
    std::size_t prompt_index) noexcept;

} // namespace reaktio::gameplay
