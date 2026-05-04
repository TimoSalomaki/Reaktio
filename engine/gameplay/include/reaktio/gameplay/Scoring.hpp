#pragma once

#include "reaktio/rhythm/CueTravelModel.hpp"
#include "reaktio/rhythm/TimingJudgement.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace reaktio::gameplay {

inline constexpr std::size_t k_timing_judgement_count = 5;

enum class ScoreGrade : std::uint8_t {
    Unranked,
    F,
    D,
    C,
    B,
    A,
    S,
    SS,
};

enum class ScoreRunState : std::uint8_t {
    Active,
    Failed,
    Cleared,
};

struct JudgementScoreRule {
    rhythm::TimingJudgement judgement{rhythm::TimingJudgement::None};
    std::uint32_t base_points{};
    double accuracy_weight{};
    double health_delta{};
    bool scoreable_hit{};
    bool advances_combo{};
};

struct ScoringRules {
    std::array<JudgementScoreRule, k_timing_judgement_count> judgement_rules{};
    std::uint32_t combo_per_multiplier_step{10};
    double multiplier_step{0.25};
    double max_multiplier{4.0};
    double starting_health{1.0};
    double min_health{};
    double max_health{1.0};
    bool fail_on_empty_health{true};
    rhythm::ChartTick section_length_ticks{1920};
};

struct ScoreJudgementEvent {
    std::uint64_t cue_id{};
    std::size_t schedule_index{};
    rhythm::ChartTick cue_hit_tick{};
    std::uint32_t channel_index{};
    rhythm::TimingJudgementResult judgement{};
};

struct ScoreJudgementResult {
    ScoreJudgementEvent event{};
    std::uint64_t awarded_points{};
    std::uint32_t combo_before{};
    std::uint32_t combo_after{};
    double multiplier_before{1.0};
    double multiplier_after{1.0};
    double health_after{1.0};
    ScoreRunState run_state{ScoreRunState::Active};
};

struct ScoreSectionStats {
    std::size_t section_index{};
    rhythm::ChartTick start_tick{};
    rhythm::ChartTick end_tick{};
    std::array<std::uint32_t, k_timing_judgement_count> judgement_counts{};
    std::uint32_t judgement_count{};
    std::uint32_t scoreable_hit_count{};
    std::uint32_t miss_count{};
    std::uint32_t max_combo{};
    std::uint64_t score{};
    double accuracy_points{};
    double max_accuracy_points{};
};

struct ScoreSummary {
    std::array<std::uint32_t, k_timing_judgement_count> judgement_counts{};
    std::uint32_t judgement_count{};
    std::uint32_t scoreable_hit_count{};
    std::uint32_t miss_count{};
    std::uint32_t current_combo{};
    std::uint32_t max_combo{};
    std::uint64_t score{};
    double multiplier{1.0};
    double health{1.0};
    double accuracy_points{};
    double max_accuracy_points{};
    double accuracy_ratio{};
    ScoreGrade grade{ScoreGrade::Unranked};
    ScoreRunState run_state{ScoreRunState::Active};
    std::size_t section_count{};
};

[[nodiscard]] ScoringRules make_default_scoring_rules();
[[nodiscard]] constexpr std::size_t to_index(rhythm::TimingJudgement judgement) noexcept {
    return static_cast<std::size_t>(judgement);
}
[[nodiscard]] constexpr std::string_view to_string(ScoreGrade grade) noexcept {
    switch (grade) {
    case ScoreGrade::Unranked:
        return "unranked";
    case ScoreGrade::F:
        return "f";
    case ScoreGrade::D:
        return "d";
    case ScoreGrade::C:
        return "c";
    case ScoreGrade::B:
        return "b";
    case ScoreGrade::A:
        return "a";
    case ScoreGrade::S:
        return "s";
    case ScoreGrade::SS:
        return "ss";
    }

    return "unknown";
}
[[nodiscard]] constexpr std::string_view to_string(ScoreRunState state) noexcept {
    switch (state) {
    case ScoreRunState::Active:
        return "active";
    case ScoreRunState::Failed:
        return "failed";
    case ScoreRunState::Cleared:
        return "cleared";
    }

    return "unknown";
}

class ScoreTracker {
  public:
    ScoreTracker();

    void reset(ScoringRules rules = make_default_scoring_rules());
    ScoreJudgementResult record_judgement(const ScoreJudgementEvent& event);
    void mark_cleared() noexcept;

    [[nodiscard]] const ScoringRules& rules() const noexcept;
    [[nodiscard]] const ScoreSummary& summary() const noexcept;
    [[nodiscard]] const ScoreJudgementResult* last_result() const noexcept;
    [[nodiscard]] std::span<const ScoreSectionStats> sections() const noexcept;

  private:
    [[nodiscard]] const JudgementScoreRule& rule_for(rhythm::TimingJudgement judgement) const noexcept;
    [[nodiscard]] ScoreSectionStats& section_for_tick(rhythm::ChartTick cue_hit_tick);
    void refresh_summary_grade() noexcept;

    ScoringRules rules_{};
    ScoreSummary summary_{};
    ScoreJudgementResult last_result_{};
    bool has_last_result_{};
    std::vector<ScoreSectionStats> sections_;
};

} // namespace reaktio::gameplay