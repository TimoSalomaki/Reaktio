#include "reaktio/games/templates/StarterMode.hpp"

#include "reaktio/foundation/Telemetry.hpp"
#include "reaktio/gameplay/IModeHost.hpp"

namespace reaktio::games::templates {

namespace {

const gameplay::ModeDescriptor k_descriptor{
    .id = "mode.template.starter",
    .display_name = "Starter Mode",
    .description = "Minimal reference mode used to validate the mode-host contract.",
};

} // namespace

const gameplay::ModeDescriptor& StarterMode::mode_descriptor() noexcept {
    return k_descriptor;
}

const gameplay::ModeDescriptor& StarterMode::descriptor() const noexcept {
    return k_descriptor;
}

void StarterMode::on_enter(gameplay::IModeHost&) {}

void StarterMode::on_fixed_step(gameplay::IModeHost& host, double) {
    foundation::TelemetrySnapshot snapshot{};
    snapshot.audio_drift_ms = 0.00;
    snapshot.draw_calls = 1;
    snapshot.visible_cues = 1;

    host.telemetry().record(snapshot);
}

void StarterMode::on_render_extract(gameplay::IModeHost&, double) {}

void StarterMode::on_exit(gameplay::IModeHost&) {}

} // namespace reaktio::games::templates