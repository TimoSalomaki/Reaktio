#pragma once

#include "reaktio/gameplay/RailObstacles.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reaktio::gameplay {

// Engine-layer mode-rule helpers for rail-aligned interactions. These are
// composable building blocks that ANY rail/lane/runner-family mode can opt
// into; the rail slice composes them, but the spatial 3D slice may reuse
// the same lane-swap / vertical-action helpers when it adopts rail-aligned
// movement.
//
// Architectural rules:
// - All helpers are pure functions over plain-data state structs. No
//   ownership of input surfaces or score trackers; modes pass the bool
//   inputs they have already resolved through their input action map.
// - Determinism is preserved when fixed_delta_seconds is constant and
//   inputs come from a deterministic source (replay, scripted dry-run,
//   seeded test).
// - The helpers do not enumerate a closed set of "interactions". Modes that
//   need additional interaction kinds (charge attack, parry, etc.) layer
//   their own rules alongside; nothing in the engine layer assumes the set
//   of helpers below is exhaustive.

struct LaneSwapRuleConfig {
    std::int32_t lane_min{-2};
    std::int32_t lane_max{+2};
    double cooldown_seconds{0.12};  // Prevents instant lane-spam.
    bool wrap_lanes{false};
};

struct LaneSwapRuleState {
    double cooldown_remaining_seconds{0.0};
    std::uint32_t swap_count{0};
};

struct LaneSwapInput {
    bool swap_left{false};
    bool swap_right{false};
};

void tick_lane_swap_rule(
    const LaneSwapRuleConfig& config,
    LaneSwapRuleState& state,
    const LaneSwapInput& input,
    RailPlayerState& player,
    double fixed_delta_seconds);

// Vertical action rule: jump, slide, or any other timed up/down overlay
// applied through RailPlayerState::vertical_offset and a status flag bit.
// The mode owns which status-flag bits mean what; we just toggle them while
// the timer is active.
struct VerticalActionRuleConfig {
    double jump_amplitude{1.5};
    double jump_duration_seconds{0.55};
    double slide_amplitude{-0.5};
    double slide_duration_seconds{0.45};
    std::uint32_t jump_status_flag{1u << 0u};
    std::uint32_t slide_status_flag{1u << 1u};
};

struct VerticalActionRuleState {
    double remaining_seconds{0.0};
    double active_amplitude{0.0};
    double active_duration_seconds{0.0};
    std::uint32_t active_status_flag{0u};
    std::uint32_t jump_count{0};
    std::uint32_t slide_count{0};
};

struct VerticalActionInput {
    bool jump{false};
    bool slide{false};
};

void tick_vertical_action_rule(
    const VerticalActionRuleConfig& config,
    VerticalActionRuleState& state,
    const VerticalActionInput& input,
    RailPlayerState& player,
    double fixed_delta_seconds);

// Shoot rule: emits a forward projectile per pressed event when the
// cooldown has elapsed. Modes pass the resulting projectile back into
// advance_projectiles each fixed step.
struct ShootRuleConfig {
    double projectile_speed{60.0};
    double projectile_lifetime_seconds{2.0};
    double cooldown_seconds{0.10};
    std::uint32_t pierce_count{0};
    std::uint32_t damage{1};
    double muzzle_arc_offset{1.0};  // Spawned slightly ahead of the player.
};

struct ShootRuleState {
    double cooldown_remaining_seconds{0.0};
    std::uint64_t next_projectile_id{1};
    std::uint32_t emitted_count{0};
};

struct ShootInput {
    bool fire{false};
};

void tick_shoot_rule(
    const ShootRuleConfig& config,
    ShootRuleState& state,
    const ShootInput& input,
    const RailPlayerState& player,
    std::vector<RailProjectile>& out_projectiles,
    double fixed_delta_seconds);

// Hold rule: tracks whether a single hold is being sustained. Modes set the
// hold's expected start/end arc lengths and feed the held-down state each
// fixed step; the rule produces success/fail/in-progress outcomes that
// modes feed into their scoring as they see fit.
enum class HoldRuleOutcome : std::uint8_t {
    Idle,         // No hold active.
    InProgress,   // Hold active and being sustained.
    Completed,    // Hold finished successfully (sustained through end).
    Released,     // Player released before end_arc_length.
    Missed,       // Hold ended without ever starting (input not held).
};

struct HoldRuleConfig {
    double start_arc_length{0.0};
    double end_arc_length{0.0};
    double release_grace_arc_length{0.25};  // Tiny tail tolerance.
};

struct HoldRuleState {
    bool armed{false};       // True between start and end arc length.
    bool sustained{false};   // True while held during armed window.
    HoldRuleOutcome last_outcome{HoldRuleOutcome::Idle};
    std::uint32_t completed_count{0};
    std::uint32_t released_count{0};
    std::uint32_t missed_count{0};
};

[[nodiscard]] std::string_view to_string(HoldRuleOutcome outcome) noexcept;

void tick_hold_rule(
    const HoldRuleConfig& config,
    HoldRuleState& state,
    bool input_held,
    double player_arc_length);

// Dodge rule: a brief invulnerability driven by a single pressed action.
// The rule is intentionally minimal at the engine layer: it owns the
// invulnerability bit on the player and the dodge counter; visual side-step
// or other presentation effects are a mode concern, derived from
// state.remaining_seconds / config.duration_seconds.
struct DodgeRuleConfig {
    double duration_seconds{0.35};
    std::uint32_t invulnerable_status_flag{1u << 2u};
};

struct DodgeRuleState {
    double remaining_seconds{0.0};
    std::uint32_t dodge_count{0};
};

struct DodgeInput {
    bool dodge{false};
};

void tick_dodge_rule(
    const DodgeRuleConfig& config,
    DodgeRuleState& state,
    const DodgeInput& input,
    RailPlayerState& player,
    double fixed_delta_seconds);

} // namespace reaktio::gameplay
