#include "reaktio/gameplay/RailInteractionRules.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace reaktio::gameplay {

namespace {

[[nodiscard]] std::int32_t wrap_lane(
    std::int32_t target, std::int32_t lane_min, std::int32_t lane_max) noexcept {
    if (lane_max <= lane_min) {
        return lane_min;
    }
    const std::int32_t span = lane_max - lane_min + 1;
    std::int32_t shifted = target - lane_min;
    shifted %= span;
    if (shifted < 0) {
        shifted += span;
    }
    return lane_min + shifted;
}

} // namespace

void tick_lane_swap_rule(
    const LaneSwapRuleConfig& config,
    LaneSwapRuleState& state,
    const LaneSwapInput& input,
    RailPlayerState& player,
    double fixed_delta_seconds) {
    if (state.cooldown_remaining_seconds > 0.0) {
        state.cooldown_remaining_seconds =
            std::max(0.0, state.cooldown_remaining_seconds - fixed_delta_seconds);
    }
    if (state.cooldown_remaining_seconds > 0.0) {
        return;
    }
    // Mutually-exclusive press: if both are signalled in the same step,
    // prefer left so behavior stays deterministic across replays.
    std::int32_t target = player.signed_lane;
    if (input.swap_left) {
        target -= 1;
    } else if (input.swap_right) {
        target += 1;
    } else {
        return;
    }
    if (config.wrap_lanes) {
        target = wrap_lane(target, config.lane_min, config.lane_max);
    } else {
        target = std::clamp(target, config.lane_min, config.lane_max);
    }
    if (target == player.signed_lane) {
        return;
    }
    player.signed_lane = target;
    state.cooldown_remaining_seconds = std::max(0.0, config.cooldown_seconds);
    ++state.swap_count;
}

void tick_vertical_action_rule(
    const VerticalActionRuleConfig& config,
    VerticalActionRuleState& state,
    const VerticalActionInput& input,
    RailPlayerState& player,
    double fixed_delta_seconds) {
    // Tick down active overlay first.
    if (state.remaining_seconds > 0.0) {
        state.remaining_seconds = std::max(0.0, state.remaining_seconds - fixed_delta_seconds);
    }
    if (state.remaining_seconds <= 0.0) {
        // Clear lingering effects.
        if (state.active_status_flag != 0u) {
            player.status_flags &= ~state.active_status_flag;
        }
        state.active_status_flag = 0u;
        state.active_amplitude = 0.0;
        state.active_duration_seconds = 0.0;
        player.vertical_offset = 0.0;
    }

    // Start a new overlay only when none is active.
    if (state.remaining_seconds <= 0.0) {
        if (input.jump) {
            state.remaining_seconds = std::max(0.0, config.jump_duration_seconds);
            state.active_amplitude = config.jump_amplitude;
            state.active_duration_seconds = state.remaining_seconds;
            state.active_status_flag = config.jump_status_flag;
            player.status_flags |= state.active_status_flag;
            ++state.jump_count;
        } else if (input.slide) {
            state.remaining_seconds = std::max(0.0, config.slide_duration_seconds);
            state.active_amplitude = config.slide_amplitude;
            state.active_duration_seconds = state.remaining_seconds;
            state.active_status_flag = config.slide_status_flag;
            player.status_flags |= state.active_status_flag;
            ++state.slide_count;
        }
    }

    // Drive the player vertical offset using a simple half-sine envelope so
    // the apex is mid-way through the duration. Deterministic + cheap.
    if (state.remaining_seconds > 0.0 && state.active_duration_seconds > 0.0) {
        const double progress =
            1.0 - (state.remaining_seconds / state.active_duration_seconds);
        // Half-sine: 0 -> 1 -> 0 over [0, 1].
        const double envelope = std::sin(progress * 3.14159265358979323846);
        player.vertical_offset = state.active_amplitude * envelope;
    }
}

void tick_shoot_rule(
    const ShootRuleConfig& config,
    ShootRuleState& state,
    const ShootInput& input,
    const RailPlayerState& player,
    std::vector<RailProjectile>& out_projectiles,
    double fixed_delta_seconds) {
    if (state.cooldown_remaining_seconds > 0.0) {
        state.cooldown_remaining_seconds =
            std::max(0.0, state.cooldown_remaining_seconds - fixed_delta_seconds);
    }
    if (!input.fire || state.cooldown_remaining_seconds > 0.0) {
        return;
    }

    RailProjectile projectile{};
    projectile.projectile_id = state.next_projectile_id++;
    projectile.arc_length = player.arc_length + config.muzzle_arc_offset;
    projectile.signed_lane = player.signed_lane;
    projectile.speed = config.projectile_speed;
    projectile.remaining_lifetime = config.projectile_lifetime_seconds;
    projectile.pierce_remaining = config.pierce_count;
    projectile.damage = config.damage;
    projectile.active = true;
    out_projectiles.push_back(projectile);

    state.cooldown_remaining_seconds = std::max(0.0, config.cooldown_seconds);
    ++state.emitted_count;
}

std::string_view to_string(HoldRuleOutcome outcome) noexcept {
    switch (outcome) {
    case HoldRuleOutcome::Idle:
        return "idle";
    case HoldRuleOutcome::InProgress:
        return "in-progress";
    case HoldRuleOutcome::Completed:
        return "completed";
    case HoldRuleOutcome::Released:
        return "released";
    case HoldRuleOutcome::Missed:
        return "missed";
    }
    return "unknown";
}

void tick_hold_rule(
    const HoldRuleConfig& config,
    HoldRuleState& state,
    bool input_held,
    double player_arc_length) {
    const double end_with_grace = config.end_arc_length + config.release_grace_arc_length;
    const bool inside_window =
        player_arc_length >= config.start_arc_length && player_arc_length <= end_with_grace;

    if (inside_window) {
        if (!state.armed) {
            // Just entered the window.
            state.armed = true;
            state.sustained = input_held;
            state.last_outcome =
                input_held ? HoldRuleOutcome::InProgress : HoldRuleOutcome::Missed;
            return;
        }
        if (input_held) {
            state.sustained = true;
            state.last_outcome = HoldRuleOutcome::InProgress;
        } else if (state.sustained) {
            // Player released mid-hold.
            state.sustained = false;
            state.last_outcome = HoldRuleOutcome::Released;
            ++state.released_count;
        } else {
            state.last_outcome = HoldRuleOutcome::Missed;
        }
        return;
    }

    if (state.armed) {
        // Just exited the window.
        if (state.last_outcome == HoldRuleOutcome::InProgress && state.sustained) {
            state.last_outcome = HoldRuleOutcome::Completed;
            ++state.completed_count;
        } else if (state.last_outcome != HoldRuleOutcome::Released &&
                   state.last_outcome != HoldRuleOutcome::Completed) {
            // Never sustained inside the window.
            state.last_outcome = HoldRuleOutcome::Missed;
            ++state.missed_count;
        }
        state.armed = false;
        state.sustained = false;
    }
}

void tick_dodge_rule(
    const DodgeRuleConfig& config,
    DodgeRuleState& state,
    const DodgeInput& input,
    RailPlayerState& player,
    double fixed_delta_seconds) {
    if (state.remaining_seconds > 0.0) {
        state.remaining_seconds = std::max(0.0, state.remaining_seconds - fixed_delta_seconds);
        if (state.remaining_seconds <= 0.0) {
            player.status_flags &= ~config.invulnerable_status_flag;
        }
    }
    if (input.dodge && state.remaining_seconds <= 0.0) {
        state.remaining_seconds = std::max(0.0, config.duration_seconds);
        player.status_flags |= config.invulnerable_status_flag;
        ++state.dodge_count;
    }
}

} // namespace reaktio::gameplay
