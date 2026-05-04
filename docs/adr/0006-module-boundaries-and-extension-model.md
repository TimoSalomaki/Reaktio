# ADR 0006: Module Boundaries and Extension Model

- Status: Accepted
- Date: 2026-04-06

## Context

The engine must stay extensible without creating a premature binary plugin ABI or a giant shared abstraction surface.

## Decision

- Engine modules expose a slim host interface to game modes.
- Version 1 mode extensibility is compile-time and source-level.
- Mode registration uses descriptors or factories wired in the composition root.
- The shared spatial runtime remains intentionally small. Mode-specific camera policy, lane logic, and interaction rules stay outside the core runtime.

## Consequences

- New modes can be added safely without destabilizing binary compatibility concerns.
- The core runtime is less likely to absorb mode-specific hacks.
- If a binary plugin ABI is ever needed later, it will be designed against proven mode boundaries rather than guessed upfront.

## Enforcement

- `GameModeRegistry::register_mode<ModeType>()` rejects any descriptor whose `api_version` does not equal `k_current_mode_api_version`. Drift in the source-level contract therefore fails at composition-root wiring, never silently at runtime.
- `IModeHost` is the only handle modes receive; widening it requires touching the engine and is reviewed as an API change.
- Smoke startup logs `Mode registry: api=N modes=M source-level=v1` so the active extension policy is visible in operations.

## Revisit Triggers

The decision to defer a binary plugin ABI must be revisited only when *all* of the following hold:

1. At least two shipped mode families (e.g. typing slice + rail slice + spatial slice) have stabilized on the source-level contract through a full content cycle.
2. A concrete external requirement — third-party mods, hot-swappable mode DLCs, or out-of-tree partner integrations — cannot be served by source-level inclusion in a reasonable build.
3. Binary stability surface area is small enough to own (host interface + descriptor + replay/save schemas) and the team is prepared to commit to versioning, deprecation, and ABI-break policies.

If only some of these conditions hold, the extension policy stays source-level and the request is rejected with a pointer to this ADR.