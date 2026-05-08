#pragma once

#include "reaktio/gameplay/CueScheduler.hpp"
#include "reaktio/rhythm/TempoMap.hpp"
#include "reaktio/tools/InspectorPanel.hpp"

#include <cstddef>

namespace reaktio::tools {

// Read-only inspector over a CueScheduler's active cues. Reports the
// scheduler summary (counts, tick, transport-revision) and a windowed
// table of the next-N upcoming active cues sorted by hit time.
//
// Keeps the inspector independent from any specific mode (CueScheduler
// is shared across all gameplay families). Modes wanting richer
// per-cue rendering can layer their own inspector on top while still
// consuming this one's panel as the foundational view.

struct ActiveCueInspectorOptions {
    std::size_t max_active_cue_rows{8};
    std::size_t max_event_rows{8};
};

[[nodiscard]] InspectorPanel build_active_cue_inspector(
    const gameplay::CueScheduler& scheduler,
    const rhythm::TempoMap* tempo_map = nullptr,
    rhythm::ChartTick cursor_tick = 0,
    ActiveCueInspectorOptions options = {});

} // namespace reaktio::tools
