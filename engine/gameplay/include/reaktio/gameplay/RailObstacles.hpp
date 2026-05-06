#pragma once

#include "reaktio/gameplay/RailPath.hpp"
#include "reaktio/rhythm/TempoMap.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace reaktio::gameplay {

// Engine-layer rail-aligned collision and interaction primitives. Lives in
// the shared gameplay layer so any rail/lane/runner mode (and the future
// spatial 3D slice when it adopts rail-aligned obstacles) can reuse them.
//
// Architectural rules:
// - Collision happens in arc-length x lane-index space, not 3D world space.
//   Rail/lane modes describe both player and obstacles in this 2-axis space,
//   which keeps obstacle queries O(log N) and replay-deterministic with no
//   floating-point drift across long playthroughs.
// - The world-space position of any obstacle is a presentation concern,
//   resolved by sampling the RailPath when rendering. Obstacle identity does
//   NOT depend on world geometry, mirroring the chart/presentation split
//   established for cues in Phase 0.
// - Interaction primitives (hit-scan, projectiles) operate on the same arc-
//   length axis. Modes layer their gameplay rules (dodge, shoot, lane swap,
//   jump, slide, hold) on top using these primitives as deterministic
//   building blocks; they are intentionally not encoded as a fixed enum here.

enum class RailObstacleFlag : std::uint32_t {
    None = 0,
    Hazard = 1u << 0u,    // Damages or fails the player on contact.
    Pickup = 1u << 1u,    // Consumed on contact, awards score/charge.
    Solid = 1u << 2u,     // Stops projectiles and the player avatar.
    Trigger = 1u << 3u,   // Fires a presentation/event hook on entry.
    Shootable = 1u << 4u, // Hit-scan and projectiles can resolve against it.
};

[[nodiscard]] constexpr std::uint32_t operator|(RailObstacleFlag a, RailObstacleFlag b) noexcept {
    return static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b);
}
[[nodiscard]] constexpr std::uint32_t operator|(std::uint32_t a, RailObstacleFlag b) noexcept {
    return a | static_cast<std::uint32_t>(b);
}
[[nodiscard]] constexpr bool has_flag(std::uint32_t flags, RailObstacleFlag flag) noexcept {
    return (flags & static_cast<std::uint32_t>(flag)) != 0u;
}

struct RailObstacle {
    std::uint64_t obstacle_id{};                  // Stable for replay; modes assign.
    double arc_length{};                          // Center along the rail.
    double arc_length_half_extent{0.5};           // Along-rail half-width.
    std::int32_t signed_lane_min{0};              // Inclusive.
    std::int32_t signed_lane_max{0};              // Inclusive.
    double vertical_offset{0.0};                  // Along path normal; presentation-only.
    std::uint32_t flags{static_cast<std::uint32_t>(RailObstacleFlag::Hazard)};
    std::int32_t group_index{0};                  // Optional grouping for env/trigger fan-out.
    std::uint32_t hit_points{1};                  // Shootable obstacles; 0 disables hit-scan kills.
};

// Sorted, queryable obstacle field. Build once per chart load. Geometry
// (positions, lane ranges, flags) is immutable post-rebuild so replays
// stay clean. Runtime state (remaining hit points, destroyed bitmap) is
// owned and mutated by the field via register_hit/mark_destroyed; modes
// no longer need to maintain a parallel HP vector.
class RailObstacleField {
  public:
    void clear() noexcept;
    void rebuild(std::vector<RailObstacle> obstacles);

    [[nodiscard]] std::span<const RailObstacle> obstacles() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

    // Reset the destroyed bitmap and runtime hit points to the values from
    // the last rebuild() without touching geometry.
    void reset_runtime_state() noexcept;

    // Per-frame queries: append indices into obstacles() for everything that
    // overlaps the player point/segment in arc-length x lane-index space.
    // Output buffers are not cleared by the query; callers control reuse.
    void query_overlap_point(
        double player_arc_length,
        std::int32_t player_signed_lane,
        std::vector<std::size_t>& out_indices) const;
    void query_overlap_range(
        double arc_length_min,
        double arc_length_max,
        std::int32_t player_signed_lane,
        std::vector<std::size_t>& out_indices) const;

    // Apply damage. Returns true iff the obstacle was destroyed by THIS
    // call (transitioned from alive to destroyed). Idempotent on already
    // destroyed obstacles: returns false and does not double-fire effects.
    [[nodiscard]] bool register_hit(std::size_t obstacle_index, std::uint32_t damage) noexcept;

    // Force-destroy without applying damage; used for proximity pickups.
    // Returns true iff this call transitioned the obstacle from alive to
    // destroyed (idempotent on already destroyed).
    bool mark_destroyed(std::size_t obstacle_index) noexcept;
    [[nodiscard]] bool is_destroyed(std::size_t obstacle_index) const noexcept;
    [[nodiscard]] std::uint32_t remaining_hit_points(std::size_t obstacle_index) const noexcept;

  private:
    std::vector<RailObstacle> obstacles_;
    std::vector<std::uint32_t> runtime_hit_points_;
    std::vector<std::uint8_t> destroyed_;
    std::vector<std::size_t> arc_length_sorted_indices_;
};

// Player kinematic on the rail. Pure data; modes drive the values.
struct RailPlayerState {
    double arc_length{0.0};
    double arc_length_velocity{0.0};
    std::int32_t signed_lane{0};
    // Reserved 0..1 interpolation between adjacent lanes; not yet driven by
    // any engine-layer rule. Future lane-swap animation will write this and
    // snap signed_lane on completion. Kept here so render extraction has a
    // stable place to read it once the rule lands; modes that need an
    // interpolated visual today should drive it themselves.
    double lateral_blend{0.0};
    double vertical_offset{0.0};      // Driven by tick_vertical_action_rule.
    std::uint32_t status_flags{0};    // Mode-defined: invulnerable, ducked, charging, etc.
};

// Hit-scan probe. The probe travels forward along the rail from origin_arc_length
// for max_distance, only on origin_signed_lane (no lateral spread in v1; modes
// can fire multiple probes for spread weapons). Returns the first solid or
// shootable hit unless pierce_count > 0, in which case it accumulates hits up
// to that many.
struct RailHitScanProbe {
    double origin_arc_length{0.0};
    std::int32_t origin_signed_lane{0};
    double max_distance{50.0};
    std::uint32_t pierce_count{0};
    std::uint32_t required_flag_mask{static_cast<std::uint32_t>(RailObstacleFlag::Shootable)};
    bool ignore_destroyed{true};
};

struct RailHitScanHit {
    std::size_t obstacle_index{};
    std::uint64_t obstacle_id{};
    double hit_arc_length{};
    double distance{};
};

void resolve_hit_scan(
    const RailObstacleField& field,
    const RailHitScanProbe& probe,
    std::vector<RailHitScanHit>& out_hits);

// Projectile emitter. Discrete-step kinematics on the rail; one entry per
// in-flight projectile, advanced with advance_projectiles(). Collision is
// resolved against the obstacle field at each step. Deterministic when fed
// constant fixed_delta_seconds and stable obstacle indices.
struct RailProjectile {
    std::uint64_t projectile_id{};
    double arc_length{};
    std::int32_t signed_lane{};
    double speed{60.0};               // Arc length per second; positive = forward.
    double remaining_lifetime{2.0};   // Seconds.
    std::uint32_t pierce_remaining{0};
    std::uint32_t damage{1};
    bool active{true};
};

struct RailProjectileHit {
    std::uint64_t projectile_id{};
    std::size_t obstacle_index{};
    std::uint64_t obstacle_id{};
    double hit_arc_length{};
    // Snapshot of the projectile's damage at the moment of the hit. Modes
    // pass this into RailObstacleField::register_hit() so the field owns
    // the HP bookkeeping; pierce-through still works because the field
    // returns false until the obstacle's HP reaches zero.
    std::uint32_t damage{1};
};

void advance_projectiles(
    std::vector<RailProjectile>& projectiles,
    const RailObstacleField& field,
    double fixed_delta_seconds,
    std::vector<RailProjectileHit>& out_hits);

// Env trigger primitive. A trigger fires once when the player crosses its
// arc length. Order is preserved across replays because triggers are sorted
// by arc length at rebuild and consumed monotonically with the player's
// reference arc length. Modes feed the resulting trigger ids into their own
// presentation event bus / haptics / camera systems.
struct RailEnvTrigger {
    std::uint64_t trigger_id{};
    double arc_length{};
    rhythm::ChartTick scheduled_tick{0};  // Optional chart tick if authored time-keyed.
    std::uint32_t kind_tag{0};            // Mode-defined kind (light, hazard pulse, camera shake...).
    double payload_scalar{0.0};
};

class RailEnvTriggerStream {
  public:
    void clear() noexcept;
    void rebuild(std::vector<RailEnvTrigger> triggers);

    [[nodiscard]] std::span<const RailEnvTrigger> triggers() const noexcept;
    [[nodiscard]] std::size_t fired_count() const noexcept;
    void reset_runtime_state() noexcept;

    // Advance the player's reference arc length and append triggers that
    // were crossed since the last call. Caller-owned out_fired buffer.
    void advance_to_arc_length(
        double player_arc_length,
        std::vector<RailEnvTrigger>& out_fired);

  private:
    std::vector<RailEnvTrigger> triggers_;
    std::size_t cursor_{0};
    double last_arc_length_{0.0};
};

} // namespace reaktio::gameplay
