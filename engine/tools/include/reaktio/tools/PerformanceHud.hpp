#pragma once

#include "reaktio/foundation/Telemetry.hpp"
#include "reaktio/gameplay/Transport.hpp"
#include "reaktio/render/RenderSubsystem.hpp"
#include "reaktio/tools/InspectorPanel.hpp"

namespace reaktio::tools {

// Performance HUD inspector. Composes the three observability surfaces
// the engine already exposes (RenderStats, TelemetrySnapshot,
// TransportDiagnostics) into a single panel + body table that's stable
// to diff and friendly for live ImGui rendering.
//
// Severity is derived from the supplied RuntimeBudget so the same panel
// works across target classes (mobile, integrated GPU, desktop). Modes
// that ship with custom thresholds wrap the helper or pass a tuned
// RuntimeBudget; the inspector itself stays mode-agnostic.
//
// Audio-clock thresholds (drift / latency) are kept separate from the
// CPU/GPU budget because the audio ADR (see docs/adr/0007) governs
// them independently from the per-frame CPU budget. The defaults
// mirror TransportInspectorThresholds so a runtime that customizes the
// transport inspector can pass the same numbers in here without
// duplicating constants.

struct PerformanceHudAudioThresholds {
    double drift_warn_ms{20.0};
    double drift_error_ms{60.0};
    double latency_warn_ms{40.0};
    double latency_error_ms{80.0};
};

struct PerformanceHudInputs {
    const foundation::TelemetrySnapshot* telemetry{};   // May be null when telemetry not yet recorded.
    const foundation::RuntimeBudget* budget{};          // Required for budget-based severity.
    const render::RenderStats* render_stats{};          // May be null when render not initialized (headless smoke).
    const gameplay::TransportDiagnostics* transport{};  // May be null when transport not initialized yet.
    PerformanceHudAudioThresholds audio_thresholds{};
};

[[nodiscard]] InspectorPanel build_performance_hud(const PerformanceHudInputs& inputs);

} // namespace reaktio::tools
