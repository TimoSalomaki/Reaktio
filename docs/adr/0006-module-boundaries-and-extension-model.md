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