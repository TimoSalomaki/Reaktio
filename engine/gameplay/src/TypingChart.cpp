#include "reaktio/gameplay/TypingChart.hpp"

#include <algorithm>

namespace reaktio::gameplay {

namespace {

std::uint32_t hash_grapheme_to_lane(const TypingGrapheme& grapheme, std::uint32_t lane_count) noexcept {
    std::uint32_t accumulator = 2166136261u;  // FNV-1a 32 init.
    for (char byte : grapheme) {
        accumulator ^= static_cast<unsigned char>(byte);
        accumulator *= 16777619u;
    }
    return lane_count > 0 ? accumulator % lane_count : 0u;
}

} // namespace

TypingChart make_typing_chart(const TypingPrompt& prompt, const TypingChartConfig& config) {
    TypingChart chart{};
    chart.config = config;
    if (chart.config.lane_count == 0) {
        chart.config.lane_count = 1;
    }
    if (chart.config.ticks_per_grapheme <= 0) {
        chart.config.ticks_per_grapheme = 480;
    }

    chart.cues.reserve(prompt.graphemes.size());
    rhythm::ChartTick judge_tick = chart.config.first_judge_tick;
    for (std::size_t index = 0; index < prompt.graphemes.size(); ++index) {
        TypingChartCue cue{};
        cue.prompt_index = index;
        if (chart.config.round_robin_lanes) {
            cue.lane_index = static_cast<std::uint32_t>(index % chart.config.lane_count);
        } else {
            cue.lane_index = hash_grapheme_to_lane(prompt.graphemes[index], chart.config.lane_count);
        }
        cue.judge_tick = judge_tick;
        cue.spawn_tick = judge_tick - chart.config.travel_lead_ticks;
        if (cue.spawn_tick < 0) {
            cue.spawn_tick = 0;
        }
        chart.cues.push_back(cue);
        chart.last_judge_tick = judge_tick;
        judge_tick += chart.config.ticks_per_grapheme;
    }
    return chart;
}

std::vector<rhythm::ScheduledCue> make_scheduler_cues_from_typing_chart(const TypingChart& chart) {
    std::vector<rhythm::ScheduledCue> cues;
    cues.reserve(chart.cues.size());
    for (const TypingChartCue& chart_cue : chart.cues) {
        cues.push_back(rhythm::ScheduledCue{
            .hit_tick = chart_cue.judge_tick,
            .channel_index = chart_cue.lane_index,
        });
    }
    return cues;
}

const TypingChartCue* find_cue_by_prompt_index(
    const TypingChart& chart,
    std::size_t prompt_index) noexcept {
    if (prompt_index >= chart.cues.size()) {
        return nullptr;
    }
    return &chart.cues[prompt_index];
}

} // namespace reaktio::gameplay
