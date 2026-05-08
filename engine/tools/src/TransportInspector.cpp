#include "reaktio/tools/TransportInspector.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace reaktio::tools {

namespace {

[[nodiscard]] std::string format_seconds(double seconds, int precision = 3) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << seconds << "s";
    return stream.str();
}

[[nodiscard]] std::string format_signed_milliseconds(double seconds) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << (seconds * 1000.0) << "ms";
    return stream.str();
}

} // namespace

InspectorPanel build_transport_inspector(
    const gameplay::TransportSnapshot& snapshot,
    const gameplay::TransportDiagnostics& diagnostics,
    TransportInspectorThresholds thresholds) {
    InspectorPanel panel{};
    panel.id = "transport";
    panel.title = "Transport";

    push_row(
        panel,
        "playback_state",
        std::string(gameplay::to_string(snapshot.playback_state)),
        snapshot.playback_state == gameplay::TransportPlaybackState::Playing
            ? InspectorRowSeverity::Notice
            : InspectorRowSeverity::Info);
    push_row(
        panel,
        "position",
        format_seconds(snapshot.position_seconds));
    push_row(
        panel,
        "duration",
        format_seconds(snapshot.duration_seconds));
    push_row(
        panel,
        "playback_rate",
        std::to_string(snapshot.playback_rate));
    push_row(
        panel,
        "completed_loops",
        std::to_string(snapshot.completed_loops));
    push_row(
        panel,
        "completed_previews",
        std::to_string(snapshot.completed_previews));

    // Diagnostics block. Mark drift / latency / correction-frequency
    // breaches with the configured severity so a downstream UI or smoke
    // verifier can surface them prominently.
    const double abs_drift = std::fabs(diagnostics.drift_seconds);
    InspectorRowSeverity drift_severity = InspectorRowSeverity::Info;
    if (abs_drift >= thresholds.drift_error_seconds) {
        drift_severity = InspectorRowSeverity::Error;
    } else if (abs_drift >= thresholds.drift_warn_seconds) {
        drift_severity = InspectorRowSeverity::Warning;
    }
    push_row(panel, "using_audio_authority",
        diagnostics.using_audio_authority ? "1" : "0");
    push_row(panel, "drift", format_signed_milliseconds(diagnostics.drift_seconds), drift_severity);

    InspectorRowSeverity latency_severity = InspectorRowSeverity::Info;
    if (diagnostics.total_output_latency_seconds >= thresholds.latency_error_seconds) {
        latency_severity = InspectorRowSeverity::Error;
    } else if (diagnostics.total_output_latency_seconds >= thresholds.latency_warn_seconds) {
        latency_severity = InspectorRowSeverity::Warning;
    }
    push_row(
        panel,
        "total_output_latency",
        format_signed_milliseconds(diagnostics.total_output_latency_seconds),
        latency_severity);
    push_row(
        panel,
        "device_latency",
        format_signed_milliseconds(diagnostics.device_latency_seconds));
    push_row(
        panel,
        "queued_input",
        format_signed_milliseconds(diagnostics.queued_input_seconds));

    InspectorRowSeverity correction_severity = InspectorRowSeverity::Info;
    if (diagnostics.recent_correction_count >= thresholds.recent_correction_warn_count) {
        correction_severity = InspectorRowSeverity::Warning;
    }
    push_row(
        panel,
        "correction_count",
        std::to_string(diagnostics.correction_count),
        correction_severity);
    push_row(
        panel,
        "recent_corrections",
        std::to_string(diagnostics.recent_correction_count));

    // Recent corrections appear as supplementary body lines so the
    // top-level rows stay scannable. Each line is one correction event.
    for (std::size_t i = 0; i < diagnostics.recent_correction_count; ++i) {
        const gameplay::TransportCorrectionEvent& event = diagnostics.recent_corrections[i];
        std::ostringstream line;
        line << "correction#" << event.sequence << " "
             << "type=" << gameplay::to_string(event.correction_type)
             << " drift_before=" << std::fixed << std::setprecision(3) << (event.drift_before_seconds * 1000.0)
             << "ms applied=" << std::fixed << std::setprecision(3) << (event.correction_applied_seconds * 1000.0)
             << "ms";
        panel.body_lines.push_back(line.str());
    }

    return panel;
}

} // namespace reaktio::tools
