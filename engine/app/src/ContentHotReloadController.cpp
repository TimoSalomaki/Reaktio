#include "reaktio/app/ContentHotReloadController.hpp"

#include "reaktio/content/CookedChartLibrary.hpp"
#include "reaktio/foundation/CrashSafeLog.hpp"
#include "reaktio/foundation/ResourceRegistry.hpp"
#include "reaktio/gameplay/EventBus.hpp"
#include "reaktio/render/RenderSubsystem.hpp"

#include <array>
#include <sstream>

namespace reaktio::app {

namespace {

void publish_hot_reload_event(
    gameplay::EventBus& event_bus,
    std::uint64_t frame_index,
    std::uint64_t simulation_step,
    content::HotReloadAssetFamily family,
    gameplay::ContentHotReloadStatus status,
    std::filesystem::path trigger_path,
    std::size_t changed_file_count,
    std::uint64_t revision,
    std::string detail) {
    event_bus.publish(
        "app.hot-reload",
        frame_index,
        simulation_step,
        gameplay::ContentHotReloadEvent{
            .family = family,
            .status = status,
            .trigger_path = std::move(trigger_path),
            .changed_file_count = changed_file_count,
            .revision = revision,
            .detail = std::move(detail),
        });
}

void log_reload_message(
    foundation::CrashSafeLog& log,
    content::HotReloadAssetFamily family,
    gameplay::ContentHotReloadStatus status,
    std::size_t changed_file_count,
    std::uint64_t revision,
    const std::filesystem::path& trigger_path,
    std::string_view detail) {
    std::ostringstream stream;
    stream << "Hot reload " << content::to_string(family)
           << ' ' << to_string(status)
           << " changes=" << changed_file_count
           << " rev=" << revision;
    if (!trigger_path.empty()) {
        stream << " path=" << trigger_path.string();
    }
    if (!detail.empty()) {
        stream << " detail=" << detail;
    }
    log.write(
        status == gameplay::ContentHotReloadStatus::Failed ? foundation::LogLevel::Error : foundation::LogLevel::Info,
        stream.str());
}

} // namespace

ContentHotReloadController::ContentHotReloadController(content::HotReloadConfig config)
    : config_(std::move(config)) {}

bool ContentHotReloadController::initialize(foundation::CrashSafeLog& log) {
    summary_ = {};
    summary_.enabled = config_.enabled;
    return watcher_.configure(config_, log);
}

void ContentHotReloadController::tick(
    double delta_seconds,
    std::uint64_t frame_index,
    std::uint64_t simulation_step,
    foundation::ResourceRegistry& resource_registry,
    render::RenderSubsystem& render_subsystem,
    content::CookedChartLibrary& cooked_chart_library,
    gameplay::EventBus& event_bus,
    foundation::CrashSafeLog& log) {
    if (!config_.enabled) {
        return;
    }

    const content::HotReloadPollResult poll_result = watcher_.poll(delta_seconds, log);
    if (!poll_result.scanned || poll_result.changes.empty()) {
        return;
    }

    summary_.revision = poll_result.revision;
    struct FamilyBatch {
        std::size_t change_count{};
        std::filesystem::path trigger_path;
    };
    std::array<FamilyBatch, 4> batches{};

    for (const content::HotReloadChange& change : poll_result.changes) {
        FamilyBatch& batch = batches[static_cast<std::size_t>(change.family)];
        ++batch.change_count;
        if (batch.trigger_path.empty()) {
            batch.trigger_path = change.path;
        }
    }

    auto apply_status = [&](content::HotReloadAssetFamily family, gameplay::ContentHotReloadStatus status, std::string detail) {
        const FamilyBatch& batch = batches[static_cast<std::size_t>(family)];
        if (batch.change_count == 0u) {
            return;
        }
        if (status == gameplay::ContentHotReloadStatus::Reloaded) {
            ++summary_.applied_reload_count;
        } else if (status == gameplay::ContentHotReloadStatus::Pending) {
            ++summary_.pending_reload_count;
        } else {
            ++summary_.failed_reload_count;
        }
        log_reload_message(log, family, status, batch.change_count, poll_result.revision, batch.trigger_path, detail);
        publish_hot_reload_event(
            event_bus,
            frame_index,
            simulation_step,
            family,
            status,
            batch.trigger_path,
            batch.change_count,
            poll_result.revision,
            std::move(detail));
    };

    if (batches[static_cast<std::size_t>(content::HotReloadAssetFamily::Charts)].change_count > 0u) {
        const std::filesystem::path& manifest_path = watcher_.config().chart_manifest_path;
        if (cooked_chart_library.load(manifest_path, log)) {
            std::ostringstream stream;
            stream << "reloaded cooked chart library charts=" << cooked_chart_library.summary().chart_count
                   << " events=" << cooked_chart_library.summary().total_event_count;
            apply_status(content::HotReloadAssetFamily::Charts, gameplay::ContentHotReloadStatus::Reloaded, stream.str());
        } else {
            apply_status(content::HotReloadAssetFamily::Charts, gameplay::ContentHotReloadStatus::Failed, "failed to reload cooked chart library");
        }
    }

    if (batches[static_cast<std::size_t>(content::HotReloadAssetFamily::SelectedContent)].change_count > 0u) {
        const std::filesystem::path& manifest_path = watcher_.config().selected_content_manifest_path;
        if (render_subsystem.load_cooked_assets(manifest_path, resource_registry)) {
            const render::RenderStats& stats = render_subsystem.stats();
            std::ostringstream stream;
            stream << "reloaded selected content textures=" << stats.loaded_textures
                   << " meshes=" << stats.loaded_meshes
                   << " fonts=" << stats.loaded_fonts;
            apply_status(content::HotReloadAssetFamily::SelectedContent, gameplay::ContentHotReloadStatus::Reloaded, stream.str());
        } else {
            apply_status(content::HotReloadAssetFamily::SelectedContent, gameplay::ContentHotReloadStatus::Failed, "failed to reload selected cooked content");
        }
    }

    if (batches[static_cast<std::size_t>(content::HotReloadAssetFamily::Shaders)].change_count > 0u) {
        apply_status(
            content::HotReloadAssetFamily::Shaders,
            gameplay::ContentHotReloadStatus::Pending,
            "shader hot swap is not wired yet; change hook is active");
    }

    if (batches[static_cast<std::size_t>(content::HotReloadAssetFamily::Materials)].change_count > 0u) {
        apply_status(
            content::HotReloadAssetFamily::Materials,
            gameplay::ContentHotReloadStatus::Pending,
            "material hot swap is not wired yet; change hook is active");
    }
}

const ContentHotReloadControllerSummary& ContentHotReloadController::summary() const noexcept {
    return summary_;
}

const content::HotReloadWatcherSummary& ContentHotReloadController::watcher_summary() const noexcept {
    return watcher_.summary();
}

const content::HotReloadConfig& ContentHotReloadController::config() const noexcept {
    return watcher_.config();
}

} // namespace reaktio::app