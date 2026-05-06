#include "reaktio/games/rail_slice/RailSliceMode.hpp"

#include "reaktio/foundation/CrashSafeLog.hpp"
#include "reaktio/gameplay/GameplayInput.hpp"
#include "reaktio/gameplay/IModeHost.hpp"
#include "reaktio/gameplay/ModeFlow.hpp"
#include "reaktio/gameplay/PresentationEvents.hpp"
#include "reaktio/gameplay/ReplayRecorder.hpp"
#include "reaktio/gameplay/SaveData.hpp"
#include "reaktio/platform/FrameClock.hpp"
#include "reaktio/render/RenderCamera.hpp"
#include "reaktio/render/RenderExtraction.hpp"

#include <algorithm>
#include <array>
#include <sstream>

namespace reaktio::games::rail_slice {

namespace {

const gameplay::ModeDescriptor k_descriptor = []() {
    gameplay::ModeDescriptor descriptor{};
    descriptor.id = "mode.rail.slice";
    descriptor.display_name = "Rail Slice";
    descriptor.family = "rail";
    descriptor.description =
        "Vertical-slice rail/lane/runner mode validating path-driven motion, "
        "deterministic obstacle/hit-scan/projectile/env-trigger primitives, and "
        "shared-stack scoring.";
    descriptor.capabilities =
        gameplay::ModeCapabilities::UsesActionInput |
        gameplay::ModeCapabilities::UsesTransport |
        gameplay::ModeCapabilities::EmitsRenderPackets |
        gameplay::ModeCapabilities::RecordsReplay |
        gameplay::ModeCapabilities::SupportsPractice;
    return descriptor;
}();

[[nodiscard]] rhythm::TempoMap make_default_tempo_map() {
    rhythm::TempoMap map{};
    rhythm::TempoMapDefinition definition{};
    definition.config.ticks_per_quarter_note = 480;
    definition.config.sample_rate_hz = 48000;
    definition.tempo_changes.push_back(
        rhythm::TempoChange{.start_tick = 0, .microseconds_per_quarter_note = 500000});
    definition.time_signature_changes.push_back(
        rhythm::TimeSignatureChange{.start_tick = 0, .numerator = 4, .denominator = 4});
    map.rebuild(std::move(definition));
    return map;
}

[[nodiscard]] gameplay::RailPath make_default_rail_path() {
    gameplay::RailPath path{};
    // Long straight runner segment with a gentle right turn near the end so
    // the camera rig + lateral collision frames are exercised.
    (void)path.rebuild({
        gameplay::RailPathControlPoint{.position = {0.0f, 0.0f, 0.0f}},
        gameplay::RailPathControlPoint{.position = {0.0f, 0.0f, 30.0f}},
        gameplay::RailPathControlPoint{.position = {0.0f, 0.0f, 60.0f}},
        gameplay::RailPathControlPoint{.position = {5.0f, 0.0f, 70.0f}},
        gameplay::RailPathControlPoint{.position = {15.0f, 0.0f, 75.0f}},
        gameplay::RailPathControlPoint{.position = {30.0f, 0.0f, 80.0f}},
    });
    return path;
}

// Hand-authored deterministic chart: one cue per beat, alternating channels
// so cues fan out across the lane layout. The smoke validates that the
// shared CueScheduler + RailChart adapter produce the expected active-cue
// counts as the player advances along the rail.
[[nodiscard]] std::vector<rhythm::ScheduledCue> make_default_schedule() {
    std::vector<rhythm::ScheduledCue> cues;
    cues.reserve(16);
    for (std::uint32_t i = 0; i < 16; ++i) {
        rhythm::ScheduledCue cue{};
        cue.hit_tick = static_cast<rhythm::ChartTick>(960 + i * 480);
        cue.channel_index = i % 4u;
        cues.push_back(cue);
    }
    return cues;
}

[[nodiscard]] std::vector<gameplay::RailObstacle> make_default_obstacle_layout() {
    std::vector<gameplay::RailObstacle> obstacles;
    // Hazard wall at lane 0, arc 12: forces a lane-swap or dodge.
    obstacles.push_back(gameplay::RailObstacle{
        .obstacle_id = 1,
        .arc_length = 12.0,
        .arc_length_half_extent = 1.0,
        .signed_lane_min = 0,
        .signed_lane_max = 0,
        .flags = static_cast<std::uint32_t>(gameplay::RailObstacleFlag::Hazard) |
                 gameplay::RailObstacleFlag::Solid,
    });
    // Pickup on lane +1, arc 18.
    obstacles.push_back(gameplay::RailObstacle{
        .obstacle_id = 2,
        .arc_length = 18.0,
        .arc_length_half_extent = 0.5,
        .signed_lane_min = 1,
        .signed_lane_max = 1,
        .flags = static_cast<std::uint32_t>(gameplay::RailObstacleFlag::Pickup),
    });
    // Shootable enemy on lane 0, arc 25.
    obstacles.push_back(gameplay::RailObstacle{
        .obstacle_id = 3,
        .arc_length = 25.0,
        .arc_length_half_extent = 0.5,
        .signed_lane_min = 0,
        .signed_lane_max = 0,
        .flags = static_cast<std::uint32_t>(gameplay::RailObstacleFlag::Shootable),
        .hit_points = 1,
    });
    // Shootable enemy on lane -1, arc 32.
    obstacles.push_back(gameplay::RailObstacle{
        .obstacle_id = 4,
        .arc_length = 32.0,
        .arc_length_half_extent = 0.5,
        .signed_lane_min = -1,
        .signed_lane_max = -1,
        .flags = static_cast<std::uint32_t>(gameplay::RailObstacleFlag::Shootable),
        .hit_points = 1,
    });
    return obstacles;
}

[[nodiscard]] std::vector<gameplay::RailEnvTrigger> make_default_env_triggers() {
    return {
        gameplay::RailEnvTrigger{.trigger_id = 1, .arc_length = 6.0, .kind_tag = 1},
        gameplay::RailEnvTrigger{.trigger_id = 2, .arc_length = 20.0, .kind_tag = 2},
        gameplay::RailEnvTrigger{.trigger_id = 3, .arc_length = 36.0, .kind_tag = 1},
    };
}

} // namespace

const gameplay::ModeDescriptor& RailSliceMode::mode_descriptor() noexcept { return k_descriptor; }

RailSliceMode::RailSliceMode() : RailSliceMode(RailSliceConfig{}) {}

RailSliceMode::RailSliceMode(RailSliceConfig config) : config_(std::move(config)) {
    if (config_.lane_layout.lane_count <= 0) {
        config_.lane_layout.lane_count = 5;
    }
    if (config_.lane_swap_config.lane_min == config_.lane_swap_config.lane_max) {
        const std::int32_t half = (config_.lane_layout.lane_count - 1) / 2;
        config_.lane_swap_config.lane_min = -half;
        config_.lane_swap_config.lane_max = half;
    }
    // Default hold rule range targets a window around the third pickup so
    // smoke verifiers can exercise the hold lifecycle.
    if (config_.hold_rule_config.start_arc_length == 0.0 &&
        config_.hold_rule_config.end_arc_length == 0.0) {
        config_.hold_rule_config.start_arc_length = 16.0;
        config_.hold_rule_config.end_arc_length = 22.0;
    }
}

const gameplay::ModeDescriptor& RailSliceMode::descriptor() const noexcept { return k_descriptor; }

void RailSliceMode::ensure_initialized(gameplay::IModeHost& host) {
    (void)host;
    if (initialized_) {
        return;
    }

    path_ = make_default_rail_path();
    scheduled_cues_ = make_default_schedule();

    gameplay::RailChartConfig chart_config{};
    chart_config.lane_layout = config_.lane_layout;
    // FixedWorld placement: chart cues are pinned at static arc positions
    // along the rail and the player is the moving frame of reference. This
    // is the natural runner/lane mental model and keeps cue judgement
    // entirely arc-length-driven (deterministic across replays even when
    // audio transport state varies).
    chart_config.cue_placement = gameplay::RailChartConfig::CuePlacement::FixedWorld;
    chart_config.arc_length_per_tick = 0.012;
    chart_config.travel_lead_ticks = 1920;
    chart_config.judge_arc_length = path_.total_length();
    chart_ = gameplay::make_rail_chart(scheduled_cues_, chart_config);

    obstacles_.rebuild(make_default_obstacle_layout());
    env_triggers_.rebuild(make_default_env_triggers());

    tempo_map_ = make_default_tempo_map();
    timing_windows_ = rhythm::make_default_timing_window_set();

    cue_scheduler_.reset();
    scoring_.reset();

    player_ = gameplay::RailPlayerState{};
    player_.signed_lane = 0;
    lane_swap_state_ = gameplay::LaneSwapRuleState{};
    vertical_action_state_ = gameplay::VerticalActionRuleState{};
    shoot_state_ = gameplay::ShootRuleState{};
    dodge_state_ = gameplay::DodgeRuleState{};
    hold_state_ = gameplay::HoldRuleState{};
    projectiles_.clear();
    spatial_cue_buffer_.clear();
    chart_cue_judged_.assign(chart_.cues.size(), static_cast<std::uint8_t>(0));
    projectile_hits_ = 0;
    hazard_hits_ = 0;
    pickups_collected_ = 0;
    chart_cues_judged_ = 0;
    spatial_cue_sample_count_ = 0;
    presentation_events_emitted_ = 0;

    initialized_ = true;
}

void RailSliceMode::on_enter(gameplay::IModeHost& host, const gameplay::ModeEnterContext& context) {
    ensure_initialized(host);
    host.flow().begin(gameplay::ModeFlowReason::EnterMode);

    if (config_.record_save_data) {
        gameplay::SaveSetting setting{};
        setting.key = "last_played_mode";
        setting.value.kind = gameplay::SaveSettingValueKind::Text;
        setting.value.text_value = std::string(descriptor().id);
        host.save_data().upsert_setting(
            "profile", std::move(setting), host.frame_timing().wall_clock_ns);
        (void)host.save_data().grant_unlock(
            "reaktio.rail.first_run",
            std::string(descriptor().id),
            host.frame_timing().wall_clock_ns);
    }

    ++lifecycle_event_count_;
    (void)context;
}

void RailSliceMode::apply_input(gameplay::IModeHost& host, double fixed_delta_seconds) {
    const gameplay::ActionInputSurface& actions = host.input().actions();

    const bool left_pressed = actions.was_pressed(k_rail_action_context, k_rail_action_lane_left) ||
                              actions.was_pressed(k_rail_action_lane_left);
    const bool right_pressed = actions.was_pressed(k_rail_action_context, k_rail_action_lane_right) ||
                               actions.was_pressed(k_rail_action_lane_right);
    const bool jump_pressed = actions.was_pressed(k_rail_action_context, k_rail_action_jump) ||
                              actions.was_pressed(k_rail_action_jump);
    const bool slide_pressed = actions.was_pressed(k_rail_action_context, k_rail_action_slide) ||
                               actions.was_pressed(k_rail_action_slide);
    const bool fire_pressed = actions.was_pressed(k_rail_action_context, k_rail_action_fire) ||
                              actions.was_pressed(k_rail_action_fire);
    const bool dodge_pressed = actions.was_pressed(k_rail_action_context, k_rail_action_dodge) ||
                               actions.was_pressed(k_rail_action_dodge);
    const bool hold_down = actions.is_down(k_rail_action_context, k_rail_action_hold) ||
                           actions.is_down(k_rail_action_hold);

    gameplay::tick_lane_swap_rule(
        config_.lane_swap_config,
        lane_swap_state_,
        gameplay::LaneSwapInput{.swap_left = left_pressed, .swap_right = right_pressed},
        player_,
        fixed_delta_seconds);

    gameplay::tick_vertical_action_rule(
        config_.vertical_action_config,
        vertical_action_state_,
        gameplay::VerticalActionInput{.jump = jump_pressed, .slide = slide_pressed},
        player_,
        fixed_delta_seconds);

    gameplay::tick_dodge_rule(
        config_.dodge_config,
        dodge_state_,
        gameplay::DodgeInput{.dodge = dodge_pressed},
        player_,
        fixed_delta_seconds);

    gameplay::tick_shoot_rule(
        config_.shoot_config,
        shoot_state_,
        gameplay::ShootInput{.fire = fire_pressed},
        player_,
        projectiles_,
        fixed_delta_seconds);

    gameplay::tick_hold_rule(
        config_.hold_rule_config, hold_state_, hold_down, player_.arc_length);
}

void RailSliceMode::advance_player(double fixed_delta_seconds) {
    player_.arc_length_velocity = config_.player_arc_velocity;
    player_.arc_length += player_.arc_length_velocity * fixed_delta_seconds;
    if (path_.valid() && player_.arc_length > path_.total_length()) {
        player_.arc_length = path_.total_length();
    }
}

void RailSliceMode::resolve_world_interactions(
    gameplay::IModeHost& host, const gameplay::ModeFixedStepContext& context) {
    // Sweep projectiles vs obstacles. The field owns HP bookkeeping; we
    // forward the projectile's damage and only score / increment the
    // hit-counter on the alive->destroyed transition so multi-HP obstacles
    // don't multiply rewards while being chipped down.
    projectile_hit_buffer_.clear();
    gameplay::advance_projectiles(
        projectiles_, obstacles_, context.fixed_delta_seconds, projectile_hit_buffer_);
    for (const gameplay::RailProjectileHit& hit : projectile_hit_buffer_) {
        const bool destroyed = obstacles_.register_hit(hit.obstacle_index, hit.damage);
        if (!destroyed) {
            continue;
        }
        ++projectile_hits_;
        gameplay::ScoreJudgementEvent event{};
        event.cue_id = hit.obstacle_id;
        event.cue_hit_tick = static_cast<rhythm::ChartTick>(context.fixed_step_index);
        event.judgement.judgement = rhythm::TimingJudgement::Perfect;
        scoring_.record_judgement(event);
    }
    projectiles_.erase(
        std::remove_if(
            projectiles_.begin(),
            projectiles_.end(),
            [](const gameplay::RailProjectile& p) { return !p.active; }),
        projectiles_.end());

    // Player overlap vs obstacle field. Pickups force-destroy and award
    // score; hazards consume a miss when not invulnerable. Both gates use
    // the field's idempotent transition return value so a single overlap
    // frame can never double-count.
    overlap_buffer_.clear();
    obstacles_.query_overlap_point(
        player_.arc_length, player_.signed_lane, overlap_buffer_);
    const bool invulnerable = (player_.status_flags &
                               config_.dodge_config.invulnerable_status_flag) != 0u;
    for (std::size_t obstacle_index : overlap_buffer_) {
        const gameplay::RailObstacle& obstacle = obstacles_.obstacles()[obstacle_index];
        if (gameplay::has_flag(obstacle.flags, gameplay::RailObstacleFlag::Pickup)) {
            if (!obstacles_.mark_destroyed(obstacle_index)) {
                continue;  // Already collected this frame or earlier.
            }
            ++pickups_collected_;
            gameplay::ScoreJudgementEvent event{};
            event.cue_id = obstacle.obstacle_id;
            event.cue_hit_tick = static_cast<rhythm::ChartTick>(context.fixed_step_index);
            event.judgement.judgement = rhythm::TimingJudgement::Great;
            scoring_.record_judgement(event);
            continue;
        }
        if (gameplay::has_flag(obstacle.flags, gameplay::RailObstacleFlag::Hazard) && !invulnerable) {
            if (obstacles_.is_destroyed(obstacle_index)) {
                continue;
            }
            ++hazard_hits_;
            gameplay::ScoreJudgementEvent event{};
            event.cue_id = obstacle.obstacle_id;
            event.cue_hit_tick = static_cast<rhythm::ChartTick>(context.fixed_step_index);
            event.judgement.judgement = rhythm::TimingJudgement::Miss;
            scoring_.record_judgement(event);
            const bool emitted = host.presentation_events().publish(
                gameplay::ScreenEffectEvent{
                    .kind = gameplay::ScreenEffectKind::Flash,
                    .intensity = 1.0f,
                    .frame_index = context.frame_index,
                });
            if (emitted) {
                ++presentation_events_emitted_;
            }
        }
    }

    // Env trigger fan-out. Modes choose their own presentation effect for
    // each kind_tag; here we publish a ColorPulse for kind 1 (lights) and
    // a Flash for kind 2 (hazard pulse) so verifiers can confirm the fan-out
    // ran end-to-end through the shared presentation bus.
    trigger_buffer_.clear();
    env_triggers_.advance_to_arc_length(player_.arc_length, trigger_buffer_);
    for (const gameplay::RailEnvTrigger& trigger : trigger_buffer_) {
        gameplay::ScreenEffectEvent effect{};
        effect.frame_index = context.frame_index;
        effect.intensity = 0.5f;
        effect.kind = trigger.kind_tag == 2u
            ? gameplay::ScreenEffectKind::Flash
            : gameplay::ScreenEffectKind::ColorPulse;
        if (host.presentation_events().publish(effect)) {
            ++presentation_events_emitted_;
        }
    }

    // Replay sample submission for the mode-level engagement metric. Modes
    // can disable this via record_replay_samples (e.g. when running off-band
    // as a smoke shutdown verifier).
    if (config_.record_replay_samples) {
        host.replay().record_judgement_sample(gameplay::ReplayJudgementSample{
            .frame_index = context.frame_index,
            .simulation_step = context.fixed_step_index,
            .schedule_index = projectile_hits_ + pickups_collected_,
            .channel_index = static_cast<std::uint32_t>(player_.signed_lane + 8),
            .judgement = rhythm::TimingJudgement::Great,
            .raw_error_microseconds = 0,
            .corrected_error_microseconds = 0,
            .applied_offset_microseconds = 0,
        });
    }
}

void RailSliceMode::resolve_chart_cues(
    gameplay::IModeHost& host, const gameplay::ModeFixedStepContext& context) {
    // Translate transport seconds into a chart tick through the shared
    // tempo map and then drive the engine-layer RailChart adapter. This is
    // the rail-family equivalent of how the typing slice consumes its
    // TypingChart output: same scheduler, same chart contract, same scoring
    // path -- only the spatial projection differs. Modes that ship dense
    // chart cues will replace this with their own per-cue interaction
    // rules; here we use a simple "chart cues that cross the judge line
    // unhandled count as Misses" policy that exercises the integration
    // end-to-end without forcing a particular interaction model.
    if (!tempo_map_.valid()) {
        return;
    }
    if (chart_cue_judged_.size() != chart_.cues.size()) {
        chart_cue_judged_.assign(chart_.cues.size(), static_cast<std::uint8_t>(0));
    }

    const gameplay::TransportSnapshot snapshot = host.transport().snapshot();
    const rhythm::TimelineMicroseconds position_us = static_cast<rhythm::TimelineMicroseconds>(
        snapshot.position_seconds > 0.0 ? snapshot.position_seconds * 1'000'000.0 : 0.0);
    const rhythm::ChartTick current_tick = tempo_map_.tick_from_microseconds(position_us);

    spatial_cue_buffer_.clear();
    gameplay::resolve_spatial_cues(
        chart_, path_, current_tick, cue_scheduler_.active_cues(), spatial_cue_buffer_);
    spatial_cue_sample_count_ = static_cast<std::uint32_t>(spatial_cue_buffer_.size());

    for (const gameplay::SpatialCueSample& sample : spatial_cue_buffer_) {
        if (sample.schedule_index >= chart_cue_judged_.size()) {
            continue;
        }
        if (chart_cue_judged_[sample.schedule_index] != 0u) {
            continue;
        }
        // Rail-family modes judge cues spatially: a cue is "passed" when
        // the player's arc_length crosses the cue's arc position. This is
        // the natural model for runner/lane gameplay (the cue physically
        // reaches the player) and keeps replay deterministic because
        // arc_length is driven by fixed-step kinematics, not by audio
        // wall-clock. Modes that score cues on press will mark
        // chart_cue_judged_[schedule_index] earlier and skip the miss
        // branch.
        if (player_.arc_length < sample.cue_arc_length) {
            continue;
        }
        chart_cue_judged_[sample.schedule_index] = static_cast<std::uint8_t>(1);
        ++chart_cues_judged_;

        gameplay::ScoreJudgementEvent event{};
        event.cue_id = static_cast<std::uint64_t>(sample.schedule_index + 1);
        event.schedule_index = sample.schedule_index;
        event.cue_hit_tick = sample.hit_tick;
        event.channel_index = static_cast<std::uint32_t>(sample.signed_lane_index + 8);
        event.judgement.judgement = rhythm::TimingJudgement::Miss;
        scoring_.record_judgement(event);
    }
    (void)context;
}

void RailSliceMode::on_fixed_step(
    gameplay::IModeHost& host, const gameplay::ModeFixedStepContext& context) {
    ensure_initialized(host);

    if (tempo_map_.valid()) {
        gameplay::CueSchedulerUpdateInput input{};
        input.tempo_map = &tempo_map_;
        const gameplay::TransportSnapshot snapshot = host.transport().snapshot();
        input.transport = &snapshot;
        input.schedule = std::span<const rhythm::ScheduledCue>(scheduled_cues_);
        // Rail-family modes lay out chart cues along fixed arc lengths.
        // The spatial projection is meaningful regardless of whether the
        // audio transport is currently playing (e.g. during paused
        // practice loops or a tooling preview). schedule_when_stopped
        // keeps the active cue set populated so render extraction and
        // off-line validation tooling continue to receive samples.
        // FixedWorld charts also need a wide spawn window so cues remain
        // active long enough for the player's arc-length sweep to cross
        // them; the scheduler default is tuned for short-window scrolling
        // highways. Modes that author dense charts can narrow this back.
        gameplay::CueSchedulerRules rules{};
        rules.schedule_when_stopped = true;
        if (chart_.config.cue_placement ==
            gameplay::RailChartConfig::CuePlacement::FixedWorld) {
            rules.spawn_window.pre_hit_visible_ticks =
                static_cast<rhythm::ChartTick>(1'000'000);
            rules.spawn_window.post_hit_visible_ticks =
                static_cast<rhythm::ChartTick>(1'000'000);
        }
        input.rules = rules;
        cue_scheduler_.update(input);
    }

    apply_input(host, context.fixed_delta_seconds);
    advance_player(context.fixed_delta_seconds);
    resolve_world_interactions(host, context);
    resolve_chart_cues(host, context);

    if (player_.arc_length >= path_.total_length() - 1e-3) {
        scoring_.mark_cleared();
        if (host.flow().can_succeed()) {
            host.flow().succeed(gameplay::ModeFlowReason::SongEnded);
        }
    }
}

void RailSliceMode::on_render_extract(
    gameplay::IModeHost& host, const gameplay::ModeRenderContext& context) {
    if (!initialized_) {
        return;
    }

    // Sample a rail camera rig that follows the player. Render extraction
    // is the only place the slice touches the renderer; everything else is
    // simulation.
    gameplay::RailCameraRig rig{};
    rig.look_at_arc_length = std::min(player_.arc_length + 4.0, path_.total_length());
    rig.follow_distance = 6.0;
    rig.lateral_offset = 0.0;
    rig.vertical_offset = 1.5;
    const gameplay::RailCameraSample camera = gameplay::sample_rail_camera(path_, rig);

    host.render_extraction().set_view_camera(
        render::RenderView::MainScene,
        render::PerspectiveCamera25D{
            .eye = {camera.eye.x, camera.eye.y, camera.eye.z},
            .target = {camera.target.x, camera.target.y, camera.target.z},
            .up = {camera.up.x, camera.up.y, camera.up.z},
            .vertical_fov_radians = static_cast<float>(camera.field_of_view_radians),
            .aspect_ratio_override = 0.0f,
            .near_plane = static_cast<float>(camera.near_plane),
            .far_plane = static_cast<float>(camera.far_plane),
        });

    std::ostringstream player_line;
    player_line << "Rail slice: arc=" << player_.arc_length
                << "/" << path_.total_length()
                << " lane=" << player_.signed_lane
                << " v-offset=" << player_.vertical_offset
                << " status=" << player_.status_flags;
    host.render_extraction().add_debug_text(0, 8, 0x0a, player_line.str());

    std::ostringstream interaction_line;
    interaction_line << "swaps=" << lane_swap_state_.swap_count
                     << " jumps=" << vertical_action_state_.jump_count
                     << " slides=" << vertical_action_state_.slide_count
                     << " dodges=" << dodge_state_.dodge_count
                     << " shots=" << shoot_state_.emitted_count
                     << " hits=" << projectile_hits_
                     << " pickups=" << pickups_collected_
                     << " hazards=" << hazard_hits_;
    host.render_extraction().add_debug_text(0, 9, 0x0e, interaction_line.str());

    (void)context;
}

void RailSliceMode::on_exit(gameplay::IModeHost& host, const gameplay::ModeExitContext& context) {
    if (!initialized_) {
        return;
    }

    const gameplay::ScoreSummary summary = scoring_.summary();
    if (host.flow().can_present_results()) {
        host.flow().present_results(summary, "mode.rail.slice.exit");
    }

    if (config_.record_save_data) {
        gameplay::SaveModeStatsResult result{};
        result.mode_id = std::string(descriptor().id);
        result.song_id = "reaktio.rail.intro";
        result.cleared = summary.run_state == gameplay::ScoreRunState::Cleared;
        result.failed = summary.run_state == gameplay::ScoreRunState::Failed;
        result.score = summary.score;
        result.combo = summary.max_combo;
        result.accuracy_ratio = summary.accuracy_ratio;
        result.grade = std::string(gameplay::to_string(summary.grade));
        result.play_time_ms = static_cast<std::uint64_t>(
            host.transport().snapshot().position_seconds > 0.0
                ? host.transport().snapshot().position_seconds * 1000.0
                : 0.0);
        result.wall_clock_ns = host.frame_timing().wall_clock_ns;
        host.save_data().record_mode_session(result);
    }

    ++lifecycle_event_count_;
    (void)context;
}

const gameplay::RailPath& RailSliceMode::path() const noexcept { return path_; }
const gameplay::RailChart& RailSliceMode::chart() const noexcept { return chart_; }
const gameplay::RailObstacleField& RailSliceMode::obstacles() const noexcept { return obstacles_; }
const gameplay::RailPlayerState& RailSliceMode::player_state() const noexcept { return player_; }
const gameplay::ScoreTracker& RailSliceMode::scoring() const noexcept { return scoring_; }
const gameplay::CueScheduler& RailSliceMode::cue_scheduler() const noexcept { return cue_scheduler_; }
const rhythm::TempoMap& RailSliceMode::tempo_map() const noexcept { return tempo_map_; }
const gameplay::LaneSwapRuleState& RailSliceMode::lane_swap_state() const noexcept { return lane_swap_state_; }
const gameplay::VerticalActionRuleState& RailSliceMode::vertical_action_state() const noexcept {
    return vertical_action_state_;
}
const gameplay::ShootRuleState& RailSliceMode::shoot_state() const noexcept { return shoot_state_; }
const gameplay::DodgeRuleState& RailSliceMode::dodge_state() const noexcept { return dodge_state_; }
const gameplay::HoldRuleState& RailSliceMode::hold_state() const noexcept { return hold_state_; }
std::uint32_t RailSliceMode::projectile_hits() const noexcept { return projectile_hits_; }
std::uint32_t RailSliceMode::hazard_hits() const noexcept { return hazard_hits_; }
std::uint32_t RailSliceMode::pickups_collected() const noexcept { return pickups_collected_; }
std::uint32_t RailSliceMode::chart_cues_judged() const noexcept { return chart_cues_judged_; }
std::uint32_t RailSliceMode::spatial_cue_sample_count() const noexcept {
    return spatial_cue_sample_count_;
}
std::uint32_t RailSliceMode::presentation_events_emitted() const noexcept {
    return presentation_events_emitted_;
}
std::uint32_t RailSliceMode::lifecycle_event_count() const noexcept { return lifecycle_event_count_; }

} // namespace reaktio::games::rail_slice
