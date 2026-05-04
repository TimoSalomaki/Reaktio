#pragma once

#include "reaktio/gameplay/IGameMode.hpp"

namespace reaktio::games::templates {

class StarterMode final : public gameplay::IGameMode {
  public:
    [[nodiscard]] static const gameplay::ModeDescriptor& mode_descriptor() noexcept;

    [[nodiscard]] const gameplay::ModeDescriptor& descriptor() const noexcept override;
    void on_enter(gameplay::IModeHost& host, const gameplay::ModeEnterContext& context) override;
    void on_fixed_step(gameplay::IModeHost& host, const gameplay::ModeFixedStepContext& context) override;
    void on_render_extract(gameplay::IModeHost& host, const gameplay::ModeRenderContext& context) override;
    void on_exit(gameplay::IModeHost& host, const gameplay::ModeExitContext& context) override;
};

} // namespace reaktio::games::templates