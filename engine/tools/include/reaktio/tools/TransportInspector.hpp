#pragma once

#include "reaktio/gameplay/Transport.hpp"
#include "reaktio/tools/InspectorPanel.hpp"

#include <cstdint>

namespace reaktio::tools {

// Read-only inspector over the engine's transport surfaces. Combines the
// authoritative TransportSnapshot (logical playback state) with the
// TransportDiagnostics (drift/latency/correction telemetry) into a
// single InspectorPanel suitable for headless validation, CLI dumps,
// and ImGui presentation.
//
// The inspector annotates rows with severity hints based on configurable
// thresholds. The default thresholds are deliberately conservative so a
// shipping mode does not silently drift past the audio-clock ADR bounds.

struct TransportInspectorThresholds {
    // Drift between authoritative position and simulation position.
    // Above warn -> Warning row; above error -> Error row.
    double drift_warn_seconds{0.020};   // 20ms
    double drift_error_seconds{0.060};  // 60ms

    // Total output latency (device + queued input).
    double latency_warn_seconds{0.040};
    double latency_error_seconds{0.080};

    // How frequently corrections fire. Treated as a soft warning when
    // the recent_correction window is saturated.
    std::size_t recent_correction_warn_count{2};
};

[[nodiscard]] InspectorPanel build_transport_inspector(
    const gameplay::TransportSnapshot& snapshot,
    const gameplay::TransportDiagnostics& diagnostics,
    TransportInspectorThresholds thresholds = {});

} // namespace reaktio::tools
