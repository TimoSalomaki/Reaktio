# ADR 0007: Authoritative Audio Clock

- Status: Accepted
- Date: 2026-04-06

## Context

Authoritative song position is the most critical technical choice in the engine. It affects cue scheduling, judgement accuracy, calibration, and replay determinism.

## Decision

- Authoritative song position is modeled in integer timing units rooted in the audio path.
- Startup latency, seek behavior, restart behavior, and loop transitions are part of the transport contract, not incidental implementation details.
- Drift correction is permitted only in ways that do not retroactively alter already-issued judgements.
- Before gameplay scoring stabilizes, a measurement spike must validate the chosen timing source on the real audio path.

## Consequences

- Transport implementation work must begin before rich gameplay scoring logic.
- Debug tooling needs explicit drift, latency, and correction-event visibility.
- Future tests must include long-session drift and seek-loop behavior.