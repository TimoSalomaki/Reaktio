#pragma once

#include "reaktio/gameplay/IGameMode.hpp"
#include "reaktio/gameplay/ModeFlow.hpp"
#include "reaktio/gameplay/Scoring.hpp"
#include "reaktio/tools/InspectorPanel.hpp"

namespace reaktio::tools {

// Read-only inspector over a game mode's runtime state. Panels:
//   - Mode descriptor (id, family, capabilities, replay-supported flag)
//   - ModeFlow snapshot (state, reason, transition count, results presence)
//   - ScoreSummary (score, combo, accuracy, grade, run state)
//
// Each is its own panel so consumers can place them independently. The
// build_*_inspector() helpers pull the data straight from engine state
// without snapshotting / copying anything beyond the produced panel.

[[nodiscard]] InspectorPanel build_mode_descriptor_inspector(
    const gameplay::ModeDescriptor& descriptor);

[[nodiscard]] InspectorPanel build_mode_flow_inspector(
    const gameplay::ModeFlowSnapshot& snapshot);

[[nodiscard]] InspectorPanel build_score_summary_inspector(
    const gameplay::ScoreSummary& summary);

} // namespace reaktio::tools
