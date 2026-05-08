#pragma once

#include "reaktio/rhythm/TempoMap.hpp"
#include "reaktio/tools/InspectorPanel.hpp"

#include <cstdint>

namespace reaktio::tools {

// Read-only inspector over the engine's tempo map. Reports the immutable
// authoring shape (tempo changes, time-signature changes, stops, warps)
// alongside a "current position" decoded against an arbitrary cursor
// tick so the same inspector serves authoring tools and live runtime.
//
// Stays UI-agnostic: panels are rendered as text by InspectorPanel
// helpers; ImGui (or any other UI) consumes the same panels later.

struct TempoMapInspectorOptions {
    // Limit the number of tempo / time-signature / stop / warp rows the
    // inspector emits to avoid unbounded growth on long charts. Detail
    // beyond this limit appears as a "... +N more" body line.
    std::size_t max_authoring_entries{8};
};

[[nodiscard]] InspectorPanel build_tempo_map_inspector(
    const rhythm::TempoMap& tempo_map,
    rhythm::ChartTick cursor_tick = 0,
    TempoMapInspectorOptions options = {});

} // namespace reaktio::tools
