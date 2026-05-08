#include "reaktio/tools/ActiveCueInspector.hpp"

#include <iomanip>
#include <sstream>

namespace reaktio::tools {

InspectorPanel build_active_cue_inspector(
    const gameplay::CueScheduler& scheduler,
    const rhythm::TempoMap* tempo_map,
    rhythm::ChartTick cursor_tick,
    ActiveCueInspectorOptions options) {
    InspectorPanel panel{};
    panel.id = "active-cues";
    panel.title = "Active Cues";

    const gameplay::CueSchedulerSummary& summary = scheduler.summary();
    push_row(panel, "scheduled_cue_count", std::to_string(summary.scheduled_cue_count));
    push_row(panel, "active_cue_count", std::to_string(summary.active_cue_count));
    push_row(panel, "spawned_this_update", std::to_string(summary.spawned_this_update));
    push_row(panel, "updated_this_update", std::to_string(summary.updated_this_update));
    push_row(panel, "despawned_this_update", std::to_string(summary.despawned_this_update));
    push_row(panel, "reset_count", std::to_string(summary.reset_count));
    push_row(panel, "transport_timeline_revision",
        std::to_string(summary.transport_timeline_revision));
    push_row(panel, "current_tick", std::to_string(summary.current_tick));

    const std::span<const gameplay::ActiveCue> cues = scheduler.active_cues();
    const std::size_t row_count = std::min(cues.size(), options.max_active_cue_rows);
    for (std::size_t i = 0; i < row_count; ++i) {
        const gameplay::ActiveCue& cue = cues[i];
        std::ostringstream line;
        line << "cue#" << cue.cue_id << " schedule=" << cue.schedule_index
             << " hit_tick=" << cue.cue.hit_tick
             << " channel=" << cue.cue.channel_index;
        if (tempo_map != nullptr && tempo_map->valid()) {
            const std::int64_t delta_ticks =
                static_cast<std::int64_t>(cue.cue.hit_tick) - static_cast<std::int64_t>(cursor_tick);
            line << " delta_ticks=" << delta_ticks;
            const rhythm::TimelineMicroseconds hit_us =
                tempo_map->microseconds_from_tick(cue.cue.hit_tick);
            const rhythm::TimelineMicroseconds cursor_us =
                tempo_map->microseconds_from_tick(cursor_tick);
            line << " delta_ms=" << std::fixed << std::setprecision(3)
                 << static_cast<double>(hit_us - cursor_us) / 1000.0;
        }
        panel.body_lines.push_back(line.str());
    }
    if (cues.size() > row_count) {
        std::ostringstream line;
        line << "active: ... +" << (cues.size() - row_count) << " more";
        panel.body_lines.push_back(line.str());
    }

    const std::span<const gameplay::CueSchedulerEvent> events = scheduler.events();
    const std::size_t event_show = std::min(events.size(), options.max_event_rows);
    for (std::size_t i = 0; i < event_show; ++i) {
        const gameplay::CueSchedulerEvent& event = events[i];
        std::ostringstream line;
        line << "event[" << i << "] kind=";
        switch (event.kind) {
        case gameplay::CueSchedulerEventKind::Spawned:
            line << "spawned";
            break;
        case gameplay::CueSchedulerEventKind::Updated:
            line << "updated";
            break;
        case gameplay::CueSchedulerEventKind::Despawned:
            line << "despawned";
            break;
        }
        line << " cue=" << event.cue.cue_id
             << " schedule=" << event.cue.schedule_index;
        if (event.discontinuity_reason != gameplay::TransportDiscontinuityReason::None) {
            line << " discontinuity=" << gameplay::to_string(event.discontinuity_reason);
        }
        panel.body_lines.push_back(line.str());
    }
    if (events.size() > event_show) {
        std::ostringstream line;
        line << "events: ... +" << (events.size() - event_show) << " more";
        panel.body_lines.push_back(line.str());
    }

    return panel;
}

} // namespace reaktio::tools
