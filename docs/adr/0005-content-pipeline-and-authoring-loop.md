# ADR 0005: Content Pipeline and Authoring Loop

- Status: Accepted
- Date: 2026-04-06

## Context

Rhythm content needs reliable validation, cooking, and iteration. Runtime-only interpretation of raw data is too fragile.

## Decision

- Authoring data is human-readable and validated offline.
- Runtime loads cooked assets for normal execution.
- The minimum supported authoring loop is validator, lightweight preview, and starter mode template.
- Shader compilation is treated as a build pipeline concern and uses bgfx tools.

## Consequences

- Tooling work starts early enough to keep content iteration practical.
- Runtime complexity stays lower because it loads cooked formats.
- Asset pipeline errors are surfaced before playtesting.