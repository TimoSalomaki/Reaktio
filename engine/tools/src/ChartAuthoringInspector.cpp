#include "reaktio/tools/ChartAuthoringInspector.hpp"

#include "reaktio/rhythm/TempoMap.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>
#include <variant>

namespace reaktio::tools {

namespace {

[[nodiscard]] const content::RoutedCueData* routed_cue_data_or_null(
    const content::ChartEvent& event) noexcept {
    if (auto* note = std::get_if<content::NoteCue>(&event)) {
        return &note->cue;
    }
    if (auto* hold = std::get_if<content::HoldCue>(&event)) {
        return &hold->cue;
    }
    if (auto* hazard = std::get_if<content::HazardCue>(&event)) {
        return &hazard->cue;
    }
    return nullptr;
}

void increment(std::unordered_map<std::int64_t, std::uint32_t>& bins, std::int64_t key) {
    bins[key] = bins[key] + 1;
}

void increment(std::unordered_map<std::uint32_t, std::uint32_t>& bins, std::uint32_t key) {
    bins[key] = bins[key] + 1;
}

} // namespace

ChartAuthoringSnapshot build_chart_authoring_snapshot(
    const content::ChartDocument& document,
    ChartAuthoringInspectorOptions options) {
    ChartAuthoringSnapshot snapshot{};
    snapshot.summary = content::summarize_chart_document(document);

    rhythm::TempoMap tempo_map{};
    rhythm::TempoMapDefinition definition_copy = document.tempo_map;
    const bool tempo_map_valid = tempo_map.rebuild(std::move(definition_copy));

    for (const content::ChartEvent& event : document.events) {
        const content::ChartEventKind kind = content::event_kind(event);
        const bool interactive = content::is_interactive_event(event);
        const rhythm::ChartTick start_tick = content::event_start_tick(event);

        if (interactive && tempo_map_valid) {
            const rhythm::RhythmPosition pos = tempo_map.position_from_tick(start_tick);
            increment(snapshot.interactive_cues_per_bar, pos.bar.bar_index);
        }
        if (const content::RoutedCueData* routed = routed_cue_data_or_null(event)) {
            // Long-duration cue table: any routed cue (Note / Hold /
            // Hazard) with non-zero authored duration. We do NOT clamp
            // by HoldCue alone because authoring formats sometimes use
            // non-zero durations on Note/Hazard cues to model rolls or
            // sustained hazards; treating them all uniformly avoids
            // surprising authors who later switch a cue's kind.
            if (routed->event.placement.duration_ticks > 0) {
                if (snapshot.top_long_duration_rows.size() < 4096) {
                    ChartAuthoringLongDurationRow row{};
                    row.kind = kind;
                    row.id = routed->event.id;
                    row.start_tick = routed->event.placement.start_tick;
                    row.end_tick_exclusive =
                        routed->event.placement.start_tick + routed->event.placement.duration_ticks;
                    row.duration_ticks = routed->event.placement.duration_ticks;
                    row.lane_index = routed->route.lane_index;
                    row.channel_index = routed->route.channel_index;
                    snapshot.top_long_duration_rows.push_back(std::move(row));
                }
            }
            if (routed->route.channel_index.has_value()) {
                increment(snapshot.channel_histogram, *routed->route.channel_index);
            }
            if (routed->route.lane_index.has_value()) {
                increment(snapshot.lane_histogram, *routed->route.lane_index);
            }
            // Cross-reference: scroll_profile_id must resolve. An empty
            // string falls back to default_scroll_profile_id; if BOTH
            // are empty the inspector flags it.
            const std::string& sid = routed->scroll_profile_id;
            const std::string& fallback = document.default_scroll_profile_id;
            const std::string& effective = sid.empty() ? fallback : sid;
            if (effective.empty()) {
                if (snapshot.cross_reference_issues.size() < options.max_cross_reference_issues) {
                    ChartAuthoringCrossReferenceIssue issue{};
                    issue.event_id = routed->event.id;
                    issue.detail = "no scroll_profile_id and no default";
                    snapshot.cross_reference_issues.push_back(std::move(issue));
                }
            } else if (content::find_scroll_profile(document, effective) == nullptr) {
                if (snapshot.cross_reference_issues.size() < options.max_cross_reference_issues) {
                    ChartAuthoringCrossReferenceIssue issue{};
                    issue.event_id = routed->event.id;
                    issue.detail = "scroll_profile_id '" + effective + "' not defined";
                    snapshot.cross_reference_issues.push_back(std::move(issue));
                }
            }
        }
    }

    // Top-N density rows by interactive count.
    {
        std::vector<ChartAuthoringDensityRow> rows;
        rows.reserve(snapshot.interactive_cues_per_bar.size());
        for (const auto& [bar, count] : snapshot.interactive_cues_per_bar) {
            ChartAuthoringDensityRow row{};
            row.bar_index = bar;
            row.interactive_cues = count;
            rows.push_back(row);
        }
        std::sort(rows.begin(), rows.end(),
            [](const ChartAuthoringDensityRow& a, const ChartAuthoringDensityRow& b) {
                if (a.interactive_cues != b.interactive_cues) {
                    return a.interactive_cues > b.interactive_cues;
                }
                return a.bar_index < b.bar_index;
            });
        if (rows.size() > options.max_density_rows) {
            rows.resize(options.max_density_rows);
        }
        snapshot.top_density_rows = std::move(rows);
    }
    // Top-N long-duration rows by duration. Collected without an
    // arbitrary upper bound during the walk (4096 cap is a safety
    // valve for pathological inputs only) and clipped to
    // options.max_long_duration_rows after sorting so the user always
    // sees the longest cues first.
    {
        std::sort(snapshot.top_long_duration_rows.begin(), snapshot.top_long_duration_rows.end(),
            [](const ChartAuthoringLongDurationRow& a, const ChartAuthoringLongDurationRow& b) {
                return a.duration_ticks > b.duration_ticks;
            });
        if (snapshot.top_long_duration_rows.size() > options.max_long_duration_rows) {
            snapshot.top_long_duration_rows.resize(options.max_long_duration_rows);
        }
    }
    return snapshot;
}

InspectorPanel build_chart_authoring_inspector(const ChartAuthoringSnapshot& snapshot) {
    InspectorPanel panel{};
    panel.id = "chart-authoring";
    panel.title = "Chart Authoring";
    push_row(panel, "events", std::to_string(snapshot.summary.event_count));
    push_row(panel, "notes", std::to_string(snapshot.summary.note_count));
    push_row(panel, "holds", std::to_string(snapshot.summary.hold_count));
    push_row(panel, "hazards", std::to_string(snapshot.summary.hazard_count));
    push_row(panel, "triggers", std::to_string(snapshot.summary.trigger_count));
    push_row(panel, "camera_events", std::to_string(snapshot.summary.camera_count));
    push_row(panel, "text_prompts", std::to_string(snapshot.summary.text_prompt_count));
    push_row(panel, "vfx_events", std::to_string(snapshot.summary.vfx_count));
    push_row(panel, "interactive_cues", std::to_string(snapshot.summary.interactive_cue_count));
    push_row(panel, "scroll_profiles", std::to_string(snapshot.summary.scroll_profile_count));
    push_row(panel, "first_event_tick", std::to_string(snapshot.summary.first_event_tick));
    push_row(panel, "last_event_tick", std::to_string(snapshot.summary.last_event_tick));
    push_row(panel, "channels_used", std::to_string(snapshot.channel_histogram.size()));
    push_row(panel, "lanes_used", std::to_string(snapshot.lane_histogram.size()));

    push_row(
        panel,
        "cross_reference_issues",
        std::to_string(snapshot.cross_reference_issues.size()),
        snapshot.cross_reference_issues.empty() ? InspectorRowSeverity::Info
                                                : InspectorRowSeverity::Warning);

    for (const ChartAuthoringDensityRow& row : snapshot.top_density_rows) {
        std::ostringstream line;
        line << "density bar=" << row.bar_index << " interactive=" << row.interactive_cues;
        panel.body_lines.push_back(line.str());
    }
    for (const ChartAuthoringLongDurationRow& row : snapshot.top_long_duration_rows) {
        std::ostringstream line;
        line << "long_duration kind=" << content::to_string(row.kind)
             << " id=" << row.id
             << " start=" << row.start_tick
             << " end=" << row.end_tick_exclusive
             << " duration=" << row.duration_ticks;
        if (row.lane_index.has_value()) {
            line << " lane=" << *row.lane_index;
        }
        if (row.channel_index.has_value()) {
            line << " channel=" << *row.channel_index;
        }
        panel.body_lines.push_back(line.str());
    }
    for (const ChartAuthoringCrossReferenceIssue& issue : snapshot.cross_reference_issues) {
        std::ostringstream line;
        line << "issue id=" << issue.event_id << " detail=" << issue.detail;
        panel.body_lines.push_back(line.str());
    }

    return panel;
}

} // namespace reaktio::tools
