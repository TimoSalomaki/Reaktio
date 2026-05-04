#include "reaktio/games/typing_slice/TypingSliceMode.hpp"

#include "reaktio/foundation/CrashSafeLog.hpp"
#include "reaktio/gameplay/GameplayInput.hpp"
#include "reaktio/gameplay/IModeHost.hpp"
#include "reaktio/gameplay/ModeFlow.hpp"
#include "reaktio/gameplay/PresentationEvents.hpp"
#include "reaktio/gameplay/ReplayRecorder.hpp"
#include "reaktio/gameplay/SaveData.hpp"
#include "reaktio/platform/FrameClock.hpp"
#include "reaktio/render/RenderCamera.hpp"
#include "reaktio/render/RenderExtraction.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>
namespace reaktio::games::typing_slice {

namespace {

constexpr std::string_view k_mode_id = "mode.typing.slice";
constexpr std::string_view k_mode_display_name = "Typing Slice";
constexpr std::string_view k_mode_description =
    "Vertical-slice typing mode validating text-input judgement, falling-token chart density, and shared-stack scoring.";
constexpr std::string_view k_mode_family = "typing";

const gameplay::ModeDescriptor k_descriptor{
    .id = k_mode_id,
    .display_name = k_mode_display_name,
    .description = k_mode_description,
    .family = k_mode_family,
    .capabilities = gameplay::ModeCapabilities::UsesTextInput |
        gameplay::ModeCapabilities::UsesTransport |
        gameplay::ModeCapabilities::EmitsRenderPackets |
        gameplay::ModeCapabilities::RecordsReplay |
        gameplay::ModeCapabilities::SupportsPractice,
};

rhythm::TempoMap make_default_tempo_map() {
    rhythm::TempoMapDefinition definition{};
    definition.config.ticks_per_quarter_note = 480;
    definition.config.sample_rate_hz = 48000;
    // 120 BPM -> 500000 microseconds per quarter note.
    definition.tempo_changes.push_back(
        rhythm::TempoChange{.start_tick = 0, .microseconds_per_quarter_note = 500000});
    definition.time_signature_changes.push_back(
        rhythm::TimeSignatureChange{.start_tick = 0, .numerator = 4, .denominator = 4});

    rhythm::TempoMap tempo_map;
    tempo_map.rebuild(std::move(definition));
    return tempo_map;
}

gameplay::TypingLesson make_default_lesson() {
    gameplay::TypingLesson lesson{};
    lesson.id = "reaktio.typing.intro";
    lesson.display_name = "Typing Intro";
    lesson.locale_tag = "en-US";
    lesson.word_groups.push_back(gameplay::TypingWordGroup{
        .id = "reaktio.typing.intro.greetings",
        .display_name = "Greetings",
        .layout_hint = "en-US",
        .locale_tag = "en-US",
        .entries = {"hi", "hello", "ready"},
    });
    lesson.exercises.push_back(gameplay::TypingExercise{
        .id = "reaktio.typing.intro.greet",
        .display_name = "Say Hi",
        .prompt_text = "hi reaktio",
        .leniency = gameplay::TypingLeniency{.case_insensitive = true},
        .word_group_ids = {"reaktio.typing.intro.greetings"},
        .target_combo = 6,
        .target_accuracy = 0.6,
        .layout_hint = "en-US",
        .locale_tag = "en-US",
    });
    lesson.progressions.push_back(gameplay::TypingProgression{
        .id = "reaktio.typing.intro.progression",
        .display_name = "Intro",
        .steps = {
            gameplay::TypingProgressionStep{
                .exercise_id = "reaktio.typing.intro.greet",
                .minimum_accuracy_to_advance = 0.6,
                .minimum_combo_to_advance = 6,
                .required = true,
            },
        },
    });
    return lesson;
}

const gameplay::TypingExercise* find_exercise(
    const gameplay::TypingLesson& lesson, std::string_view exercise_id) noexcept {
    for (const gameplay::TypingExercise& exercise : lesson.exercises) {
        if (exercise.id == exercise_id) {
            return &exercise;
        }
    }
    if (!lesson.exercises.empty()) {
        return &lesson.exercises.front();
    }
    return nullptr;
}

rhythm::TimingJudgement classify_typing_match(const gameplay::TypingJudgementResult& result) noexcept {
    switch (result.judgement) {
    case gameplay::TypingJudgement::Match:
        return rhythm::TimingJudgement::Perfect;
    case gameplay::TypingJudgement::MatchLenient:
        return rhythm::TimingJudgement::Great;
    case gameplay::TypingJudgement::Skipped:
    case gameplay::TypingJudgement::Mismatch:
        return rhythm::TimingJudgement::Miss;
    case gameplay::TypingJudgement::IgnoredWhitespace:
    case gameplay::TypingJudgement::PromptComplete:
    case gameplay::TypingJudgement::None:
    default:
        return rhythm::TimingJudgement::None;
    }
}

} // namespace

const gameplay::ModeDescriptor& TypingSliceMode::mode_descriptor() noexcept {
    return k_descriptor;
}

TypingSliceMode::TypingSliceMode() : TypingSliceMode(TypingSliceConfig{}) {}

TypingSliceMode::TypingSliceMode(TypingSliceConfig config)
    : config_(std::move(config)),
      timing_tracker_(gameplay::TypingTimingTrackerOptions{
          .capacity = 256,
          .histogram_bucket_count = 8,
          .histogram_bucket_microseconds = 50000,
      }) {
    if (config_.chart_config.lane_count == 0) {
        config_.chart_config.lane_count = 4;
    }
    if (config_.chart_config.ticks_per_grapheme <= 0) {
        config_.chart_config.ticks_per_grapheme = 240;
    }
    if (config_.chart_config.travel_lead_ticks <= 0) {
        config_.chart_config.travel_lead_ticks = 960;
    }
    if (config_.chart_config.first_judge_tick <= 0) {
        config_.chart_config.first_judge_tick = 960;
    }
}

const gameplay::ModeDescriptor& TypingSliceMode::descriptor() const noexcept {
    return k_descriptor;
}

void TypingSliceMode::ensure_initialized(gameplay::IModeHost& host) {
    // host is reserved for future first-time initialization that needs host
    // services (resource registry, configuration, modifiers); it is not
    // required by the current data-driven setup.
    (void)host;
    if (initialized_) {
        return;
    }

    default_lesson_ = make_default_lesson();
    const gameplay::TypingExercise* exercise = find_exercise(default_lesson_, config_.exercise_id);
    if (exercise == nullptr) {
        // Defensive fallback so the mode never deadlocks on broken authoring data.
        active_exercise_ = gameplay::TypingExercise{
            .id = "reaktio.typing.fallback",
            .display_name = "Fallback",
            .prompt_text = "reaktio",
            .leniency = gameplay::TypingLeniency{.case_insensitive = true},
        };
    } else {
        active_exercise_ = *exercise;
    }

    prompt_ = gameplay::make_prompt_from_exercise(active_exercise_);
    chart_ = gameplay::make_typing_chart(prompt_, config_.chart_config);
    scheduled_cues_ = gameplay::make_scheduler_cues_from_typing_chart(chart_);

    cursor_.reset(prompt_);
    analytics_.reset();
    error_patterns_.reset();
    timing_tracker_.reset();
    scoring_.reset();
    cue_scheduler_.reset();

    tempo_map_ = make_default_tempo_map();
    timing_windows_ = rhythm::make_default_timing_window_set();

    initialized_ = true;
}

void TypingSliceMode::on_enter(gameplay::IModeHost& host, const gameplay::ModeEnterContext& context) {
    ensure_initialized(host);
    host.flow().begin(gameplay::ModeFlowReason::EnterMode);

    {
        gameplay::SaveSetting setting{};
        setting.key = "last_played_mode";
        setting.value.kind = gameplay::SaveSettingValueKind::Text;
        setting.value.text_value = std::string(descriptor().id);
        host.save_data().upsert_setting(
            "profile", std::move(setting), host.frame_timing().wall_clock_ns);
        (void)host.save_data().grant_unlock(
            "reaktio.typing.first_run",
            std::string(descriptor().id),
            host.frame_timing().wall_clock_ns);
    }

    ++lifecycle_event_count_;
    (void)context;
}

void TypingSliceMode::on_fixed_step(
    gameplay::IModeHost& host, const gameplay::ModeFixedStepContext& context) {
    ensure_initialized(host);

    // Drive the cue scheduler so falling-token presentation and despawn
    // events run on the same shared engine layer used by every other mode.
    if (tempo_map_.valid()) {
        gameplay::CueSchedulerUpdateInput input{};
        input.tempo_map = &tempo_map_;
        const gameplay::TransportSnapshot snapshot = host.transport().snapshot();
        input.transport = &snapshot;
        input.schedule = std::span<const rhythm::ScheduledCue>(scheduled_cues_);
        input.rules = gameplay::CueSchedulerRules{};
        cue_scheduler_.update(input);
    }

    for (const gameplay::TextInputEvent& event : host.input().text().text_events()) {
        process_text_event(host, event.text, context);
    }
}

void TypingSliceMode::process_text_event(
    gameplay::IModeHost& host,
    std::string_view utf8_text,
    const gameplay::ModeFixedStepContext& context) {
    if (utf8_text.empty()) {
        return;
    }

    const gameplay::TypingLeniency& leniency = active_exercise_.leniency;
    const std::vector<gameplay::TypingGrapheme> incoming =
        gameplay::decode_typing_graphemes(utf8_text, {});
    for (const gameplay::TypingGrapheme& grapheme : incoming) {
        const std::size_t prompt_index = cursor_.cursor();
        const gameplay::TypingChartCue* chart_cue =
            gameplay::find_cue_by_prompt_index(chart_, prompt_index);

        const gameplay::TypingJudgementResult result =
            cursor_.accept_grapheme(grapheme, leniency);
        analytics_.record(result);
        error_patterns_.record(result);

        const rhythm::TimingJudgement classified = classify_typing_match(result);
        if (classified == rhythm::TimingJudgement::None) {
            continue;
        }

        const gameplay::TransportSnapshot snapshot = host.transport().snapshot();
        const rhythm::TimelineMicroseconds input_time_microseconds =
            static_cast<rhythm::TimelineMicroseconds>(snapshot.position_seconds * 1'000'000.0) +
            config_.song_offset_microseconds;

        rhythm::TimingJudgementResult timing_result{};
        if (chart_cue != nullptr && tempo_map_.valid()) {
            timing_result = rhythm::evaluate_timing_judgement(
                tempo_map_, timing_windows_, chart_cue->judge_tick,
                input_time_microseconds, rhythm::TimingOffsetProfile{});
            // Override the timing-derived bucket with the typing-derived
            // classification so a typing miss never gets accidentally upgraded
            // to a Perfect just because it landed in the perfect window.
            timing_result.judgement = classified;
            timing_result.scoreable_hit = (classified != rhythm::TimingJudgement::Miss);
            timing_result.advances_combo = (classified != rhythm::TimingJudgement::Miss);
        } else {
            timing_result.judgement = classified;
            timing_result.input_time_microseconds = input_time_microseconds;
            timing_result.scoreable_hit = (classified != rhythm::TimingJudgement::Miss);
            timing_result.advances_combo = (classified != rhythm::TimingJudgement::Miss);
        }

        gameplay::ScoreJudgementEvent score_event{};
        score_event.cue_id = static_cast<std::uint64_t>(prompt_index + 1);
        score_event.schedule_index = prompt_index;
        score_event.cue_hit_tick = chart_cue != nullptr ? chart_cue->judge_tick : 0;
        score_event.channel_index = chart_cue != nullptr ? chart_cue->lane_index : 0;
        score_event.judgement = timing_result;
        const gameplay::ScoreJudgementResult score_result = scoring_.record_judgement(score_event);

        timing_tracker_.record(gameplay::TypingTimingSample{
            .simulation_step = context.fixed_step_index,
            .frame_index = context.frame_index,
            .latency_microseconds = std::abs(timing_result.corrected_error_microseconds),
            .judgement = result.judgement,
            .advanced_cursor = result.advanced_cursor,
        });

        // UI feedback hooks: a screen pulse on miss/lenient match. Modes
        // route through the shared PresentationEventBus so platform/render
        // never gets called directly.
        if (result.judgement == gameplay::TypingJudgement::Mismatch &&
            config_.emit_screen_effect_on_miss) {
            const bool emitted = host.presentation_events().publish(
                gameplay::ScreenEffectEvent{
                    .kind = gameplay::ScreenEffectKind::Flash,
                    .id = "typing.miss",
                    .duration_seconds = 0.10,
                    .intensity = 0.6,
                    .color = {1.0f, 0.2f, 0.2f, 1.0f},
                    .simulation_step = context.fixed_step_index,
                    .frame_index = context.frame_index,
                });
            if (emitted) {
                ++presentation_events_emitted_;
            }
        } else if (result.judgement == gameplay::TypingJudgement::MatchLenient &&
                   config_.emit_screen_effect_on_lenient_match) {
            const bool emitted = host.presentation_events().publish(
                gameplay::ScreenEffectEvent{
                    .kind = gameplay::ScreenEffectKind::ColorPulse,
                    .id = "typing.lenient_hint",
                    .duration_seconds = 0.05,
                    .intensity = 0.3,
                    .color = {1.0f, 0.85f, 0.3f, 1.0f},
                    .simulation_step = context.fixed_step_index,
                    .frame_index = context.frame_index,
                });
            if (emitted) {
                ++presentation_events_emitted_;
            }
        }

        // Best-effort replay sample so the typing-slice mode shows up in the
        // existing replay inspection view alongside any scored slice.
        // Best-effort replay sample so the typing-slice mode shows up in the
        // existing replay inspection view alongside any scored slice. Skipped
        // when running inside the smoke verifier, which has its own replay
        // session that should not be contaminated by an out-of-band run.
        if (config_.record_replay_samples) {
            host.replay().record_judgement_sample(gameplay::ReplayJudgementSample{
                .frame_index = context.frame_index,
                .simulation_step = context.fixed_step_index,
                .cue_id = score_event.cue_id,
                .schedule_index = prompt_index,
                .cue_hit_tick = score_event.cue_hit_tick,
                .channel_index = score_event.channel_index,
                .judgement = timing_result.judgement,
                .scoreable_hit = timing_result.scoreable_hit,
                .advances_combo = timing_result.advances_combo,
                .raw_error_microseconds = timing_result.raw_error_microseconds,
                .corrected_error_microseconds = timing_result.corrected_error_microseconds,
                .applied_offset_microseconds = timing_result.applied_offset_microseconds,
                .score_after = scoring_.summary().score,
                .combo_after = score_result.combo_after,
                .multiplier_after = score_result.multiplier_after,
                .health_after = score_result.health_after,
                .run_state = score_result.run_state,
            });
        }

        if (cursor_.finished()) {
            analytics_.record_prompt_completion();
            scoring_.mark_cleared();
            if (host.flow().can_succeed()) {
                host.flow().succeed(gameplay::ModeFlowReason::SongEnded);
            }
            break;
        }
    }
}

void TypingSliceMode::on_render_extract(
    gameplay::IModeHost& host, const gameplay::ModeRenderContext& context) {
    ensure_initialized(host);

    host.render_extraction().set_view_camera(
        reaktio::render::RenderView::MainScene,
        reaktio::render::OrthographicCamera2D{
            .center = {0.0f, 0.0f},
            .virtual_height = 720.0f,
            .view_distance = 10.0f,
            .near_plane = 0.0f,
            .far_plane = 100.0f,
        });

    std::ostringstream prompt_line;
    prompt_line << "Typing slice: " << active_exercise_.display_name
                << " cursor=" << cursor_.cursor() << "/" << prompt_.graphemes.size()
                << " hits=" << cursor_.hit_count()
                << " misses=" << cursor_.miss_count()
                << " combo=" << cursor_.combo();
    host.render_extraction().add_debug_text(0, 8, 0x0a, prompt_line.str());

    std::ostringstream cue_line;
    cue_line << "active-cues=" << cue_scheduler_.summary().active_cue_count
             << " scheduled=" << cue_scheduler_.summary().scheduled_cue_count
             << " lanes=" << chart_.config.lane_count;
    host.render_extraction().add_debug_text(0, 9, 0x0e, cue_line.str());

    (void)context;
}

void TypingSliceMode::on_exit(gameplay::IModeHost& host, const gameplay::ModeExitContext& context) {
    if (!initialized_) {
        return;
    }

    const gameplay::ScoreSummary summary = scoring_.summary();
    if (host.flow().can_present_results()) {
        host.flow().present_results(summary, "mode.typing.slice.exit");
    }

    gameplay::SaveModeStatsResult result{};
    result.mode_id = std::string(descriptor().id);
    result.song_id = config_.song_id;
    result.cleared = summary.run_state == gameplay::ScoreRunState::Cleared;
    result.failed = summary.run_state == gameplay::ScoreRunState::Failed;
    result.score = summary.score;
    result.combo = summary.max_combo;
    result.accuracy_ratio = summary.accuracy_ratio;
    result.grade = std::string(gameplay::to_string(summary.grade));
    result.play_time_ms = static_cast<std::uint64_t>(
        host.transport().snapshot().position_seconds > 0.0
            ? host.transport().snapshot().position_seconds * 1000.0
            : 0.0);
    result.wall_clock_ns = host.frame_timing().wall_clock_ns;
    host.save_data().record_mode_session(result);

    ++lifecycle_event_count_;
    (void)context;
}

const gameplay::TypingPrompt& TypingSliceMode::prompt() const noexcept {
    return prompt_;
}

const gameplay::TypingChart& TypingSliceMode::chart() const noexcept {
    return chart_;
}

const gameplay::TypingCursor& TypingSliceMode::cursor() const noexcept {
    return cursor_;
}

const gameplay::TypingAnalytics& TypingSliceMode::analytics() const noexcept {
    return analytics_;
}

const gameplay::TypingErrorPatternTracker& TypingSliceMode::error_patterns() const noexcept {
    return error_patterns_;
}

const gameplay::TypingTimingTracker& TypingSliceMode::timing_tracker() const noexcept {
    return timing_tracker_;
}

const gameplay::ScoreTracker& TypingSliceMode::scoring() const noexcept {
    return scoring_;
}

const gameplay::CueScheduler& TypingSliceMode::cue_scheduler() const noexcept {
    return cue_scheduler_;
}

const rhythm::TempoMap& TypingSliceMode::tempo_map() const noexcept {
    return tempo_map_;
}

std::uint32_t TypingSliceMode::presentation_events_emitted() const noexcept {
    return presentation_events_emitted_;
}

std::uint32_t TypingSliceMode::lifecycle_event_count() const noexcept {
    return lifecycle_event_count_;
}

} // namespace reaktio::games::typing_slice
