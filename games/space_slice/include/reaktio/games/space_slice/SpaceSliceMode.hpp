#pragma once

#include "reaktio/gameplay/CueScheduler.hpp"
#include "reaktio/gameplay/IGameMode.hpp"
#include "reaktio/gameplay/MusicReactivePresentation.hpp"
#include "reaktio/gameplay/Scoring.hpp"
#include "reaktio/gameplay/SpatialCamera.hpp"
#include "reaktio/gameplay/SpatialCollision.hpp"
#include "reaktio/gameplay/SpatialKinematics.hpp"
#include "reaktio/gameplay/SpatialPatternGenerator.hpp"
#include "reaktio/rhythm/TempoMap.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reaktio::games::space_slice {

// Action context + identifiers. Modes wire actual key/controller bindings
// at the application layer; the slice just reads named action events
// through the same ActionInputSurface every other mode uses.
inline constexpr std::string_view k_space_action_context = "space-slice";
inline constexpr std::string_view k_space_action_orbit_left = "space.orbit_left";
inline constexpr std::string_view k_space_action_orbit_right = "space.orbit_right";
inline constexpr std::string_view k_space_action_dive = "space.dive";   // Bring orbit radius inward.
inline constexpr std::string_view k_space_action_climb = "space.climb"; // Push orbit radius outward.

struct SpaceSliceConfig {
    // Player control parameters. The player rides on an orbit ring around
    // the tunnel axis at a configurable radius; orbit_left/right rotate
    // the heading, climb/dive shift the radius. These are deliberately
    // decoupled from the hazard sweep velocity so modes can tune player
    // agency separately from incoming difficulty.
    float player_orbit_radius{4.0f};
    float player_radius_min{2.0f};
    float player_radius_max{8.0f};
    float player_orbit_speed_radians_per_second{4.5f};
    float player_radius_speed_per_second{4.0f};
    float player_collision_radius{0.55f};
    // Tunnel geometry. The tunnel axis is +Z; the orbit "up" basis is +Y.
    // Modes that want a different orientation can override the rig later.
    gameplay::Vector3 tunnel_center{0.0f, 0.0f, 0.0f};
    gameplay::Vector3 tunnel_axis_forward{0.0f, 0.0f, 1.0f};
    gameplay::Vector3 tunnel_axis_up{0.0f, 1.0f, 0.0f};
    // Pattern RNG configuration. By default the slice draws from the
    // host's DeterministicRandomService stream "space.patterns", so the
    // runtime root seed propagates into pattern authoring (and therefore
    // into replays). For off-band tooling that wants reproducible output
    // independent of the runtime seed, set use_host_random_service=false
    // and the slice will use a private rng seeded by pattern_seed.
    bool use_host_random_service{true};
    std::string_view pattern_rng_stream_name{"space.patterns"};
    std::uint64_t pattern_seed{0xC0FFEEC0FFEE2026ull};
    // Number of patterns to pre-author into the chart. Each pattern emits
    // RingSliceHazards that activate on a beat boundary inside the chart
    // window.
    std::uint32_t pattern_request_count{8};
    // Replay / save toggles mirror the rail slice's policy.
    bool record_replay_samples{true};
    bool record_save_data{true};
};

struct SpaceSlicePlayerState {
    float orbit_heading_radians{0.0f};
    float orbit_radius{4.0f};
    std::uint32_t status_flags{0};
};

struct SpaceSliceHazardInstance {
    gameplay::RingSliceRuntime runtime{};
    bool active{true};
    bool resolved{false};      // True once it has been judged (hit OR safely passed).
    bool transit_recorded{false};  // Spatial-cue accounting marker.
    std::uint64_t pattern_index{};
};

// Vertical slice C: a Super Hexagon-like radial obstacle field rendered
// through the FreeCamera3D path so the engine's full 3D presentation
// pipeline is exercised. Composes:
//   - SpatialPatternGenerator for replay-safe hazard authoring,
//   - SpatialKinematics (RotationalKinematicState + RadialSweepState +
//     OscillatingState) for deterministic motion,
//   - SpatialCollision (sphere vs OBB) for player/hazard contact,
//   - SpatialCamera::TunnelCameraRig for the FreeCamera3D feed,
//   - MusicReactivePresentation for beat-synced env presentation,
//   - shared CueScheduler / ScoreTracker / ModeFlow / ReplayRecorder /
//     PresentationEventBus / SaveDataStore (proven by the rail slice).
class SpaceSliceMode final : public gameplay::IGameMode {
  public:
    SpaceSliceMode();
    explicit SpaceSliceMode(SpaceSliceConfig config);

    [[nodiscard]] static const gameplay::ModeDescriptor& mode_descriptor() noexcept;

    [[nodiscard]] const gameplay::ModeDescriptor& descriptor() const noexcept override;
    void on_enter(gameplay::IModeHost& host, const gameplay::ModeEnterContext& context) override;
    void on_fixed_step(gameplay::IModeHost& host, const gameplay::ModeFixedStepContext& context) override;
    void on_render_extract(gameplay::IModeHost& host, const gameplay::ModeRenderContext& context) override;
    void on_exit(gameplay::IModeHost& host, const gameplay::ModeExitContext& context) override;

    // Read-only observation surfaces consumed by the smoke verifier.
    [[nodiscard]] const SpaceSliceConfig& config() const noexcept { return config_; }
    [[nodiscard]] const SpaceSlicePlayerState& player_state() const noexcept { return player_; }
    [[nodiscard]] const gameplay::ScoreTracker& scoring() const noexcept { return scoring_; }
    [[nodiscard]] const rhythm::TempoMap& tempo_map() const noexcept { return tempo_map_; }
    [[nodiscard]] std::span<const SpaceSliceHazardInstance> hazards() const noexcept {
        return std::span<const SpaceSliceHazardInstance>(hazards_);
    }
    [[nodiscard]] std::uint32_t pattern_request_count() const noexcept { return patterns_authored_; }
    [[nodiscard]] std::uint32_t hazards_authored() const noexcept { return hazards_authored_; }
    [[nodiscard]] std::uint32_t hazards_active() const noexcept;
    [[nodiscard]] std::uint32_t hazards_resolved() const noexcept { return hazards_resolved_; }
    [[nodiscard]] std::uint32_t hazards_avoided() const noexcept { return hazards_avoided_; }
    [[nodiscard]] std::uint32_t hazards_hit() const noexcept { return hazards_hit_; }
    [[nodiscard]] std::uint32_t presentation_events_emitted() const noexcept {
        return presentation_events_emitted_;
    }
    [[nodiscard]] std::uint32_t lifecycle_event_count() const noexcept { return lifecycle_event_count_; }
    [[nodiscard]] double last_beat_pulse_value() const noexcept { return last_beat_pulse_; }
    [[nodiscard]] double last_bar_pulse_value() const noexcept { return last_bar_pulse_; }

  private:
    void ensure_initialized(gameplay::IModeHost& host);
    void apply_input(gameplay::IModeHost& host, double fixed_delta_seconds);
    void advance_world(gameplay::IModeHost& host, const gameplay::ModeFixedStepContext& context);
    void evaluate_music_envelopes(
        gameplay::IModeHost& host, const gameplay::ModeFixedStepContext& context);

    SpaceSliceConfig config_;
    SpaceSlicePlayerState player_{};
    rhythm::TempoMap tempo_map_{};
    rhythm::TimingWindowSet timing_windows_{};
    std::vector<rhythm::ScheduledCue> scheduled_cues_{};
    gameplay::CueScheduler cue_scheduler_{};
    gameplay::ScoreTracker scoring_{};
    std::vector<SpaceSliceHazardInstance> hazards_{};
    gameplay::PulseEnvelopeConfig beat_pulse_config_{};
    gameplay::PulseEnvelopeConfig bar_pulse_config_{};
    double last_beat_pulse_{0.0};
    double last_bar_pulse_{0.0};
    bool last_beat_pulse_emitted_{false};
    bool last_bar_pulse_emitted_{false};
    std::uint32_t patterns_authored_{};
    std::uint32_t hazards_authored_{};
    std::uint32_t hazards_resolved_{};
    std::uint32_t hazards_avoided_{};
    std::uint32_t hazards_hit_{};
    std::uint32_t presentation_events_emitted_{};
    std::uint32_t lifecycle_event_count_{};
    bool initialized_{false};
};

} // namespace reaktio::games::space_slice
