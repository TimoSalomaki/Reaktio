# ADR 0001: Engine Charter and Invariants

- Status: Accepted
- Date: 2026-04-06

## Context

The project needs a stable definition of what the engine is for and which constraints must never be traded away for convenience.

## Decision

- The engine serves three priority mode families: falling-character or falling-note games, rail or lane action games, and spatial obstacle or survival games.
- Windows desktop is the first-class bootstrap platform.
- Deterministic gameplay, replay safety, content validation, and authoring feedback outrank short-term feature breadth.
- Simulation uses a fixed step.
- Audio transport owns authoritative song position.
- Rendering consumes extracted presentation state instead of mutating gameplay state.

## Consequences

- Early prototypes must preserve determinism and the transport model.
- Features that require breaking these invariants are deferred or rejected.