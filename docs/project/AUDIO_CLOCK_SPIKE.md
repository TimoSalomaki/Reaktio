# Audio Clock Spike Plan

## Purpose

Validate the authoritative song-position model before judgement logic, replay checks, and content tooling depend on it.

## Questions to Answer

- What timing source best represents audible playback: queued samples, callback-produced sample cursor, device-reported position, or a hybrid model?
- What startup latency and seek latency characteristics does SDL3 expose on Windows?
- How much drift accumulates between the simulation clock and the audio path over long playback sessions?
- Which correction policy keeps clocks aligned without changing already-issued judgements?

## Deliverables

- Measurement notes for play, pause, stop, seek, restart, and loop transitions.
- Drift traces collected over at least 10-minute and 30-minute playback sessions.
- A recommendation for authoritative sample cursor ownership and correction thresholds.
- A short list of edge cases that must be covered by later automated tests.

## Exit Criteria

- The chosen authoritative position model is documented and accepted.
- Loop and seek semantics are frozen.
- Correction policy is defined in terms of when to snap, when to blend, and what never changes retroactively.