# ADR 0003: Platform and Render Boundaries

- Status: Accepted
- Date: 2026-04-06

## Context

The engine must use SDL3 and bgfx together without mixing responsibilities or letting gameplay logic grow direct platform or renderer dependencies.

## Decision

- SDL3 owns app bootstrap, windows, input, text input, controller access, timers, audio-device access, and platform utilities.
- bgfx owns renderer setup and draw submission.
- Game and mode code never calls raw bgfx or SDL platform lifecycle APIs directly.
- The renderer receives extracted presentation data from simulation-facing systems.

## Consequences

- SDL renderer APIs are not used.
- Future back-end changes remain isolated behind engine modules.
- Debugging and profiling stay more tractable because platform and render responsibilities are explicit.