#pragma once

#include "reaktio/gameplay/CueScheduler.hpp"
#include "reaktio/gameplay/IGameMode.hpp"
#include "reaktio/gameplay/RailChart.hpp"
#include "reaktio/gameplay/RailInteractionRules.hpp"
#include "reaktio/gameplay/RailObstacles.hpp"
#include "reaktio/gameplay/RailPath.hpp"
#include "reaktio/gameplay/Scoring.hpp"
#include "reaktio/rhythm/TempoMap.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reaktio::games::rail_slice {

inline constexpr std::string_view k_rail_action_context = "rail-slice";
inline constexpr std::string_view k_rail_action_lane_left = "rail.lane_left";
inline constexpr std::string_view k_rail_action_lane_right = "rail.lane_right";
inline constexpr std::string_view k_rail_action_jump = "rail.jump";
inline constexpr std::string_view k_rail_action_slide = "rail.slide";
inline constexpr std::string_view k_rail_action_fire = "rail.fire";
inline constexpr std::string_view k_rail_action_dodge = "rail.dodge";
inline constexpr std::string_view k_rail_action_hold = "rail.hold";

struct RailSliceConfig {
    // Player rail-velocity: arc length per second. Mirrors the "scrolling
    // mode" speed; constant for v1.
    double player_arc_velocity{8.0};
    // Lane layout authoring; spacing is forwarded into the rail chart too.
    gameplay::RailLaneLayout lane_layout{.lane_count = 5, .lane_spacing = 1.5};
    // Interaction rule configs are public so the host (or future authoring
    // tool) can tune them per-song or per-lesson.
    gameplay::LaneSwapRuleConfig lane_swap_config{};
    gameplay::VerticalActionRuleConfig vertical_action_config{};
    gameplay::ShootRuleConfig shoot_config{};
    gameplay::DodgeRuleConfig dodge_config{};
    gameplay::HoldRuleConfig hold_rule_config{};  // Modes set start/end based on the chart.
    // Optional: disable replay-sample submission when running off-band
    // (e.g. as a smoke shutdown verifier).
    bool record_replay_samples{true};
    // Optional: persist a save-data record on exit (mode session row).
    bool record_save_data{true};
};

// Rail/lane/runner-style slice. Composes the engine-layer rail primitives
// (RailPath, RailChart, RailObstacleField) with the shared rhythm and
// gameplay subsystems (CueScheduler, ScoreTracker, ReplayRecorder,
// PresentationEventBus, SaveDataStore) to prove a 2.5D mode can run
// entirely on the shared engine stack without bypassing any of the
// architectural boundaries set in Phase 0.
class RailSliceMode final : public gameplay::IGameMode {
  public:
    RailSliceMode();
    explicit RailSliceMode(RailSliceConfig config);

    [[nodiscard]] static const gameplay::ModeDescriptor& mode_descriptor() noexcept;

    [[nodiscard]] const gameplay::ModeDescriptor& descriptor() const noexcept override;
    void on_enter(gameplay::IModeHost& host, const gameplay::ModeEnterContext& context) override;
    void on_fixed_step(gameplay::IModeHost& host, const gameplay::ModeFixedStepContext& context) override;
    void on_render_extract(gameplay::IModeHost& host, const gameplay::ModeRenderContext& context) override;
    void on_exit(gameplay::IModeHost& host, const gameplay::ModeExitContext& context) override;

    // Read-only observation surfaces used by the smoke shutdown verifier
    // and any future inspector. None of these mutate engine state.
    [[nodiscard]] const gameplay::RailPath& path() const noexcept;
    [[nodiscard]] const gameplay::RailChart& chart() const noexcept;
    [[nodiscard]] const gameplay::RailObstacleField& obstacles() const noexcept;
    [[nodiscard]] const gameplay::RailPlayerState& player_state() const noexcept;
    [[nodiscard]] const gameplay::ScoreTracker& scoring() const noexcept;
    [[nodiscard]] const gameplay::CueScheduler& cue_scheduler() const noexcept;
    [[nodiscard]] const rhythm::TempoMap& tempo_map() const noexcept;
    [[nodiscard]] const gameplay::LaneSwapRuleState& lane_swap_state() const noexcept;
    [[nodiscard]] const gameplay::VerticalActionRuleState& vertical_action_state() const noexcept;
    [[nodiscard]] const gameplay::ShootRuleState& shoot_state() const noexcept;
    [[nodiscard]] const gameplay::DodgeRuleState& dodge_state() const noexcept;
    [[nodiscard]] const gameplay::HoldRuleState& hold_state() const noexcept;
    [[nodiscard]] std::uint32_t projectile_hits() const noexcept;
    [[nodiscard]] std::uint32_t hazard_hits() const noexcept;
    [[nodiscard]] std::uint32_t pickups_collected() const noexcept;
    [[nodiscard]] std::uint32_t chart_cues_judged() const noexcept;
    [[nodiscard]] std::uint32_t spatial_cue_sample_count() const noexcept;
    [[nodiscard]] std::uint32_t presentation_events_emitted() const noexcept;
    [[nodiscard]] std::uint32_t lifecycle_event_count() const noexcept;

  private:
    void ensure_initialized(gameplay::IModeHost& host);
    void apply_input(gameplay::IModeHost& host, double fixed_delta_seconds);
    void advance_player(double fixed_delta_seconds);
    void resolve_world_interactions(gameplay::IModeHost& host, const gameplay::ModeFixedStepContext& context);
    void resolve_chart_cues(gameplay::IModeHost& host, const gameplay::ModeFixedStepContext& context);

    RailSliceConfig config_;
    gameplay::RailPath path_{};
    gameplay::RailChart chart_{};
    gameplay::RailObstacleField obstacles_{};
    gameplay::RailEnvTriggerStream env_triggers_{};
    rhythm::TempoMap tempo_map_{};
    rhythm::TimingWindowSet timing_windows_{};
    std::vector<rhythm::ScheduledCue> scheduled_cues_{};
    gameplay::CueScheduler cue_scheduler_{};
    gameplay::ScoreTracker scoring_{};
    gameplay::RailPlayerState player_{};
    gameplay::LaneSwapRuleState lane_swap_state_{};
    gameplay::VerticalActionRuleState vertical_action_state_{};
    gameplay::ShootRuleState shoot_state_{};
    gameplay::DodgeRuleState dodge_state_{};
    gameplay::HoldRuleState hold_state_{};
    std::vector<gameplay::RailProjectile> projectiles_{};
    std::vector<gameplay::RailProjectileHit> projectile_hit_buffer_{};
    std::vector<std::size_t> overlap_buffer_{};
    std::vector<gameplay::RailEnvTrigger> trigger_buffer_{};
    std::vector<gameplay::SpatialCueSample> spatial_cue_buffer_{};
    std::vector<std::uint8_t> chart_cue_judged_{};
    std::uint32_t projectile_hits_{};
    std::uint32_t hazard_hits_{};
    std::uint32_t pickups_collected_{};
    std::uint32_t chart_cues_judged_{};
    std::uint32_t spatial_cue_sample_count_{};
    std::uint32_t presentation_events_emitted_{};
    std::uint32_t lifecycle_event_count_{};
    bool initialized_{false};
};

} // namespace reaktio::games::rail_slice
