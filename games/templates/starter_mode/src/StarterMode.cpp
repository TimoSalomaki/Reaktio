#include "reaktio/games/templates/StarterMode.hpp"

#include "reaktio/foundation/Telemetry.hpp"
#include "reaktio/gameplay/IModeHost.hpp"
#include "reaktio/platform/InputSnapshot.hpp"
#include "reaktio/render/RenderExtraction.hpp"

#include <sstream>

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

void StarterMode::on_render_extract(gameplay::IModeHost& host, double interpolation_alpha) {
    host.render_extraction().set_main_scene_clear(0x16324cff);

    std::ostringstream interpolation_stream;
    interpolation_stream << "starter extraction alpha=" << interpolation_alpha;
    host.render_extraction().add_debug_text(0, 8, 0x0e, interpolation_stream.str());

    std::ostringstream input_stream;
    input_stream << "starter keys this frame=" << host.input_snapshot().keyboard_events().size();
    host.render_extraction().add_debug_text(0, 9, 0x0a, input_stream.str());
}

void StarterMode::on_exit(gameplay::IModeHost&) {}

} // namespace reaktio::games::templates