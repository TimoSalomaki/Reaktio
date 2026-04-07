#pragma once

#include "reaktio/gameplay/IGameMode.hpp"

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
};

} // namespace reaktio::games::reference