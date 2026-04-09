#pragma once

#include "reaktio/gameplay/IGameMode.hpp"
#include "reaktio/gameplay/MotionCollision.hpp"
#include "reaktio/gameplay/Transforms.hpp"

#include <cstddef>
#include <cstdint>

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
    std::size_t world_entity_count_{};
    float average_phase_{};
    float sample_cue_world_x_{};
    gameplay::Vector3 sample_tip_world_{};
    std::uint64_t collision_signature_{};
    std::uint64_t last_published_collision_topology_{static_cast<std::uint64_t>(-1)};
    gameplay::MotionIntegrationReport motion_report_{};
    gameplay::CollisionDetectionReport collision_report_{};
    gameplay::TransformPropagationReport propagation_report_{};
};

} // namespace reaktio::games::reference