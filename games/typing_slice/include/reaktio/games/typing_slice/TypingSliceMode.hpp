#pragma once

#include "reaktio/gameplay/CueScheduler.hpp"
#include "reaktio/gameplay/IGameMode.hpp"
#include "reaktio/gameplay/Scoring.hpp"
#include "reaktio/gameplay/TypingAnalyticsExtensions.hpp"
#include "reaktio/gameplay/TypingChart.hpp"
#include "reaktio/gameplay/TypingLesson.hpp"
#include "reaktio/gameplay/TypingPrompt.hpp"
#include "reaktio/rhythm/TempoMap.hpp"
#include "reaktio/rhythm/TimingJudgement.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace reaktio::games::typing_slice {

struct TypingSliceConfig {
    std::string lesson_id{"reaktio.typing.intro"};
    std::string exercise_id{"reaktio.typing.intro.greet"};
    std::string song_id{"reaktio.typing.intro.greet"};
    gameplay::TypingChartConfig chart_config{};
    rhythm::TimelineMicroseconds song_offset_microseconds{0};
    bool emit_screen_effect_on_miss{true};
    bool emit_screen_effect_on_lenient_match{true};
    bool record_replay_samples{true};
};

class TypingSliceMode final : public gameplay::IGameMode {
  public:
    TypingSliceMode();
    explicit TypingSliceMode(TypingSliceConfig config);

    [[nodiscard]] static const gameplay::ModeDescriptor& mode_descriptor() noexcept;

    [[nodiscard]] const gameplay::ModeDescriptor& descriptor() const noexcept override;
    void on_enter(gameplay::IModeHost& host, const gameplay::ModeEnterContext& context) override;
    void on_fixed_step(gameplay::IModeHost& host, const gameplay::ModeFixedStepContext& context) override;
    void on_render_extract(gameplay::IModeHost& host, const gameplay::ModeRenderContext& context) override;
    void on_exit(gameplay::IModeHost& host, const gameplay::ModeExitContext& context) override;

    // Observation surfaces (used by the smoke shutdown verifier and any
    // future inspector). Pure read-only views into engine-layer state.
    [[nodiscard]] const gameplay::TypingPrompt& prompt() const noexcept;
    [[nodiscard]] const gameplay::TypingChart& chart() const noexcept;
    [[nodiscard]] const gameplay::TypingCursor& cursor() const noexcept;
    [[nodiscard]] const gameplay::TypingAnalytics& analytics() const noexcept;
    [[nodiscard]] const gameplay::TypingErrorPatternTracker& error_patterns() const noexcept;
    [[nodiscard]] const gameplay::TypingTimingTracker& timing_tracker() const noexcept;
    [[nodiscard]] const gameplay::ScoreTracker& scoring() const noexcept;
    [[nodiscard]] const gameplay::CueScheduler& cue_scheduler() const noexcept;
    [[nodiscard]] const rhythm::TempoMap& tempo_map() const noexcept;
    [[nodiscard]] std::uint32_t presentation_events_emitted() const noexcept;
    [[nodiscard]] std::uint32_t lifecycle_event_count() const noexcept;

  private:
    void ensure_initialized(gameplay::IModeHost& host);
    void process_text_event(
        gameplay::IModeHost& host,
        std::string_view utf8_text,
        const gameplay::ModeFixedStepContext& context);

    TypingSliceConfig config_;
    gameplay::TypingLesson default_lesson_{};
    gameplay::TypingExercise active_exercise_{};
    gameplay::TypingPrompt prompt_{};
    gameplay::TypingChart chart_{};
    gameplay::TypingCursor cursor_{};
    gameplay::TypingAnalytics analytics_{};
    gameplay::TypingErrorPatternTracker error_patterns_{};
    gameplay::TypingTimingTracker timing_tracker_{};
    gameplay::ScoreTracker scoring_{};
    gameplay::CueScheduler cue_scheduler_{};
    rhythm::TempoMap tempo_map_{};
    rhythm::TimingWindowSet timing_windows_{};
    std::vector<rhythm::ScheduledCue> scheduled_cues_{};
    std::uint32_t lifecycle_event_count_{};
    std::uint32_t presentation_events_emitted_{};
    bool initialized_{false};
};

} // namespace reaktio::games::typing_slice
