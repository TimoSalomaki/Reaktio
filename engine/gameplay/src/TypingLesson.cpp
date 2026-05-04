#include "reaktio/gameplay/TypingLesson.hpp"

#include <unordered_set>
#include <utility>

namespace reaktio::gameplay {

namespace {

void push_issue(
    TypingLessonValidationReport& report,
    TypingLessonValidationIssue::Kind kind,
    std::string detail) {
    report.ok = false;
    report.issues.push_back(TypingLessonValidationIssue{
        .kind = kind,
        .detail = std::move(detail),
    });
}

} // namespace

TypingLessonValidationReport validate_typing_lesson(const TypingLesson& lesson) {
    TypingLessonValidationReport report{};
    if (lesson.id.empty()) {
        push_issue(report, TypingLessonValidationIssue::Kind::EmptyLessonId, "lesson.id");
    }

    std::unordered_set<std::string> word_group_ids;
    for (const TypingWordGroup& group : lesson.word_groups) {
        if (group.id.empty()) {
            push_issue(report, TypingLessonValidationIssue::Kind::EmptyWordGroupId, "lesson.word_groups[].id");
            continue;
        }
        if (!word_group_ids.insert(group.id).second) {
            push_issue(report, TypingLessonValidationIssue::Kind::DuplicateWordGroupId, group.id);
        }
    }

    std::unordered_set<std::string> exercise_ids;
    for (const TypingExercise& exercise : lesson.exercises) {
        if (exercise.id.empty()) {
            push_issue(report, TypingLessonValidationIssue::Kind::EmptyExerciseId, "lesson.exercises[].id");
            continue;
        }
        if (!exercise_ids.insert(exercise.id).second) {
            push_issue(report, TypingLessonValidationIssue::Kind::DuplicateExerciseId, exercise.id);
        }
        if (exercise.prompt_text.empty()) {
            push_issue(report, TypingLessonValidationIssue::Kind::EmptyExercisePromptText, exercise.id);
        }
        for (const std::string& reference : exercise.word_group_ids) {
            if (word_group_ids.find(reference) == word_group_ids.end()) {
                push_issue(
                    report,
                    TypingLessonValidationIssue::Kind::UnknownWordGroupReference,
                    exercise.id + " -> " + reference);
            }
        }
    }

    std::unordered_set<std::string> progression_ids;
    for (const TypingProgression& progression : lesson.progressions) {
        if (progression.id.empty()) {
            push_issue(report, TypingLessonValidationIssue::Kind::EmptyProgressionId, "lesson.progressions[].id");
            continue;
        }
        if (!progression_ids.insert(progression.id).second) {
            push_issue(report, TypingLessonValidationIssue::Kind::DuplicateProgressionId, progression.id);
        }
        for (const TypingProgressionStep& step : progression.steps) {
            if (step.exercise_id.empty() ||
                exercise_ids.find(step.exercise_id) == exercise_ids.end()) {
                push_issue(
                    report,
                    TypingLessonValidationIssue::Kind::UnknownExerciseReference,
                    progression.id + " -> " + step.exercise_id);
            }
        }
    }

    return report;
}

TypingLessonStore::AddResult TypingLessonStore::add_lesson(TypingLesson lesson) {
    if (lesson.id.empty()) {
        return AddResult::Rejected;
    }
    if (id_index_.find(lesson.id) != id_index_.end()) {
        return AddResult::DuplicateId;
    }
    const std::size_t inserted_index = lessons_.size();
    id_index_.emplace(lesson.id, inserted_index);
    lessons_.push_back(std::move(lesson));
    return AddResult::Added;
}

const TypingLesson* TypingLessonStore::find(std::string_view id) const noexcept {
    const auto it = id_index_.find(std::string(id));
    if (it == id_index_.end()) {
        return nullptr;
    }
    return &lessons_[it->second];
}

std::span<const TypingLesson> TypingLessonStore::lessons() const noexcept {
    return std::span<const TypingLesson>{lessons_.data(), lessons_.size()};
}

std::size_t TypingLessonStore::lesson_count() const noexcept {
    return lessons_.size();
}

TypingPrompt make_prompt_from_exercise(
    const TypingExercise& exercise,
    const GraphemeBreakOracle& oracle) {
    TypingPromptMetadata metadata{};
    metadata.id = exercise.id;
    metadata.display_name = exercise.display_name;
    metadata.layout_hint = exercise.layout_hint;
    metadata.locale_tag = exercise.locale_tag;
    return make_typing_prompt(std::move(metadata), exercise.prompt_text, oracle);
}

void TypingProgressionTracker::reset(const TypingProgression* progression) noexcept {
    progression_ = progression;
    step_index_ = 0;
    finished_ = (progression == nullptr) || progression->steps.empty();
}

const TypingProgression* TypingProgressionTracker::progression() const noexcept {
    return progression_;
}

std::size_t TypingProgressionTracker::step_index() const noexcept {
    return step_index_;
}

bool TypingProgressionTracker::finished() const noexcept {
    return finished_;
}

const TypingProgressionStep* TypingProgressionTracker::current_step() const noexcept {
    if (finished_ || progression_ == nullptr || step_index_ >= progression_->steps.size()) {
        return nullptr;
    }
    return &progression_->steps[step_index_];
}

TypingProgressionTracker::StepOutcome TypingProgressionTracker::submit_result(
    const TypingExerciseResult& result) {
    const TypingProgressionStep* step = current_step();
    if (step == nullptr) {
        return StepOutcome::NoActiveStep;
    }
    if (step->exercise_id != result.exercise_id) {
        return StepOutcome::StepMismatch;
    }

    const bool accuracy_ok = result.accuracy_ratio >= step->minimum_accuracy_to_advance;
    const bool combo_ok = result.max_combo >= step->minimum_combo_to_advance;
    const bool completion_ok = !step->required || result.prompt_completed;
    if (!(accuracy_ok && combo_ok && completion_ok)) {
        return StepOutcome::Failed;
    }

    ++step_index_;
    if (step_index_ >= progression_->steps.size()) {
        finished_ = true;
        return StepOutcome::Completed;
    }
    return StepOutcome::Advanced;
}

} // namespace reaktio::gameplay
