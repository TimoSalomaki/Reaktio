#pragma once

#include "reaktio/gameplay/Scoring.hpp"
#include "reaktio/rhythm/TimingJudgement.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reaktio::gameplay {

namespace modifier_ids {

inline constexpr std::string_view k_speed_multiplier = "speed_multiplier";
inline constexpr std::string_view k_autoplay = "autoplay";
inline constexpr std::string_view k_no_fail = "no_fail";
inline constexpr std::string_view k_mirror_channels = "mirror_channels";
inline constexpr std::string_view k_practice_assist = "practice_assist";

} // namespace modifier_ids

enum class ModifierKind : std::uint8_t {
    Custom,
    SpeedMultiplier,
    Autoplay,
    NoFail,
    MirrorChannels,
    PracticeAssist,
};

struct ModifierEntry {
    std::string id;
    ModifierKind kind{ModifierKind::Custom};
    double numeric_parameter{1.0};
    std::int64_t integer_parameter{};
    bool boolean_parameter{};
    bool enabled{true};
};

struct ModifierLimits {
    double min_speed_multiplier{0.5};
    double max_speed_multiplier{2.5};
    std::int64_t max_mirror_channel_count{16};
};

class ModifierSet {
  public:
    void clear() noexcept;
    void set(ModifierEntry entry);
    void set_enabled(std::string_view id, bool enabled) noexcept;
    void remove(std::string_view id) noexcept;

    [[nodiscard]] const ModifierEntry* find(std::string_view id) const noexcept;
    [[nodiscard]] bool is_active(std::string_view id) const noexcept;
    [[nodiscard]] std::span<const ModifierEntry> entries() const noexcept;

    [[nodiscard]] double speed_multiplier(double fallback = 1.0) const noexcept;
    [[nodiscard]] bool autoplay_enabled() const noexcept;
    [[nodiscard]] bool no_fail_enabled() const noexcept;
    [[nodiscard]] std::uint32_t mirror_channel_count() const noexcept;
    [[nodiscard]] bool practice_assist_enabled() const noexcept;

    [[nodiscard]] const ModifierLimits& limits() const noexcept;
    void set_limits(ModifierLimits limits) noexcept;

    [[nodiscard]] std::uint64_t signature() const noexcept;

  private:
    [[nodiscard]] ModifierEntry* find_mutable(std::string_view id) noexcept;

    std::vector<ModifierEntry> entries_;
    ModifierLimits limits_{};
};

struct ModifierStoreSummary {
    std::size_t mode_count{};
    std::size_t entry_count{};
    std::size_t enabled_entry_count{};
};

class ModifierStore {
  public:
    void clear() noexcept;
    void set(std::string_view mode_id, ModifierEntry entry);
    void set_limits(std::string_view mode_id, ModifierLimits limits);
    void remove(std::string_view mode_id, std::string_view modifier_id) noexcept;

    [[nodiscard]] bool has_mode(std::string_view mode_id) const noexcept;
    [[nodiscard]] const ModifierSet& view(std::string_view mode_id) const noexcept;
    [[nodiscard]] ModifierStoreSummary summary() const noexcept;

  private:
    [[nodiscard]] ModifierSet& mutable_view(std::string_view mode_id);

    std::unordered_map<std::string, ModifierSet> sets_by_mode_;
    ModifierSet empty_set_{};
};

[[nodiscard]] ModifierKind classify_modifier_id(std::string_view id) noexcept;
[[nodiscard]] std::string_view to_string(ModifierKind kind) noexcept;

[[nodiscard]] ScoringRules apply_modifiers_to_scoring_rules(
    const ScoringRules& rules,
    const ModifierSet& modifiers) noexcept;

[[nodiscard]] rhythm::TimingJudgementResult apply_modifiers_to_judgement(
    const rhythm::TimingJudgementResult& judgement,
    const rhythm::TimingWindowSet& window_set,
    const ModifierSet& modifiers) noexcept;

[[nodiscard]] std::uint32_t mirror_channel_index(
    std::uint32_t channel_index,
    const ModifierSet& modifiers) noexcept;

} // namespace reaktio::gameplay
