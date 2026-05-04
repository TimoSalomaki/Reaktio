#include "reaktio/gameplay/Scoring.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace reaktio::gameplay {

namespace {

JudgementScoreRule make_rule(
    rhythm::TimingJudgement judgement,
    std::uint32_t base_points,
    double accuracy_weight,
    double health_delta,
    bool scoreable_hit,
    bool advances_combo) noexcept {
    return JudgementScoreRule{
        .judgement = judgement,
        .base_points = base_points,
        .accuracy_weight = accuracy_weight,
        .health_delta = health_delta,
        .scoreable_hit = scoreable_hit,
        .advances_combo = advances_combo,
    };
}

double clamp_health(double health, const ScoringRules& rules) noexcept {
    return std::clamp(health, rules.min_health, rules.max_health);
}

double multiplier_for_combo(std::uint32_t combo, const ScoringRules& rules) noexcept {
    if (rules.combo_per_multiplier_step == 0u) {
        return 1.0;
    }

    const std::uint32_t steps = combo / rules.combo_per_multiplier_step;
    return std::min(1.0 + static_cast<double>(steps) * rules.multiplier_step, rules.max_multiplier);
}

ScoreGrade grade_for_accuracy(double accuracy_ratio, std::uint32_t judgement_count, ScoreRunState run_state) noexcept {
    if (judgement_count == 0u) {
        return ScoreGrade::Unranked;
    }
    if (run_state == ScoreRunState::Failed) {
        return ScoreGrade::F;
    }
    if (accuracy_ratio >= 0.995) {
        return ScoreGrade::SS;
    }
    if (accuracy_ratio >= 0.950) {
        return ScoreGrade::S;
    }
    if (accuracy_ratio >= 0.900) {
        return ScoreGrade::A;
    }
    if (accuracy_ratio >= 0.800) {
        return ScoreGrade::B;
    }
    if (accuracy_ratio >= 0.700) {
        return ScoreGrade::C;
    }
    if (accuracy_ratio >= 0.600) {
        return ScoreGrade::D;
    }
    return ScoreGrade::F;
}

std::size_t section_index_for_tick(rhythm::ChartTick cue_hit_tick, const ScoringRules& rules) noexcept {
    if (rules.section_length_ticks <= 0 || cue_hit_tick <= 0) {
        return 0;
    }

    return static_cast<std::size_t>(cue_hit_tick / rules.section_length_ticks);
}

} // namespace

ScoringRules make_default_scoring_rules() {
    ScoringRules rules{};
    rules.judgement_rules[to_index(rhythm::TimingJudgement::None)] =
        make_rule(rhythm::TimingJudgement::None, 0, 0.0, 0.0, false, false);
    rules.judgement_rules[to_index(rhythm::TimingJudgement::Miss)] =
        make_rule(rhythm::TimingJudgement::Miss, 0, 0.0, -0.08, false, false);
    rules.judgement_rules[to_index(rhythm::TimingJudgement::Good)] =
        make_rule(rhythm::TimingJudgement::Good, 500, 0.65, 0.01, true, true);
    rules.judgement_rules[to_index(rhythm::TimingJudgement::Great)] =
        make_rule(rhythm::TimingJudgement::Great, 800, 0.85, 0.025, true, true);
    rules.judgement_rules[to_index(rhythm::TimingJudgement::Perfect)] =
        make_rule(rhythm::TimingJudgement::Perfect, 1000, 1.0, 0.04, true, true);
    return rules;
}

ScoreTracker::ScoreTracker() {
    reset();
}

void ScoreTracker::reset(ScoringRules rules) {
    rules_ = std::move(rules);
    summary_ = ScoreSummary{
        .multiplier = 1.0,
        .health = clamp_health(rules_.starting_health, rules_),
        .run_state = ScoreRunState::Active,
    };
    last_result_ = {};
    has_last_result_ = false;
    sections_.clear();
}

ScoreJudgementResult ScoreTracker::record_judgement(const ScoreJudgementEvent& event) {
    const JudgementScoreRule& rule = rule_for(event.judgement.judgement);
    const bool scoreable_hit = rule.scoreable_hit && event.judgement.scoreable_hit;
    const bool advances_combo = rule.advances_combo && event.judgement.advances_combo;
    const double multiplier_before = summary_.multiplier;
    const std::uint32_t combo_before = summary_.current_combo;
    const std::uint64_t awarded_points = scoreable_hit
        ? static_cast<std::uint64_t>(std::llround(static_cast<double>(rule.base_points) * multiplier_before))
        : 0u;

    ++summary_.judgement_count;
    ++summary_.judgement_counts[to_index(event.judgement.judgement)];
    summary_.score += awarded_points;
    summary_.accuracy_points += rule.accuracy_weight;
    summary_.max_accuracy_points += 1.0;
    if (scoreable_hit) {
        ++summary_.scoreable_hit_count;
    }
    if (!advances_combo) {
        ++summary_.miss_count;
        summary_.current_combo = 0;
    } else {
        ++summary_.current_combo;
        summary_.max_combo = std::max(summary_.max_combo, summary_.current_combo);
    }
    summary_.multiplier = multiplier_for_combo(summary_.current_combo, rules_);
    summary_.health = clamp_health(summary_.health + rule.health_delta, rules_);
    if (rules_.fail_on_empty_health && summary_.health <= rules_.min_health) {
        summary_.run_state = ScoreRunState::Failed;
    }

    ScoreSectionStats& section = section_for_tick(event.cue_hit_tick);
    ++section.judgement_count;
    ++section.judgement_counts[to_index(event.judgement.judgement)];
    section.score += awarded_points;
    section.accuracy_points += rule.accuracy_weight;
    section.max_accuracy_points += 1.0;
    if (scoreable_hit) {
        ++section.scoreable_hit_count;
    }
    if (!advances_combo) {
        ++section.miss_count;
    }
    section.max_combo = std::max(section.max_combo, summary_.current_combo);

    refresh_summary_grade();

    last_result_ = ScoreJudgementResult{
        .event = event,
        .awarded_points = awarded_points,
        .combo_before = combo_before,
        .combo_after = summary_.current_combo,
        .multiplier_before = multiplier_before,
        .multiplier_after = summary_.multiplier,
        .health_after = summary_.health,
        .run_state = summary_.run_state,
    };
    has_last_result_ = true;
    return last_result_;
}

void ScoreTracker::mark_cleared() noexcept {
    if (summary_.run_state == ScoreRunState::Active) {
        summary_.run_state = ScoreRunState::Cleared;
    }
    refresh_summary_grade();
}

const ScoringRules& ScoreTracker::rules() const noexcept {
    return rules_;
}

const ScoreSummary& ScoreTracker::summary() const noexcept {
    return summary_;
}

const ScoreJudgementResult* ScoreTracker::last_result() const noexcept {
    return has_last_result_ ? &last_result_ : nullptr;
}

std::span<const ScoreSectionStats> ScoreTracker::sections() const noexcept {
    return std::span<const ScoreSectionStats>{sections_.data(), sections_.size()};
}

const JudgementScoreRule& ScoreTracker::rule_for(rhythm::TimingJudgement judgement) const noexcept {
    return rules_.judgement_rules[to_index(judgement)];
}

ScoreSectionStats& ScoreTracker::section_for_tick(rhythm::ChartTick cue_hit_tick) {
    const std::size_t section_index = section_index_for_tick(cue_hit_tick, rules_);
    const auto existing = std::find_if(sections_.begin(), sections_.end(), [section_index](const ScoreSectionStats& section) {
        return section.section_index == section_index;
    });
    if (existing != sections_.end()) {
        return *existing;
    }

    const rhythm::ChartTick section_length = rules_.section_length_ticks > 0 ? rules_.section_length_ticks : 0;
    sections_.push_back(ScoreSectionStats{
        .section_index = section_index,
        .start_tick = static_cast<rhythm::ChartTick>(section_index) * section_length,
        .end_tick = static_cast<rhythm::ChartTick>(section_index + 1u) * section_length,
    });
    summary_.section_count = sections_.size();
    return sections_.back();
}

void ScoreTracker::refresh_summary_grade() noexcept {
    summary_.accuracy_ratio = summary_.max_accuracy_points > 0.0
        ? summary_.accuracy_points / summary_.max_accuracy_points
        : 0.0;
    summary_.grade = grade_for_accuracy(summary_.accuracy_ratio, summary_.judgement_count, summary_.run_state);
}

} // namespace reaktio::gameplay