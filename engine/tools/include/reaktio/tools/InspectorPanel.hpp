#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reaktio::tools {

// Phase 11 - Tooling and authoring support.
//
// The tooling layer deliberately produces UI-framework-agnostic data
// models. Every inspector returns a populated InspectorPanel that any
// presentation backend (Dear ImGui, terminal CLI, web dashboard, etc.)
// can consume. The smoke validates panels as text; the eventual ImGui
// integration becomes a thin presentation skin that consumes the same
// panels without touching engine code.
//
// Architectural rules:
//   - tools/* is a one-way consumer of engine/* state. Inspectors take
//     const refs to engine state and never mutate it.
//   - tools/* has no dependency on bgfx, SDL, or ImGui. The library is
//     headless-testable.
//   - Each inspector has a small build_*_inspector() free function and a
//     format_*_inspector_text() free function. Both are pure.
//
// The InspectorPanel structure intentionally mirrors a typical
// debug-tools panel: a heading, a list of key/value rows, and a list of
// optional plain-text body lines for tabular content. Nesting is
// expressed as separate panels grouped by the consumer; no recursive
// containers are baked into the contract so consumers stay free to
// arrange panels into tabs, columns, or trees as they please.

enum class InspectorRowSeverity : std::uint8_t {
    Info = 0,        // Default. Black/white text in a typical UI.
    Notice = 1,      // Highlight (e.g. transport playing, mode in flow).
    Warning = 2,     // Approaching a budget threshold or a soft assertion.
    Error = 3,       // Hard violation (over budget, replay mismatch, etc.).
};

[[nodiscard]] constexpr std::string_view to_string(InspectorRowSeverity severity) noexcept {
    switch (severity) {
    case InspectorRowSeverity::Info:
        return "info";
    case InspectorRowSeverity::Notice:
        return "notice";
    case InspectorRowSeverity::Warning:
        return "warning";
    case InspectorRowSeverity::Error:
        return "error";
    }
    return "unknown";
}

struct InspectorRow {
    std::string label;
    std::string value;
    InspectorRowSeverity severity{InspectorRowSeverity::Info};
};

struct InspectorPanel {
    std::string id;          // Stable, machine-readable ID (e.g. "transport").
    std::string title;       // Human-readable title.
    std::vector<InspectorRow> rows;
    std::vector<std::string> body_lines;  // Optional supplementary lines (tables, deltas).
    bool empty() const noexcept { return rows.empty() && body_lines.empty(); }
};

// Append a row to a panel. Convenience helper to keep build_*_inspector
// implementations readable.
void push_row(
    InspectorPanel& panel,
    std::string_view label,
    std::string value,
    InspectorRowSeverity severity = InspectorRowSeverity::Info);

// Render a panel to a single multi-line string. Format is stable and
// safe to diff in regression tests. Each row appears as
// "  <label>=<value> [severity]" with severity suffix elided when Info.
[[nodiscard]] std::string format_inspector_panel(const InspectorPanel& panel);

// Render a list of panels as a single text block. Panels are separated
// by a blank line. Empty panels are skipped.
[[nodiscard]] std::string format_inspector_panels(
    const std::vector<InspectorPanel>& panels);

} // namespace reaktio::tools
