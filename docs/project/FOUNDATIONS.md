# Project Foundations

## Goals

- Build a reusable rhythm-action engine for Windows first, with Linux and macOS kept viable through SDL3 and bgfx-friendly boundaries.
- Support three priority game families from one runtime: falling-character or falling-note play, rail or lane action, and spatial obstacle or survival play.
- Keep game rules modular so new modes are added by implementing mode modules rather than editing core platform, audio, or render code.
- Optimize for deterministic gameplay, replay safety, content validation, and rapid authoring feedback.

## Non-Goals for Version 1

- Runtime binary plugin ABI.
- Networked multiplayer.
- Full editor or WYSIWYG level authoring.
- General-purpose rigid body physics.
- Embedded scripting as a required engine layer.
- Non-rhythm open-world streaming or sandbox systems.

## Core Invariants

- Simulation is fixed-step and never uses raw frame delta as the source of truth.
- Audio transport owns authoritative song position.
- Rendering consumes extracted presentation data and does not drive gameplay state.
- SDL3 owns platform concerns and bgfx owns rendering concerns.
- Stable asset identifiers are data-facing strings; runtime handles are typed numeric IDs.
- Version 1 modes are compile-time registered modules.

## Target Device Classes

- Baseline development target: Windows 10 or 11 x64 desktop, 4 physical CPU cores, DX11-capable GPU from the late 2010s, 8 GiB RAM.
- Recommended target: Windows 10 or 11 x64 desktop, 6 or more physical CPU cores, DX12 or Vulkan-capable GPU, 16 GiB RAM.
- Deferred validation targets: Linux x64 and macOS Apple Silicon or x64 after the Windows-first bootstrap stabilizes.

## Version 1 Budgets

| Area | Budget | Notes |
| --- | --- | --- |
| Visual frame rate | 60 FPS baseline, 120 FPS preferred | Gameplay must remain correct below render refresh changes. |
| Main-thread frame work | <= 8.0 ms p95 on baseline target | Leaves margin for OS jitter and driver overhead. |
| Fixed-step simulation | <= 2.0 ms p95 | Measured without editor or debug overlays. |
| Render submission | <= 3.0 ms p95 | Excludes GPU completion time. |
| Draw calls | <= 1,500 per frame baseline | Prefer batching and instancing well below the cap. |
| Visible interactive cues | >= 10,000 simple cues sustained | Applies to sprite or lane-heavy content. |
| Runtime memory | <= 512 MiB steady-state baseline | Excludes external tooling. |
| Audio callback work | <= 1.0 ms p95 | No blocking locks or heap allocation. |
| Latency calibration granularity | 1 ms or better | Offsets are stored per device profile. |

## Coding and Ownership Rules

- C++20 is the language baseline for engine code.
- Public types use PascalCase. Functions, local variables, namespaces, and data members use snake_case.
- Directories are lower-case and file names match their primary type or responsibility.
- `std::unique_ptr` expresses ownership transfer. `std::span`, `std::string_view`, and references express non-owning access.
- Raw pointers are allowed only for nullable or clearly non-owning relationships.
- Do not throw exceptions across engine module boundaries. Use explicit results for recoverable errors and assertions for programmer mistakes.
- Avoid hidden singletons and global mutable state. The composition root wires subsystems explicitly.
- Code in frame-critical paths must avoid heap allocation unless a subsystem contract explicitly allows it.

## Threading Rules

- SDL lifecycle, event pumping, and window ownership stay on the main thread.
- bgfx submission stays behind the render subsystem. Mode code does not call bgfx directly.
- Audio callback code must not block, allocate, log to slow sinks, or touch non-lock-free shared state.
- Cross-thread communication must use explicit queues or well-scoped synchronization primitives.

## Engine API Boundary

- Engine modules expose services, contracts, and data structures.
- Game modes consume those services through a slim host interface.
- Game modes may define their own world policy, lane logic, and mode-specific data, but they do not reach into platform bootstrap or renderer internals.
- Asset and chart identifiers crossing module boundaries use stable string IDs. Runtime-internal references use strong typed handles.

## Naming and Identifier Strategy

- Stable content IDs use lower-case dotted strings such as `mode.typing.lesson.home_row` or `song.demo.intro`.
- Strong typed numeric IDs are reserved for runtime handles such as `ModeHandle` or future resource handles.
- CMake targets use the `reaktio_` prefix for concrete targets and `Reaktio::` for aliases.

## Minimum Authoring Loop

- Edit chart or mode data.
- Run validator.
- Preview the timeline without booting a full game flow.
- Start from a maintained starter mode template when creating a new mode family.

## Scope Blacklist

- Runtime binary plugins.
- Network replication and online features.
- Full visual editor.
- Heavy physics integration.
- General scripting runtime.
- Non-rhythm systemic feature work not required by the first three mode families.