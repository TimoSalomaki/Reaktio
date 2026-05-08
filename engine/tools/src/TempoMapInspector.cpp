#include "reaktio/tools/TempoMapInspector.hpp"

#include <iomanip>
#include <sstream>

namespace reaktio::tools {

namespace {

[[nodiscard]] double bpm_for_microseconds_per_quarter(rhythm::TimelineMicroseconds us_per_quarter) noexcept {
    if (us_per_quarter <= 0) {
        return 0.0;
    }
    return 60'000'000.0 / static_cast<double>(us_per_quarter);
}

template <typename Vec>
void emit_overflow_note(InspectorPanel& panel, const Vec& vec, std::size_t shown, const char* label) {
    if (vec.size() > shown) {
        std::ostringstream line;
        line << label << ": ... +" << (vec.size() - shown) << " more";
        panel.body_lines.push_back(line.str());
    }
}

} // namespace

InspectorPanel build_tempo_map_inspector(
    const rhythm::TempoMap& tempo_map,
    rhythm::ChartTick cursor_tick,
    TempoMapInspectorOptions options) {
    InspectorPanel panel{};
    panel.id = "tempo-map";
    panel.title = "Tempo Map";

    if (!tempo_map.valid()) {
        push_row(panel, "valid", "0", InspectorRowSeverity::Error);
        push_row(panel, "last_error", std::string(tempo_map.last_error()), InspectorRowSeverity::Error);
        return panel;
    }

    const rhythm::TempoMapDefinition& definition = tempo_map.definition();
    const rhythm::TempoMapConfig& config = tempo_map.config();
    push_row(panel, "valid", "1");
    push_row(panel, "ticks_per_quarter_note", std::to_string(config.ticks_per_quarter_note));
    push_row(panel, "sample_rate_hz", std::to_string(config.sample_rate_hz));
    push_row(panel, "tempo_changes", std::to_string(definition.tempo_changes.size()));
    push_row(panel, "time_signature_changes", std::to_string(definition.time_signature_changes.size()));
    push_row(panel, "stops", std::to_string(definition.stops.size()));
    push_row(panel, "warps", std::to_string(definition.warps.size()));

    // Current cursor decoding. Useful for both authoring tools (scrub)
    // and live runtime (current transport position).
    const rhythm::RhythmPosition cursor = tempo_map.position_from_tick(cursor_tick);
    {
        std::ostringstream stream;
        stream << "tick=" << cursor.tick
               << " us=" << cursor.microseconds
               << " sec=" << std::fixed << std::setprecision(3) << cursor.seconds;
        push_row(panel, "cursor_position", stream.str());
    }
    {
        std::ostringstream stream;
        stream << "beat=" << cursor.beat.whole_beats
               << "+" << cursor.beat.tick_offset_in_beat
               << "/" << cursor.beat.ticks_per_beat;
        push_row(panel, "cursor_beat", stream.str());
    }
    {
        std::ostringstream stream;
        stream << "bar=" << cursor.bar.bar_index
               << " beat_in_bar=" << cursor.bar.beat_index_in_bar
               << " sig=" << cursor.bar.beats_per_bar << "/" << cursor.bar.beat_unit;
        push_row(panel, "cursor_bar", stream.str());
    }

    // Authoring tables. Keep them as body lines so the row block stays
    // scannable; truncate at max_authoring_entries with an explicit
    // overflow note.
    const std::size_t tempo_show =
        std::min<std::size_t>(definition.tempo_changes.size(), options.max_authoring_entries);
    for (std::size_t i = 0; i < tempo_show; ++i) {
        const rhythm::TempoChange& change = definition.tempo_changes[i];
        std::ostringstream line;
        line << "tempo[" << i << "] tick=" << change.start_tick
             << " us_per_quarter=" << change.microseconds_per_quarter_note
             << " bpm=" << std::fixed << std::setprecision(2)
             << bpm_for_microseconds_per_quarter(change.microseconds_per_quarter_note);
        panel.body_lines.push_back(line.str());
    }
    emit_overflow_note(panel, definition.tempo_changes, tempo_show, "tempo");

    const std::size_t sig_show =
        std::min<std::size_t>(definition.time_signature_changes.size(), options.max_authoring_entries);
    for (std::size_t i = 0; i < sig_show; ++i) {
        const rhythm::TimeSignatureChange& change = definition.time_signature_changes[i];
        std::ostringstream line;
        line << "sig[" << i << "] tick=" << change.start_tick
             << " " << change.numerator << "/" << change.denominator;
        panel.body_lines.push_back(line.str());
    }
    emit_overflow_note(panel, definition.time_signature_changes, sig_show, "sig");

    const std::size_t stop_show =
        std::min<std::size_t>(definition.stops.size(), options.max_authoring_entries);
    for (std::size_t i = 0; i < stop_show; ++i) {
        const rhythm::StopSegment& stop = definition.stops[i];
        std::ostringstream line;
        line << "stop[" << i << "] tick=" << stop.start_tick
             << " duration_us=" << stop.duration_microseconds;
        panel.body_lines.push_back(line.str());
    }
    emit_overflow_note(panel, definition.stops, stop_show, "stop");

    const std::size_t warp_show =
        std::min<std::size_t>(definition.warps.size(), options.max_authoring_entries);
    for (std::size_t i = 0; i < warp_show; ++i) {
        const rhythm::WarpSegment& warp = definition.warps[i];
        std::ostringstream line;
        line << "warp[" << i << "] tick=" << warp.start_tick
             << " duration_ticks=" << warp.duration_ticks;
        panel.body_lines.push_back(line.str());
    }
    emit_overflow_note(panel, definition.warps, warp_show, "warp");

    return panel;
}

} // namespace reaktio::tools
