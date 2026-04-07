#include "reaktio/games/reference/ReferenceSandboxMode.hpp"

#include "reaktio/foundation/Telemetry.hpp"
#include "reaktio/gameplay/IModeHost.hpp"
#include "reaktio/gameplay/Transport.hpp"
#include "reaktio/platform/InputSnapshot.hpp"
#include "reaktio/render/RenderCamera.hpp"
#include "reaktio/render/RenderExtraction.hpp"

#include <sstream>

namespace reaktio::games::reference {

namespace {

const gameplay::ModeDescriptor k_descriptor{
    .id = "mode.reference.sandbox",
    .display_name = "Reference Sandbox",
    .description = "Reference mode that exercises lifecycle, input, transport stubs, and render extraction.",
};

std::uint32_t state_color(gameplay::TransportPlaybackState playback_state) noexcept {
    switch (playback_state) {
    case gameplay::TransportPlaybackState::Playing:
        return 0x1f4d2cff;
    case gameplay::TransportPlaybackState::Paused:
        return 0x7f5a1fff;
    case gameplay::TransportPlaybackState::Stopped:
        return 0x4a2430ff;
    }

    return 0x16324cff;
}

} // namespace

const gameplay::ModeDescriptor& ReferenceSandboxMode::mode_descriptor() noexcept {
    return k_descriptor;
}

const gameplay::ModeDescriptor& ReferenceSandboxMode::descriptor() const noexcept {
    return k_descriptor;
}

void ReferenceSandboxMode::on_enter(gameplay::IModeHost& host) {
    fixed_steps_ = 0;

    gameplay::ITransportControl& transport = host.transport();
    transport.stop();
    transport.set_loop_region(0.75, 1.25);
    transport.play();
}

void ReferenceSandboxMode::on_fixed_step(gameplay::IModeHost& host, double) {
    ++fixed_steps_;

    gameplay::ITransportControl& transport = host.transport();
    if (fixed_steps_ == 2) {
        transport.pause();
    } else if (fixed_steps_ == 3) {
        transport.seek(0.50);
    } else if (fixed_steps_ == 4) {
        transport.play();
    } else if (fixed_steps_ == 5) {
        transport.clear_loop_region();
    } else if (fixed_steps_ == 6) {
        transport.set_loop_region(1.0, 1.4);
    } else if (fixed_steps_ == 7) {
        transport.restart();
    }

    foundation::TelemetrySnapshot snapshot{};
    snapshot.audio_drift_ms = 0.00;
    snapshot.visible_cues = 3;
    snapshot.draw_calls = 0;
    host.telemetry().record(snapshot);
}

void ReferenceSandboxMode::on_render_extract(gameplay::IModeHost& host, double interpolation_alpha) {
    const gameplay::TransportSnapshot& transport_snapshot = host.transport().snapshot();
    const platform::InputSnapshot& input_snapshot = host.input_snapshot();

    host.render_extraction().set_view_camera(
        reaktio::render::RenderView::MainScene,
        reaktio::render::OrthographicCamera2D{
            .center = {0.0f, 0.0f},
            .virtual_height = 720.0f,
            .view_distance = 10.0f,
            .near_plane = 0.0f,
            .far_plane = 100.0f,
        });
    host.render_extraction().set_main_scene_clear(state_color(transport_snapshot.playback_state));

    std::ostringstream state_stream;
    state_stream << "transport=" << gameplay::to_string(transport_snapshot.playback_state) << " pos="
                 << transport_snapshot.position_seconds << '/' << transport_snapshot.duration_seconds << "s alpha="
                 << interpolation_alpha;
    host.render_extraction().add_debug_text(0, 8, 0x0f, state_stream.str());

    std::ostringstream loop_stream;
    loop_stream << "loop=" << transport_snapshot.loop_region.enabled << " ["
                << transport_snapshot.loop_region.start_seconds << ", "
                << transport_snapshot.loop_region.end_seconds << "] loops="
                << transport_snapshot.completed_loops << " fixed-steps="
                << transport_snapshot.advanced_fixed_steps;
    host.render_extraction().add_debug_text(0, 9, 0x0e, loop_stream.str());

    std::ostringstream input_stream;
    input_stream << "keys=" << input_snapshot.keyboard_events().size() << " mouse="
                 << input_snapshot.mouse_button_events().size() << " text="
                 << input_snapshot.text_input_events().size() << " gamepads="
                 << input_snapshot.connected_gamepads().size();
    host.render_extraction().add_debug_text(0, 10, 0x0a, input_stream.str());
}

void ReferenceSandboxMode::on_exit(gameplay::IModeHost& host) {
    host.transport().stop();
}

} // namespace reaktio::games::reference