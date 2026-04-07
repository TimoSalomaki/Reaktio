#include "reaktio/games/reference/ReferenceSandboxMode.hpp"

#include "reaktio/foundation/DeterministicRandom.hpp"
#include "reaktio/foundation/Telemetry.hpp"
#include "reaktio/gameplay/IModeHost.hpp"
#include "reaktio/gameplay/ReplayRecorder.hpp"
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

std::uint32_t mix_visual_color(gameplay::TransportPlaybackState playback_state, std::uint32_t visual_roll) noexcept {
    const std::uint32_t base = state_color(playback_state);
    const std::uint32_t red = (base >> 24u) & 0xffu;
    const std::uint32_t green = (base >> 16u) & 0xffu;
    const std::uint32_t blue = 0x20u + (visual_roll % 0xa0u);
    return (red << 24u) | (green << 16u) | (blue << 8u) | 0xffu;
}

std::uint64_t make_state_hash(
    std::uint64_t fixed_steps,
    const gameplay::TransportSnapshot& transport_snapshot,
    std::uint32_t transport_roll,
    std::uint32_t visual_roll) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    hash ^= fixed_steps;
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(transport_snapshot.playback_state);
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(transport_snapshot.position_seconds * 1000000.0);
    hash *= 1099511628211ull;
    hash ^= transport_roll;
    hash *= 1099511628211ull;
    hash ^= visual_roll;
    return hash;
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
    transport_roll_ = 0;
    visual_roll_ = 0;

    gameplay::ITransportControl& transport = host.transport();
    transport.stop();
    transport.set_loop_region(0.75, 1.25);
    transport.play();

    host.random_service().reset_streams();
    host.replay().record_checkpoint(gameplay::ReplayCheckpoint{
        .frame_index = host.frame_timing().frame_index,
        .simulation_step = fixed_steps_,
        .transport_state = transport.snapshot().playback_state,
        .transport_position_seconds = transport.snapshot().position_seconds,
        .root_random_seed = host.random_service().root_seed(),
        .authoritative_state_hash = 0,
        .label = "enter",
        .summary = "reference sandbox entered and initialized transport loop region",
    });
}

void ReferenceSandboxMode::on_fixed_step(gameplay::IModeHost& host, double) {
    ++fixed_steps_;

    foundation::DeterministicRng& transport_rng =
        host.random_service().stream("reference-sandbox.transport");
    foundation::DeterministicRng& visual_rng =
        host.random_service().stream("reference-sandbox.visual");
    transport_roll_ = transport_rng.next_u32(0u, 9999u);
    visual_roll_ = visual_rng.next_u32(0u, 255u);

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

    const gameplay::TransportSnapshot& transport_snapshot = transport.snapshot();
    host.replay().record_checkpoint(gameplay::ReplayCheckpoint{
        .frame_index = host.frame_timing().frame_index,
        .simulation_step = fixed_steps_,
        .transport_state = transport_snapshot.playback_state,
        .transport_position_seconds = transport_snapshot.position_seconds,
        .root_random_seed = host.random_service().root_seed(),
        .authoritative_state_hash = make_state_hash(fixed_steps_, transport_snapshot, transport_roll_, visual_roll_),
        .label = "fixed-step",
        .summary = "sandbox transport/RNG state checkpoint",
    });

    foundation::TelemetrySnapshot snapshot{};
    snapshot.audio_drift_ms = 0.00;
    snapshot.visible_cues = 3;
    snapshot.draw_calls = 0;
    host.telemetry().record(snapshot);
}

void ReferenceSandboxMode::on_render_extract(gameplay::IModeHost& host, double interpolation_alpha) {
    const gameplay::TransportSnapshot& transport_snapshot = host.transport().snapshot();
    const platform::InputSnapshot& input_snapshot = host.input_snapshot();
    const foundation::DeterministicRandomService& random_service = host.random_service();
    const foundation::DeterministicRng* transport_rng = random_service.find_stream("reference-sandbox.transport");
    const foundation::DeterministicRng* visual_rng = random_service.find_stream("reference-sandbox.visual");
    const gameplay::ReplayRecorder& replay_recorder = host.replay();
    const gameplay::ReplayCheckpoint* last_checkpoint = replay_recorder.last_checkpoint();

    host.render_extraction().set_view_camera(
        reaktio::render::RenderView::MainScene,
        reaktio::render::OrthographicCamera2D{
            .center = {0.0f, 0.0f},
            .virtual_height = 720.0f,
            .view_distance = 10.0f,
            .near_plane = 0.0f,
            .far_plane = 100.0f,
        });
    host.render_extraction().set_main_scene_clear(mix_visual_color(transport_snapshot.playback_state, visual_roll_));

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

    std::ostringstream rng_stream;
    rng_stream << "rng root=0x" << std::hex << random_service.root_seed() << std::dec
               << " streams=" << random_service.stream_count() << " transport-roll="
               << transport_roll_ << " visual-roll=" << visual_roll_ << " draws="
               << (transport_rng != nullptr ? transport_rng->generated_values() : 0) << '/'
               << (visual_rng != nullptr ? visual_rng->generated_values() : 0);
    host.render_extraction().add_debug_text(0, 11, 0x0d, rng_stream.str());

    std::ostringstream replay_stream;
    replay_stream << "replay inputs=" << replay_recorder.input_frame_count() << " checkpoints="
                  << replay_recorder.checkpoint_count() << " last="
                  << (last_checkpoint != nullptr ? last_checkpoint->label : std::string_view("none"));
    host.render_extraction().add_debug_text(0, 12, 0x0c, replay_stream.str());
}

void ReferenceSandboxMode::on_exit(gameplay::IModeHost& host) {
    host.transport().stop();
}

} // namespace reaktio::games::reference