#pragma once

#include "reaktio/foundation/ResourceRegistry.hpp"
#include "reaktio/gameplay/CueScheduler.hpp"
#include "reaktio/gameplay/IGameMode.hpp"
#include "reaktio/gameplay/Modifiers.hpp"
#include "reaktio/gameplay/Scoring.hpp"
#include "reaktio/gameplay/MotionCollision.hpp"
#include "reaktio/gameplay/Transforms.hpp"
#include "reaktio/rhythm/CueTravelModel.hpp"
#include "reaktio/rhythm/LatencyCalibration.hpp"
#include "reaktio/rhythm/TempoMap.hpp"
#include "reaktio/rhythm/TimingJudgement.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reaktio::games::reference {

enum class CalibrationFlowMode : std::uint8_t {
  None,
  Output,
  Input,
};

[[nodiscard]] inline constexpr std::string_view to_string(CalibrationFlowMode mode) noexcept {
  switch (mode) {
  case CalibrationFlowMode::None:
    return "none";
  case CalibrationFlowMode::Output:
    return "output";
  case CalibrationFlowMode::Input:
    return "input";
  }

  return "unknown";
}

class ReferenceSandboxMode final : public gameplay::IGameMode {
  public:
    [[nodiscard]] static const gameplay::ModeDescriptor& mode_descriptor() noexcept;

    [[nodiscard]] const gameplay::ModeDescriptor& descriptor() const noexcept override;
    void on_enter(gameplay::IModeHost& host, const gameplay::ModeEnterContext& context) override;
    void on_frame_begin(gameplay::IModeHost& host, const gameplay::ModeFrameContext& context) override;
    void on_fixed_step(gameplay::IModeHost& host, const gameplay::ModeFixedStepContext& context) override;
    void on_render_extract(gameplay::IModeHost& host, const gameplay::ModeRenderContext& context) override;
    void on_exit(gameplay::IModeHost& host, const gameplay::ModeExitContext& context) override;

  private:
    std::uint64_t fixed_steps_{};
    std::uint32_t transport_roll_{};
    std::uint32_t visual_roll_{};
    std::string configured_transport_pause_binding_{"keyboard:Space"};
    std::string configured_transport_restart_binding_{"keyboard:R"};
    std::string configured_calibration_output_binding_{"keyboard:O"};
    std::string configured_calibration_input_binding_{"keyboard:I"};
    std::string configured_calibration_commit_binding_{"keyboard:Return"};
    std::string configured_calibration_clear_binding_{"keyboard:Backspace"};
    std::string configured_calibration_adjust_negative_binding_{"keyboard:Left"};
    std::string configured_calibration_adjust_positive_binding_{"keyboard:Right"};
    std::string configured_practice_speed_decrease_binding_{"keyboard:Z"};
    std::string configured_practice_speed_increase_binding_{"keyboard:X"};
    std::string configured_practice_speed_reset_binding_{"keyboard:C"};
    std::string configured_practice_loop_mark_start_binding_{"keyboard:J"};
    std::string configured_practice_loop_mark_end_binding_{"keyboard:K"};
    std::string configured_practice_loop_apply_binding_{"keyboard:L"};
    std::string configured_practice_loop_clear_binding_{"keyboard:U"};
    std::string configured_practice_offset_visualization_toggle_binding_{"keyboard:V"};
    float configured_velocity_scale_{1.0f};
    float configured_hit_window_half_width_{32.0f};
    float configured_hit_window_half_height_{24.0f};
    std::string configured_cue_material_authoring_id_{"reference.sandbox.material.cue"};
    std::string configured_debug_font_authoring_id_{"reference.sandbox.font.debug"};
    foundation::ResourceRegistrySummary resource_summary_{};
    foundation::ResourceHandle cue_texture_handle_{};
    std::string cue_texture_runtime_label_{};
    foundation::ResourceHandle cue_material_handle_{};
    std::string cue_material_runtime_label_{};
    foundation::ResourceHandle rig_mesh_handle_{};
    std::string rig_mesh_runtime_label_{};
    foundation::ResourceHandle debug_font_handle_{};
    std::string debug_font_runtime_label_{};
    bool stale_debug_font_borrow_valid_{};
    std::size_t world_entity_count_{};
    float average_phase_{};
    float sample_cue_world_x_{};
    gameplay::Vector3 sample_tip_world_{};
    std::uint64_t collision_signature_{};
    std::uint64_t collision_topology_{};
    std::uint64_t last_published_collision_topology_{static_cast<std::uint64_t>(-1)};
    std::uint64_t last_published_transport_revision_{};
    rhythm::TempoMap rhythm_tempo_map_{};
    rhythm::RhythmPosition rhythm_position_{};
    std::string rhythm_status_{"tempo-map=uninitialized"};
    std::vector<rhythm::ScheduledCue> scheduled_cues_{};
    gameplay::CueScheduler cue_scheduler_{};
    gameplay::CueSchedulerSummary cue_scheduler_summary_{};
    gameplay::ScoreTracker score_tracker_{};
    gameplay::ScoreSummary score_summary_{};
    std::vector<std::size_t> scored_schedule_indices_{};
    rhythm::TimingWindowSet judgement_window_set_{};
    rhythm::TimingOffsetProfile judgement_offset_profile_{};
    gameplay::ModifierSet modifier_snapshot_{};
    std::uint64_t modifier_signature_{};
    double practice_scroll_speed_multiplier_{1.0};
    bool practice_offset_visualization_enabled_{true};
    double practice_loop_marker_start_seconds_{};
    double practice_loop_marker_end_seconds_{};
    bool practice_loop_marker_start_set_{};
    bool practice_loop_marker_end_set_{};
    CalibrationFlowMode calibration_flow_mode_{CalibrationFlowMode::None};
    rhythm::LatencyCalibrationSession output_latency_calibration_{rhythm::LatencyCalibrationKind::AudioOutput};
    rhythm::LatencyCalibrationSession input_latency_calibration_{rhythm::LatencyCalibrationKind::InputResponse};
    rhythm::TimelineMicroseconds pending_output_offset_microseconds_{};
    rhythm::TimingJudgementResult nearest_judgement_{};
    double nearest_cue_timing_error_ms_{};
    std::size_t visible_scheduled_cue_count_{};
    gameplay::MotionIntegrationReport motion_report_{};
    gameplay::CollisionDetectionReport collision_report_{};
    gameplay::TransformPropagationReport propagation_report_{};
};

} // namespace reaktio::games::reference