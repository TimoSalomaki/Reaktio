#include "reaktio/games/templates/StarterMode.hpp"

#include "reaktio/foundation/Telemetry.hpp"
#include "reaktio/gameplay/GameplayInput.hpp"
#include "reaktio/gameplay/IModeHost.hpp"
#include "reaktio/platform/FrameClock.hpp"
#include "reaktio/platform/InputSnapshot.hpp"
#include "reaktio/render/RenderCamera.hpp"
#include "reaktio/render/RenderExtraction.hpp"

#include <sstream>

namespace reaktio::games::templates {

namespace {

const gameplay::ModeDescriptor k_descriptor{
    .id = "mode.template.starter",
    .display_name = "Starter Mode",
    .description = "Minimal reference mode used to validate the mode-host contract.",
    .family = "template",
    .capabilities = gameplay::ModeCapabilities::UsesActionInput |
        gameplay::ModeCapabilities::EmitsRenderPackets,
};

} // namespace

const gameplay::ModeDescriptor& StarterMode::mode_descriptor() noexcept {
    return k_descriptor;
}

const gameplay::ModeDescriptor& StarterMode::descriptor() const noexcept {
    return k_descriptor;
}

void StarterMode::on_enter(gameplay::IModeHost&, const gameplay::ModeEnterContext&) {}

void StarterMode::on_fixed_step(gameplay::IModeHost& host, const gameplay::ModeFixedStepContext&) {
    foundation::TelemetrySnapshot snapshot{};
    snapshot.audio_drift_ms = 0.00;
    snapshot.draw_calls = 1;
    snapshot.visible_cues = 1;

    host.telemetry().record(snapshot);
}

void StarterMode::on_render_extract(gameplay::IModeHost& host, const gameplay::ModeRenderContext& context) {
    const std::uint64_t frame_index = host.frame_timing().frame_index;
    const std::uint64_t camera_variant = frame_index % 3u;

    std::string_view camera_label = "orthographic-2d";
    if (camera_variant == 0u) {
        host.render_extraction().set_view_camera(
            reaktio::render::RenderView::MainScene,
            reaktio::render::OrthographicCamera2D{
                .center = {0.0f, 0.0f},
                .virtual_height = 720.0f,
                .view_distance = 10.0f,
                .near_plane = 0.0f,
                .far_plane = 100.0f,
            });
    } else if (camera_variant == 1u) {
        camera_label = "perspective-2.5d";
        host.render_extraction().set_view_camera(
            reaktio::render::RenderView::MainScene,
            reaktio::render::PerspectiveCamera25D{
                .eye = {0.0f, 3.0f, -8.0f},
                .target = {0.0f, 0.0f, 0.0f},
                .up = {0.0f, 1.0f, 0.0f},
                .vertical_fov_radians = 1.04719758f,
                .near_plane = 0.1f,
                .far_plane = 250.0f,
            });
    } else {
        camera_label = "free-3d";
        host.render_extraction().set_view_camera(
            reaktio::render::RenderView::MainScene,
            reaktio::render::FreeCamera3D{
                .position = {0.0f, 2.0f, -6.0f},
                .forward = {0.0f, -0.1f, 1.0f},
                .up = {0.0f, 1.0f, 0.0f},
                .vertical_fov_radians = 1.04719758f,
                .near_plane = 0.1f,
                .far_plane = 250.0f,
            });
    }

    host.render_extraction().set_main_scene_clear(0x16324cff);

    std::ostringstream interpolation_stream;
    interpolation_stream << "starter extraction alpha=" << context.interpolation_alpha << " camera=" << camera_label;
    host.render_extraction().add_debug_text(0, 8, 0x0e, interpolation_stream.str());

    const gameplay::ModeInputFrame& input = host.input();
    std::ostringstream input_stream;
    input_stream << "starter actions pressed=" << input.actions().pressed_count()
                 << " text=" << input.text().event_count()
                 << " analog-axes=" << input.analog().axis_count();
    host.render_extraction().add_debug_text(0, 9, 0x0a, input_stream.str());
}

void StarterMode::on_exit(gameplay::IModeHost&, const gameplay::ModeExitContext&) {}

} // namespace reaktio::games::templates