#include "reaktio/tools/ModeStateInspector.hpp"

#include <iomanip>
#include <sstream>
#include <vector>

namespace reaktio::tools {

namespace {

[[nodiscard]] std::string format_capability_flags(gameplay::ModeCapabilityFlags flags) {
    using gameplay::ModeCapabilities::EmitsRenderPackets;
    using gameplay::ModeCapabilities::RecordsReplay;
    using gameplay::ModeCapabilities::SupportsCalibration;
    using gameplay::ModeCapabilities::SupportsPractice;
    using gameplay::ModeCapabilities::UsesActionInput;
    using gameplay::ModeCapabilities::UsesAnalogInput;
    using gameplay::ModeCapabilities::UsesTextInput;
    using gameplay::ModeCapabilities::UsesTransport;
    using gameplay::ModeCapabilities::UsesWorldModel;

    static const std::pair<gameplay::ModeCapabilityFlags, std::string_view> table[] = {
        {UsesActionInput, "action"},
        {UsesTextInput, "text"},
        {UsesAnalogInput, "analog"},
        {UsesTransport, "transport"},
        {UsesWorldModel, "world"},
        {EmitsRenderPackets, "render"},
        {RecordsReplay, "replay"},
        {SupportsPractice, "practice"},
        {SupportsCalibration, "calibration"},
    };
    std::ostringstream stream;
    bool first = true;
    for (const auto& [flag, label] : table) {
        if ((flags & flag) == flag) {
            if (!first) {
                stream << ",";
            }
            stream << label;
            first = false;
        }
    }
    if (first) {
        stream << "none";
    }
    return stream.str();
}

} // namespace

InspectorPanel build_mode_descriptor_inspector(const gameplay::ModeDescriptor& descriptor) {
    InspectorPanel panel{};
    panel.id = "mode-descriptor";
    panel.title = "Mode Descriptor";
    push_row(panel, "id", std::string(descriptor.id));
    push_row(panel, "display_name", std::string(descriptor.display_name));
    push_row(panel, "family", std::string(descriptor.family));
    push_row(panel, "capabilities", format_capability_flags(descriptor.capabilities));
    if (!descriptor.description.empty()) {
        panel.body_lines.push_back(std::string(descriptor.description));
    }
    return panel;
}

InspectorPanel build_mode_flow_inspector(const gameplay::ModeFlowSnapshot& snapshot) {
    InspectorPanel panel{};
    panel.id = "mode-flow";
    panel.title = "Mode Flow";
    push_row(panel, "state", std::string(gameplay::to_string(snapshot.state)));
    push_row(panel, "last_reason", std::string(gameplay::to_string(snapshot.last_reason)));
    push_row(panel, "transition_count", std::to_string(snapshot.transition_count));
    push_row(panel, "simulation_step", std::to_string(snapshot.simulation_step));
    push_row(panel, "frame_index", std::to_string(snapshot.frame_index));
    push_row(panel, "results_present", snapshot.results.present ? "1" : "0");
    if (snapshot.results.present && !snapshot.results.label.empty()) {
        push_row(panel, "results_label", snapshot.results.label);
    }
    push_row(panel, "practice_active",
        snapshot.flags.practice_active ? "1" : "0");
    push_row(panel, "no_fail_active",
        snapshot.flags.no_fail_active ? "1" : "0");
    push_row(panel, "autoplay_active",
        snapshot.flags.autoplay_active ? "1" : "0");
    return panel;
}

InspectorPanel build_score_summary_inspector(const gameplay::ScoreSummary& summary) {
    InspectorPanel panel{};
    panel.id = "score-summary";
    panel.title = "Score Summary";
    push_row(panel, "score", std::to_string(summary.score));
    push_row(panel, "current_combo", std::to_string(summary.current_combo));
    push_row(panel, "max_combo", std::to_string(summary.max_combo));
    push_row(panel, "scoreable_hits", std::to_string(summary.scoreable_hit_count));
    push_row(panel, "misses", std::to_string(summary.miss_count));
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(3) << summary.accuracy_ratio;
        push_row(panel, "accuracy_ratio", stream.str());
    }
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(2) << summary.multiplier;
        push_row(panel, "multiplier", stream.str());
    }
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(2) << summary.health;
        const InspectorRowSeverity severity = summary.health <= 0.0
            ? InspectorRowSeverity::Error
            : (summary.health < 0.25 ? InspectorRowSeverity::Warning : InspectorRowSeverity::Info);
        push_row(panel, "health", stream.str(), severity);
    }
    push_row(panel, "grade", std::string(gameplay::to_string(summary.grade)));
    {
        std::string_view text;
        switch (summary.run_state) {
        case gameplay::ScoreRunState::Active:
            text = "active";
            break;
        case gameplay::ScoreRunState::Cleared:
            text = "cleared";
            break;
        case gameplay::ScoreRunState::Failed:
            text = "failed";
            break;
        }
        const InspectorRowSeverity severity = summary.run_state == gameplay::ScoreRunState::Failed
            ? InspectorRowSeverity::Error
            : (summary.run_state == gameplay::ScoreRunState::Cleared
                   ? InspectorRowSeverity::Notice
                   : InspectorRowSeverity::Info);
        push_row(panel, "run_state", std::string(text), severity);
    }
    return panel;
}

} // namespace reaktio::tools
