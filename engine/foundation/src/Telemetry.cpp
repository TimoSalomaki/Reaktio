#include "reaktio/foundation/Telemetry.hpp"

#include <iomanip>
#include <sstream>

namespace reaktio::foundation {

void TelemetryRecorder::record(TelemetrySnapshot snapshot) {
    history_.push_back(snapshot);
}

const TelemetrySnapshot* TelemetryRecorder::last() const noexcept {
    if (history_.empty()) {
        return nullptr;
    }

    return &history_.back();
}

TelemetrySnapshot* TelemetryRecorder::last_mutable() noexcept {
    if (history_.empty()) {
        return nullptr;
    }

    return &history_.back();
}

std::span<const TelemetrySnapshot> TelemetryRecorder::history() const noexcept {
    return std::span<const TelemetrySnapshot>{history_.data(), history_.size()};
}

RuntimeBudget make_bootstrap_budget() noexcept {
    return RuntimeBudget{};
}

bool within_budget(const TelemetrySnapshot& snapshot, const RuntimeBudget& budget) noexcept {
    return snapshot.frame_ms <= budget.target_frame_ms &&
           snapshot.simulation_ms <= budget.simulation_budget_ms &&
           snapshot.render_submission_ms <= budget.render_submission_budget_ms &&
           snapshot.resident_memory_mib <= budget.resident_memory_budget_mib &&
           snapshot.draw_calls <= budget.draw_call_budget &&
           snapshot.visible_cues <= budget.visible_cue_budget;
}

std::string describe_budget_report(const TelemetrySnapshot& snapshot, const RuntimeBudget& budget) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2);
    stream << "Telemetry snapshot: frame=" << snapshot.frame_ms << "ms / " << budget.target_frame_ms
           << "ms, sim=" << snapshot.simulation_ms << "ms / " << budget.simulation_budget_ms
           << "ms, render=" << snapshot.render_submission_ms << "ms / "
           << budget.render_submission_budget_ms << "ms, drift=" << snapshot.audio_drift_ms
           << "ms, draw_calls=" << snapshot.draw_calls << " / " << budget.draw_call_budget
           << ", visible_cues=" << snapshot.visible_cues << " / " << budget.visible_cue_budget
           << ", resident_memory=" << snapshot.resident_memory_mib << " MiB / "
           << budget.resident_memory_budget_mib << " MiB";

    if (!within_budget(snapshot, budget)) {
        stream << " [OVER BUDGET]";
    }

    return stream.str();
}

} // namespace reaktio::foundation