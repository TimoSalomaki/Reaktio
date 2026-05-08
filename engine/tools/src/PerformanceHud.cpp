#include "reaktio/tools/PerformanceHud.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace reaktio::tools {

namespace {

[[nodiscard]] InspectorRowSeverity severity_for(double value, double warn_threshold, double error_threshold) noexcept {
    if (value >= error_threshold) {
        return InspectorRowSeverity::Error;
    }
    if (value >= warn_threshold) {
        return InspectorRowSeverity::Warning;
    }
    return InspectorRowSeverity::Info;
}

[[nodiscard]] std::string format_ms(double ms, int precision = 3) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << ms << "ms";
    return stream.str();
}

} // namespace

InspectorPanel build_performance_hud(const PerformanceHudInputs& inputs) {
    InspectorPanel panel{};
    panel.id = "performance-hud";
    panel.title = "Performance HUD";

    if (inputs.telemetry != nullptr && inputs.budget != nullptr) {
        const foundation::TelemetrySnapshot& snap = *inputs.telemetry;
        const foundation::RuntimeBudget& budget = *inputs.budget;
        push_row(
            panel,
            "frame_ms",
            format_ms(snap.frame_ms),
            severity_for(snap.frame_ms, budget.target_frame_ms, budget.target_frame_ms * 1.5));
        push_row(
            panel,
            "simulation_ms",
            format_ms(snap.simulation_ms),
            severity_for(snap.simulation_ms, budget.simulation_budget_ms, budget.simulation_budget_ms * 1.5));
        push_row(
            panel,
            "render_submission_ms",
            format_ms(snap.render_submission_ms),
            severity_for(
                snap.render_submission_ms,
                budget.render_submission_budget_ms,
                budget.render_submission_budget_ms * 1.5));
        push_row(
            panel,
            "audio_drift_ms",
            format_ms(snap.audio_drift_ms),
            severity_for(
                std::fabs(snap.audio_drift_ms),
                inputs.audio_thresholds.drift_warn_ms,
                inputs.audio_thresholds.drift_error_ms));
        push_row(
            panel,
            "resident_memory_mib",
            std::to_string(snap.resident_memory_mib),
            severity_for(
                static_cast<double>(snap.resident_memory_mib),
                static_cast<double>(budget.resident_memory_budget_mib),
                static_cast<double>(budget.resident_memory_budget_mib) * 1.25));
        push_row(
            panel,
            "draw_calls",
            std::to_string(snap.draw_calls),
            severity_for(
                static_cast<double>(snap.draw_calls),
                static_cast<double>(budget.draw_call_budget),
                static_cast<double>(budget.draw_call_budget) * 1.25));
        push_row(
            panel,
            "visible_cues",
            std::to_string(snap.visible_cues),
            severity_for(
                static_cast<double>(snap.visible_cues),
                static_cast<double>(budget.visible_cue_budget),
                static_cast<double>(budget.visible_cue_budget) * 1.25));
    } else {
        push_row(panel, "telemetry", "<unavailable>", InspectorRowSeverity::Warning);
    }

    if (inputs.render_stats != nullptr) {
        const render::RenderStats& stats = *inputs.render_stats;
        push_row(panel, "renderer", std::string(stats.renderer_name));
        push_row(panel, "backbuffer",
            std::to_string(stats.backbuffer_width) + "x" + std::to_string(stats.backbuffer_height));
        push_row(panel, "view_count", std::to_string(stats.view_count));
        push_row(panel, "render_draw_calls", std::to_string(stats.draw_calls));
        push_row(panel, "render_compute_calls", std::to_string(stats.compute_calls));
        push_row(panel, "render_blit_calls", std::to_string(stats.blit_calls));
        push_row(panel, "transient_vertices", std::to_string(stats.transient_vertices));
        push_row(
            panel,
            "transient_failed_allocations",
            std::to_string(stats.transient_failed_allocations),
            stats.transient_failed_allocations > 0 ? InspectorRowSeverity::Error : InspectorRowSeverity::Info);
        push_row(panel, "instanced_batches", std::to_string(stats.instanced_batches));
        push_row(panel, "instanced_instances", std::to_string(stats.instanced_instances));
        push_row(
            panel,
            "instance_failed_allocations",
            std::to_string(stats.instance_failed_allocations),
            stats.instance_failed_allocations > 0 ? InspectorRowSeverity::Error : InspectorRowSeverity::Info);
        push_row(panel, "loaded_assets",
            std::to_string(stats.loaded_textures) + "tex/" +
            std::to_string(stats.loaded_meshes) + "mesh/" +
            std::to_string(stats.loaded_fonts) + "font");
    }

    if (inputs.transport != nullptr) {
        const gameplay::TransportDiagnostics& diag = *inputs.transport;
        push_row(panel, "audio_authority", diag.using_audio_authority ? "1" : "0");
        push_row(panel, "audio_drift",
            format_ms(diag.drift_seconds * 1000.0),
            severity_for(
                std::fabs(diag.drift_seconds * 1000.0),
                inputs.audio_thresholds.drift_warn_ms,
                inputs.audio_thresholds.drift_error_ms));
        push_row(panel, "audio_total_latency",
            format_ms(diag.total_output_latency_seconds * 1000.0),
            severity_for(
                diag.total_output_latency_seconds * 1000.0,
                inputs.audio_thresholds.latency_warn_ms,
                inputs.audio_thresholds.latency_error_ms));
        push_row(panel, "audio_corrections", std::to_string(diag.correction_count));
    }

    return panel;
}

} // namespace reaktio::tools
