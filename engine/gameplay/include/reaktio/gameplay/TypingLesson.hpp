#pragma once

#include "reaktio/gameplay/TypingPrompt.hpp"
#include "reaktio/rhythm/TempoMap.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reaktio::gameplay {

// Pure-data lesson schema. Sits in the gameplay layer so any typing-family
// mode (training, dialogue, lyric, language) can reuse it. A cooker can be
// dropped in later without changing this contract.

struct TypingWordGroup {
    std::string id;
    std::string display_name;
    std::string layout_hint;
    std::string locale_tag;
    std::vector<std::string> entries;
};

struct TypingExercise {
    std::string id;
    std::string display_name;
    std::string prompt_text;                  // raw UTF-8; resolves to graphemes at runtime.
    TypingLeniency leniency;
    std::vector<std::string> word_group_ids;  // optional references into word_groups.
    rhythm::TimelineMicroseconds suggested_duration_microseconds{};
    std::uint32_t target_combo{};
    double target_accuracy{};
    std::string layout_hint;
    std::string locale_tag;
};

struct TypingProgressionStep {
    std::string exercise_id;
    double minimum_accuracy_to_advance{};
    std::uint32_t minimum_combo_to_advance{};
    bool required{true};
};

struct TypingProgression {
    std::string id;
    std::string display_name;
    std::vector<TypingProgressionStep> steps;
};

struct TypingLesson {
    std::string id;
    std::string display_name;
    std::string locale_tag;
    std::vector<TypingWordGroup> word_groups;
    std::vector<TypingExercise> exercises;
    std::vector<TypingProgression> progressions;
};

struct TypingLessonValidationIssue {
    enum class Kind : std::uint8_t {
        EmptyLessonId,
        DuplicateExerciseId,
        DuplicateWordGroupId,
        DuplicateProgressionId,
        UnknownExerciseReference,
        UnknownWordGroupReference,
        EmptyExercisePromptText,
        EmptyExerciseId,
        EmptyWordGroupId,
        EmptyProgressionId,
    };
    Kind kind{Kind::EmptyLessonId};
    std::string detail;
};

struct TypingLessonValidationReport {
    bool ok{true};
    std::vector<TypingLessonValidationIssue> issues;
};

[[nodiscard]] TypingLessonValidationReport validate_typing_lesson(const TypingLesson& lesson);

class TypingLessonStore {
  public:
    enum class AddResult : std::uint8_t {
        Added,
        DuplicateId,
        Rejected,
    };

    AddResult add_lesson(TypingLesson lesson);

    [[nodiscard]] const TypingLesson* find(std::string_view id) const noexcept;
    [[nodiscard]] std::span<const TypingLesson> lessons() const noexcept;
    [[nodiscard]] std::size_t lesson_count() const noexcept;

  private:
    std::vector<TypingLesson> lessons_;
    std::unordered_map<std::string, std::size_t> id_index_;
};

// Resolves a prompt for an exercise. Modes can inject a different
// GraphemeBreakOracle without touching the lesson data.
[[nodiscard]] TypingPrompt make_prompt_from_exercise(
    const TypingExercise& exercise,
    const GraphemeBreakOracle& oracle = {});

struct TypingExerciseResult {
    std::string exercise_id;
    std::uint32_t hit_count{};
    std::uint32_t miss_count{};
    std::uint32_t skip_count{};
    std::uint32_t lenient_match_count{};
    std::uint32_t max_combo{};
    double accuracy_ratio{};
    bool prompt_completed{};
};

// Lightweight progression tracker that decides whether a step is satisfied
// based on its target thresholds. Owns no analytics — callers feed results in.
class TypingProgressionTracker {
  public:
    void reset(const TypingProgression* progression) noexcept;

    [[nodiscard]] const TypingProgression* progression() const noexcept;
    [[nodiscard]] std::size_t step_index() const noexcept;
    [[nodiscard]] bool finished() const noexcept;
    [[nodiscard]] const TypingProgressionStep* current_step() const noexcept;

    enum class StepOutcome : std::uint8_t {
        NoActiveStep,
        StepMismatch,
        Failed,
        Advanced,
        Completed,
    };

    StepOutcome submit_result(const TypingExerciseResult& result);

  private:
    const TypingProgression* progression_{};
    std::size_t step_index_{};
    bool finished_{false};
};

} // namespace reaktio::gameplay
