#pragma once

#include "reaktio/foundation/CrashSafeLog.hpp"
#include "reaktio/content/HotReload.hpp"
#include "reaktio/foundation/DeterministicRandom.hpp"
#include "reaktio/foundation/ResourceRegistry.hpp"
#include "reaktio/foundation/Telemetry.hpp"
#include "reaktio/gameplay/EventBus.hpp"
#include "reaktio/gameplay/GameModeRegistry.hpp"
#include "reaktio/gameplay/GameplayInput.hpp"
#include "reaktio/gameplay/IModeHost.hpp"
#include "reaktio/gameplay/ModeConfiguration.hpp"
#include "reaktio/gameplay/ModeFlow.hpp"
#include "reaktio/gameplay/Modifiers.hpp"
#include "reaktio/gameplay/PresentationEvents.hpp"
#include "reaktio/gameplay/ReplayRecorder.hpp"
#include "reaktio/gameplay/SaveData.hpp"
#include "reaktio/gameplay/WorldModel.hpp"
#include "reaktio/platform/ApplicationConfig.hpp"
#include "reaktio/platform/FrameClock.hpp"
#include "reaktio/platform/InputBindings.hpp"
#include "reaktio/platform/InputSnapshot.hpp"
#include "reaktio/render/RenderExtraction.hpp"
#include "reaktio/platform/StackProbe.hpp"
#include "reaktio/platform/WindowState.hpp"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace reaktio::foundation {
struct BuildInfo;
} // namespace reaktio::foundation

namespace reaktio::platform {
struct AudioDeviceInfo;
class SdlApplicationShell;
} // namespace reaktio::platform

namespace reaktio::app {

struct SmokeApplicationDependencies {
    foundation::RuntimeBudget runtime_budget;
    platform::ApplicationConfig application_config;
    std::string startup_mode_id;
    std::uint64_t random_seed{foundation::k_default_random_seed};
    platform::StackProbe stack_probe;
    gameplay::GameModeRegistry game_mode_registry;
    std::ostream* log_stream{};
    std::string configuration_source{"<defaults>"};
    platform::InputBindingsConfig input_bindings;
    gameplay::InputActionMapStore input_action_maps;
    gameplay::ModeConfigurationStore mode_configuration;
    gameplay::ModifierStore modifiers;
    content::HotReloadConfig hot_reload;

    // Optional: the smoke runs a deterministic post-shutdown lifecycle
    // exercise on this mode if provided. The app library treats it as an
    // opaque IGameMode and never assumes anything about its concrete type.
    std::unique_ptr<gameplay::IGameMode> post_shutdown_dry_run_mode;
    std::vector<std::string> post_shutdown_dry_run_text_events;
    std::string post_shutdown_dry_run_label{"post-shutdown-dry-run"};
};

class SmokeApplication final : public gameplay::IModeHost {
  public:
    explicit SmokeApplication(SmokeApplicationDependencies dependencies);

    [[nodiscard]] int run();

    [[nodiscard]] const foundation::RuntimeBudget& runtime_budget() const noexcept override;
    [[nodiscard]] const platform::StackProbe& stack_probe() const noexcept override;
    [[nodiscard]] const gameplay::ModeInputFrame& input() const noexcept override;
    [[nodiscard]] const gameplay::InputActionMapStore& input_action_maps() const noexcept override;
    [[nodiscard]] gameplay::InputActionMapStore& input_action_maps() noexcept override;
    [[nodiscard]] const platform::InputSnapshot& input_snapshot() const noexcept override;
    [[nodiscard]] const platform::InputBindingsConfig& input_bindings() const noexcept override;
    [[nodiscard]] const platform::FrameTiming& frame_timing() const noexcept override;
    [[nodiscard]] const platform::WindowState& window_state() const noexcept override;
    [[nodiscard]] foundation::DeterministicRandomService& random_service() noexcept override;
    [[nodiscard]] foundation::ResourceRegistry& resource_registry() noexcept override;
    [[nodiscard]] const gameplay::ModeConfigurationStore& mode_configuration() const noexcept override;
    [[nodiscard]] const gameplay::ModifierStore& modifier_store() const noexcept override;
    [[nodiscard]] const gameplay::ModifierSet& modifiers() const noexcept override;
    [[nodiscard]] gameplay::EventBus& event_bus() noexcept override;
    [[nodiscard]] gameplay::PresentationEventBus& presentation_events() noexcept override;
    [[nodiscard]] gameplay::ModeFlowController& flow() noexcept override;
    [[nodiscard]] gameplay::ReplayRecorder& replay() noexcept override;
    [[nodiscard]] gameplay::SaveDataStore& save_data() noexcept override;
    [[nodiscard]] gameplay::WorldModel& world_model() noexcept override;
    [[nodiscard]] gameplay::ITransportControl& transport() noexcept override;
    [[nodiscard]] render::RenderExtractionContext& render_extraction() noexcept override;
    [[nodiscard]] foundation::TelemetryRecorder& telemetry() noexcept override;
    void request_quit() noexcept override;
    [[nodiscard]] bool toggle_fullscreen() noexcept override;

  private:
    void log_startup(const foundation::BuildInfo& build_info);
    void log_audio_device(const platform::AudioDeviceInfo& info);
    void log_window_state();

    SmokeApplicationDependencies dependencies_;
    foundation::TelemetryRecorder telemetry_recorder_;
    foundation::DeterministicRandomService random_service_;
    foundation::ResourceRegistry resource_registry_;
    gameplay::EventBus event_bus_;
    gameplay::PresentationEventBus presentation_event_bus_;
    gameplay::ModeFlowController mode_flow_{};
    gameplay::SaveDataStore save_data_store_{};
    gameplay::InMemorySaveDataBackend save_data_backend_{};
    gameplay::ModeInputFrame mode_input_frame_;
    gameplay::ReplayRecorder replay_recorder_;
    gameplay::WorldModel world_model_;
    render::RenderExtractionContext render_extraction_context_;
    foundation::CrashSafeLog crash_safe_log_;
    platform::SdlApplicationShell* active_shell_{};
    gameplay::ITransportControl* active_transport_{};
    std::string active_mode_id_{};
    std::uint64_t published_flow_transition_count_{};
};

} // namespace reaktio::app