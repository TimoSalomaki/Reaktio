#pragma once

#include "reaktio/foundation/ResourceRegistry.hpp"
#include "reaktio/gameplay/IGameMode.hpp"
#include "reaktio/gameplay/MotionCollision.hpp"
#include "reaktio/gameplay/Transforms.hpp"
#include "reaktio/rhythm/CueTravelModel.hpp"
#include "reaktio/rhythm/TempoMap.hpp"
#include "reaktio/rhythm/TimingJudgement.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace reaktio::games::reference {

class ReferenceSandboxMode final : public gameplay::IGameMode {
  public:
    [[nodiscard]] static const gameplay::ModeDescriptor& mode_descriptor() noexcept;

    [[nodiscard]] const gameplay::ModeDescriptor& descriptor() const noexcept override;
    void on_enter(gameplay::IModeHost& host) override;
    void on_fixed_step(gameplay::IModeHost& host, double fixed_delta_seconds) override;
    void on_render_extract(gameplay::IModeHost& host, double interpolation_alpha) override;
    void on_exit(gameplay::IModeHost& host) override;

  private:
    std::uint64_t fixed_steps_{};
    std::uint32_t transport_roll_{};
    std::uint32_t visual_roll_{};
    std::string configured_transport_pause_binding_{"keyboard:Space"};
    std::string configured_transport_restart_binding_{"keyboard:R"};
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
    rhythm::TimingWindowSet judgement_window_set_{};
    rhythm::TimingOffsetProfile judgement_offset_profile_{};
    rhythm::TimingJudgementResult nearest_judgement_{};
    double nearest_cue_timing_error_ms_{};
    std::size_t visible_scheduled_cue_count_{};
    gameplay::MotionIntegrationReport motion_report_{};
    gameplay::CollisionDetectionReport collision_report_{};
    gameplay::TransformPropagationReport propagation_report_{};
};

} // namespace reaktio::games::reference