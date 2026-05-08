# ADR 0008 - Editor Scope and Tooling Layer Strategy

## Status

Accepted - 2026-05-08

## Context

Phase 11 of the implementation plan asks the team to "decide whether a
full editor should be in-engine, external, or deferred until after
multiple shipped slices". By the time we reached this decision the
engine had already shipped:

- A typing slice (2D, text input + falling tokens).
- A rail slice (2.5D, lane-based with parallax, projectile, hold cues).
- A space slice (3D, tunnel/orbital camera with kinematic hazards).

Every slice ran on the same transport, chart, replay, and asset
infrastructure with no slice-specific timing hacks. The CLI tools at
this point were:

- `reaktio_content_cooker` / `reaktio_content_validator` for chart and
  asset processing.
- `reaktio_chart_preview` for scrubbing cooked charts.
- `reaktio_smoke` exercising the three slices headlessly with full
  inspector-text validation.

The original plan deliberately deferred a full visual editor (scope
guardrail in `IMPLEMENTATION_PLAN.md`).

## Decision

We are deferring an in-engine WYSIWYG editor for the foreseeable future.
Phase 11 ships an **inspector layer** instead:

1. A new `engine/tools/` static library produces **UI-framework-agnostic
   inspectors** as plain-data structs + text formatters
   (`InspectorPanel`).
2. The smoke is the inspector layer's first consumer: it builds and
   validates panel text against closed-form expectations as part of CI.
3. CLI driver programs (e.g. `reaktio_chart_preview`,
   `reaktio_content_validator`) consume the same inspectors from the
   command line.
4. A future ImGui (or any other UI framework) integration becomes a
   thin **presentation skin** on top of `InspectorPanel` data. No
   gameplay or engine code learns about the UI library. The eventual
   ImGui in-engine debug overlay will live behind a renderer-only
   adapter that turns `InspectorPanel` into immediate-mode widgets, and
   that adapter is the only place where ImGui appears in the build
   graph.

A full visual scene editor remains explicitly out of scope for the
foreseeable future. We do not gain enough authoring leverage from a
WYSIWYG editor at this stage to justify its maintenance burden against
text-based authoring workflows backed by inspectors and validators.

## Consequences

### Positive

- The same inspectors validate engine state in CI (`reaktio_smoke`),
  drive CLI tools, and will drive an in-engine debug overlay when one
  exists - one source of truth, three presentation surfaces.
- Inspectors are headless-testable. Panels are diffable text, so
  inspector regressions surface as smoke diffs without requiring a UI
  driver.
- Engine modules never depend on a UI library. ImGui (or any
  alternative) is contained behind a single adapter, and the build
  graph stays DAG-shaped.
- Authoring tools can be added incrementally: each tool is a thin CLI
  driver around an inspector + the existing content/transport modules.
- Defers a high-cost editor decision until shipped content production
  proves the gap is real.

### Negative

- Authors currently work with cooked-chart text, the chart preview CLI,
  and inspector dumps. We accept this friction in exchange for the
  zero coupling between authoring tools and the runtime renderer.
- The Phase 11 plan item "Build an in-engine debug UI using Dear ImGui
  or an equivalent tool layer" is satisfied by the tool-layer
  abstraction (the *layer* is shipped; the ImGui presentation skin is
  scoped as a follow-up), not by an immediate-mode UI integration.

### Neutral

- This decision can be revisited when at least one of the following
  becomes true:
  1. Authoring throughput becomes a measured bottleneck (text +
     inspector workflow can no longer keep up with content demand).
  2. We need pixel-accurate scene composition (not currently the case
     for any of the three shipped slice families).
  3. A specific contributor pipeline (third-party charters, modding,
     external content authors) demonstrably needs WYSIWYG controls.

## Architectural rules locked in by this ADR

- `engine/tools/` is **read-only over engine state**. Inspectors take
  const refs, return data structs, never mutate. This rule keeps the
  tooling layer safe to call from any thread that already holds the
  required engine reference.
- `engine/tools/` has **zero dependency on bgfx, SDL, or any UI
  library**. The library is fully headless-testable.
- Any future `engine/render/imgui/` (or equivalent) adapter must
  consume inspector data only. Engine code never includes ImGui (or
  any UI lib) headers.
- CLI tooling under `tools/` consumes the inspector layer + the
  existing content / transport / replay modules. CLI tooling must not
  bypass inspectors and reach directly into engine state when an
  inspector already exposes the data.

These rules keep the SDL clean-platform / bgfx clean-renderer
boundaries intact (per ADR 0003) and keep the engine architecture from
silently absorbing the editor's complexity.
