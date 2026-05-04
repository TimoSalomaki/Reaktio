#include "reaktio/gameplay/Modifiers.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace reaktio::gameplay {

namespace {

constexpr std::uint64_t k_fnv_offset = 14695981039346656037ull;
constexpr std::uint64_t k_fnv_prime = 1099511628211ull;

void hash_combine(std::uint64_t& hash, std::uint64_t value) noexcept {
    hash ^= value;
    hash *= k_fnv_prime;
}

std::uint64_t hash_double(double value) noexcept {
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(std::llround(value * 1000000.0)));
}

const rhythm::TimingWindow* tightest_perfect_window(const rhythm::TimingWindowSet& window_set) noexcept {
    const rhythm::TimingWindow* best = nullptr;
    for (const rhythm::TimingWindow& window : window_set.ordered_windows) {
        if (window.judgement != rhythm::TimingJudgement::Perfect) {
            continue;
        }

        if (best == nullptr ||
            (window.early_window_microseconds + window.late_window_microseconds) <
                (best->early_window_microseconds + best->late_window_microseconds)) {
            best = &window;
        }
    }

    return best;
}

} // namespace

ModifierKind classify_modifier_id(std::string_view id) noexcept {
    if (id == modifier_ids::k_speed_multiplier) {
        return ModifierKind::SpeedMultiplier;
    }
    if (id == modifier_ids::k_autoplay) {
        return ModifierKind::Autoplay;
    }
    if (id == modifier_ids::k_no_fail) {
        return ModifierKind::NoFail;
    }
    if (id == modifier_ids::k_mirror_channels) {
        return ModifierKind::MirrorChannels;
    }
    if (id == modifier_ids::k_practice_assist) {
        return ModifierKind::PracticeAssist;
    }
    return ModifierKind::Custom;
}

std::string_view to_string(ModifierKind kind) noexcept {
    switch (kind) {
    case ModifierKind::Custom:
        return "custom";
    case ModifierKind::SpeedMultiplier:
        return "speed_multiplier";
    case ModifierKind::Autoplay:
        return "autoplay";
    case ModifierKind::NoFail:
        return "no_fail";
    case ModifierKind::MirrorChannels:
        return "mirror_channels";
    case ModifierKind::PracticeAssist:
        return "practice_assist";
    }

    return "unknown";
}

void ModifierSet::clear() noexcept {
    entries_.clear();
}

void ModifierSet::set(ModifierEntry entry) {
    if (entry.kind == ModifierKind::Custom) {
        entry.kind = classify_modifier_id(entry.id);
    }

    if (ModifierEntry* existing = find_mutable(entry.id); existing != nullptr) {
        *existing = std::move(entry);
        return;
    }

    entries_.push_back(std::move(entry));
}

void ModifierSet::set_enabled(std::string_view id, bool enabled) noexcept {
    if (ModifierEntry* existing = find_mutable(id); existing != nullptr) {
        existing->enabled = enabled;
    }
}

void ModifierSet::remove(std::string_view id) noexcept {
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(), [id](const ModifierEntry& entry) {
            return entry.id == id;
        }),
        entries_.end());
}

const ModifierEntry* ModifierSet::find(std::string_view id) const noexcept {
    const auto it = std::find_if(entries_.begin(), entries_.end(), [id](const ModifierEntry& entry) {
        return entry.id == id;
    });
    return it != entries_.end() ? &(*it) : nullptr;
}

bool ModifierSet::is_active(std::string_view id) const noexcept {
    const ModifierEntry* entry = find(id);
    return entry != nullptr && entry->enabled;
}

std::span<const ModifierEntry> ModifierSet::entries() const noexcept {
    return std::span<const ModifierEntry>{entries_.data(), entries_.size()};
}

double ModifierSet::speed_multiplier(double fallback) const noexcept {
    const ModifierEntry* entry = find(modifier_ids::k_speed_multiplier);
    if (entry == nullptr || !entry->enabled || !std::isfinite(entry->numeric_parameter)) {
        return fallback;
    }

    return std::clamp(
        entry->numeric_parameter,
        limits_.min_speed_multiplier,
        limits_.max_speed_multiplier);
}

bool ModifierSet::autoplay_enabled() const noexcept {
    return is_active(modifier_ids::k_autoplay);
}

bool ModifierSet::no_fail_enabled() const noexcept {
    return is_active(modifier_ids::k_no_fail);
}

std::uint32_t ModifierSet::mirror_channel_count() const noexcept {
    const ModifierEntry* entry = find(modifier_ids::k_mirror_channels);
    if (entry == nullptr || !entry->enabled || entry->integer_parameter <= 1) {
        return 0;
    }

    const std::int64_t clamped = std::min<std::int64_t>(
        entry->integer_parameter,
        limits_.max_mirror_channel_count > 0 ? limits_.max_mirror_channel_count : entry->integer_parameter);
    return static_cast<std::uint32_t>(std::max<std::int64_t>(0, clamped));
}

bool ModifierSet::practice_assist_enabled() const noexcept {
    return is_active(modifier_ids::k_practice_assist);
}

const ModifierLimits& ModifierSet::limits() const noexcept {
    return limits_;
}

void ModifierSet::set_limits(ModifierLimits limits) noexcept {
    limits_ = limits;
}

std::uint64_t ModifierSet::signature() const noexcept {
    std::uint64_t hash = k_fnv_offset;
    for (const ModifierEntry& entry : entries_) {
        for (const char ch : entry.id) {
            hash_combine(hash, static_cast<std::uint64_t>(static_cast<unsigned char>(ch)));
        }
        hash_combine(hash, static_cast<std::uint64_t>(entry.kind));
        hash_combine(hash, hash_double(entry.numeric_parameter));
        hash_combine(hash, static_cast<std::uint64_t>(entry.integer_parameter));
        hash_combine(hash, static_cast<std::uint64_t>(entry.boolean_parameter));
        hash_combine(hash, static_cast<std::uint64_t>(entry.enabled));
    }
    return hash;
}

ModifierEntry* ModifierSet::find_mutable(std::string_view id) noexcept {
    const auto it = std::find_if(entries_.begin(), entries_.end(), [id](const ModifierEntry& entry) {
        return entry.id == id;
    });
    return it != entries_.end() ? &(*it) : nullptr;
}

void ModifierStore::clear() noexcept {
    sets_by_mode_.clear();
}

void ModifierStore::set(std::string_view mode_id, ModifierEntry entry) {
    mutable_view(mode_id).set(std::move(entry));
}

void ModifierStore::set_limits(std::string_view mode_id, ModifierLimits limits) {
    mutable_view(mode_id).set_limits(limits);
}

void ModifierStore::remove(std::string_view mode_id, std::string_view modifier_id) noexcept {
    const auto it = sets_by_mode_.find(std::string(mode_id));
    if (it == sets_by_mode_.end()) {
        return;
    }

    it->second.remove(modifier_id);
}

bool ModifierStore::has_mode(std::string_view mode_id) const noexcept {
    return sets_by_mode_.find(std::string(mode_id)) != sets_by_mode_.end();
}

const ModifierSet& ModifierStore::view(std::string_view mode_id) const noexcept {
    const auto it = sets_by_mode_.find(std::string(mode_id));
    return it != sets_by_mode_.end() ? it->second : empty_set_;
}

ModifierStoreSummary ModifierStore::summary() const noexcept {
    ModifierStoreSummary summary{};
    summary.mode_count = sets_by_mode_.size();
    for (const auto& [mode_id, set] : sets_by_mode_) {
        for (const ModifierEntry& entry : set.entries()) {
            ++summary.entry_count;
            if (entry.enabled) {
                ++summary.enabled_entry_count;
            }
        }
    }
    return summary;
}

ModifierSet& ModifierStore::mutable_view(std::string_view mode_id) {
    const auto [it, inserted] = sets_by_mode_.try_emplace(std::string(mode_id));
    return it->second;
}

ScoringRules apply_modifiers_to_scoring_rules(
    const ScoringRules& rules,
    const ModifierSet& modifiers) noexcept {
    ScoringRules result = rules;
    if (modifiers.no_fail_enabled()) {
        result.fail_on_empty_health = false;
        result.min_health = std::min(result.min_health, 0.0);
    }
    return result;
}

rhythm::TimingJudgementResult apply_modifiers_to_judgement(
    const rhythm::TimingJudgementResult& judgement,
    const rhythm::TimingWindowSet& window_set,
    const ModifierSet& modifiers) noexcept {
    if (!modifiers.autoplay_enabled()) {
        return judgement;
    }

    rhythm::TimingJudgementResult forced = judgement;
    forced.judgement = rhythm::TimingJudgement::Perfect;
    forced.matched_window = true;
    forced.scoreable_hit = true;
    forced.advances_combo = true;
    forced.early = false;
    forced.late = false;
    forced.corrected_error_microseconds = 0;
    forced.raw_error_microseconds = 0;

    if (const rhythm::TimingWindow* perfect = tightest_perfect_window(window_set); perfect != nullptr) {
        forced.matched_early_window_microseconds = perfect->early_window_microseconds;
        forced.matched_late_window_microseconds = perfect->late_window_microseconds;
    }

    return forced;
}

std::uint32_t mirror_channel_index(
    std::uint32_t channel_index,
    const ModifierSet& modifiers) noexcept {
    const std::uint32_t lane_count = modifiers.mirror_channel_count();
    if (lane_count <= 1) {
        return channel_index;
    }

    const std::uint32_t bounded = channel_index % lane_count;
    return lane_count - 1u - bounded;
}

} // namespace reaktio::gameplay
