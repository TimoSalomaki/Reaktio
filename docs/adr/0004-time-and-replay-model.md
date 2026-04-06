# ADR 0004: Time and Replay Model

- Status: Accepted
- Date: 2026-04-06

## Context

Rhythm gameplay requires stable timing and exact replay. Frame time alone is not authoritative enough.

## Decision

- The engine separates simulation time, audio time, and render time.
- Simulation uses a fixed-step update model.
- Audio transport provides authoritative song position.
- Judgement and scheduling operate on integer timing units derived from audio samples or chart ticks.
- Replay data records deterministic inputs plus the authoritative timing context required to reproduce outcomes.

## Consequences

- Systems cannot assume render cadence equals gameplay cadence.
- Timing data structures favor integer math over floating-point authority.
- Transport correctness must be validated before gameplay scoring is finalized.