#pragma once

#include "reaktio/gameplay/IGameMode.hpp"

namespace reaktio::games::templates {

class StarterMode final : public gameplay::IGameMode {
  public:
    [[nodiscard]] static const gameplay::ModeDescriptor& mode_descriptor() noexcept;

    [[nodiscard]] const gameplay::ModeDescriptor& descriptor() const noexcept override;
    void on_enter(gameplay::IModeHost& host) override;
    void on_fixed_step(gameplay::IModeHost& host, double) override;
    void on_render_extract(gameplay::IModeHost& host, double interpolation_alpha) override;
    void on_exit(gameplay::IModeHost& host) override;
};

} // namespace reaktio::games::templates