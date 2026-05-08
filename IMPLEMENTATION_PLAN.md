# Reaktio Engine Implementation Plan

## Target Outcome

Build a reusable rhythm-action engine that can host multiple gameplay paradigms without splitting into separate codebases for 2D, 2.5D, and 3D games. The engine should support at least these families of games from the same foundation:

- Falling-note or falling-character games such as typing trainers and piano-tile variants.
- Lane or rail based games such as rhythm shooters, side-scrollers, and runner hybrids.
- Spatial obstacle or survival games such as Super Hexagon-like or Geometry Dash-like experiences.

This plan assumes these baseline decisions:

- C++20 as the language baseline.
- SDL3 for platform concerns such as windowing, input, text input, controllers, timers, and audio device access.
- bgfx for all rendering.
- Top-level CMake as the project build system.
- Data-driven content and strict subsystem boundaries.

## Architectural Conclusions

1. SDL should own platform concerns only.

   Use SDL for application startup, windows, event pumping, keyboard and controller input, text input, timers, audio device access, file paths, and platform utilities. Do not use the SDL renderer. Do not create a graphics context through SDL when bgfx is active. SDL only provides the native window handle needed to initialize bgfx.

2. bgfx should be wrapped behind a renderer subsystem.

   Game code should never scatter raw bgfx calls across gameplay logic. Simulation should produce renderable data or render commands, and a dedicated render layer should translate that into bgfx views, programs, buffers, uniforms, and draw submissions.

3. The engine must separate simulation time, audio time, and render time.

   Rhythm gameplay breaks down quickly if frame time is treated as the source of truth. The authoritative music position should come from the audio transport. Simulation should run on a fixed step. Rendering should interpolate or extrapolate presentation state from simulation snapshots where appropriate.

4. One spatial foundation should serve 2D, 2.5D, and 3D.

   Do not create separate engines or separate gameplay stacks for each visual style. Keep the shared runtime intentionally small: transforms, cameras, layers, collision primitives, and render extraction should be generic, while camera policy, movement rules, lane logic, spatial conventions, and interaction models stay mode-specific. Orthographic 2D, perspective 2.5D, and full 3D should be different views of the same runtime, not forced into one bloated scene abstraction.

5. The rhythm domain should be first-class, not an add-on.

   Tempo maps, time signatures, scroll velocities, hit windows, latency calibration, chart cues, judgement rules, and practice tooling should be core subsystems. They should not live inside a specific game mode.

6. Gameplay rules should be pluggable.

   The engine core should provide timelines, input actions, scoring hooks, collision hooks, spawn scheduling, camera events, VFX events, and save or replay infrastructure. Individual game modes should define how cues become interactables, how they are judged, and how failure or scoring works. In version 1 this should mean compile-time registered modules with a clean source-level contract, not a binary plugin ABI.

7. Determinism and replay support should be built in early.

   Reaction games benefit from exact replay, regression testing, tuning, ghost playback, and later online or asynchronous features. Treat deterministic simulation, seeded randomness, and input recording as part of the architecture, not post-launch polish.

8. Asset cooking should happen offline.

   Shaders, textures, meshes, fonts, and charts should be validated and transformed into runtime-friendly formats before launch. Runtime should load cooked assets, not raw authoring data, except during explicit development hot-reload flows.

9. Avoid overcommitting to full general-purpose physics.

   Most rhythm-action games only need deterministic kinematics, simple collision volumes, path motion, and trigger volumes. Start with custom collision and movement primitives. Only add a heavy external physics engine if a specific game mode proves it is necessary.

10. Build for observability from day one.

   Debug overlays, frame timings, audio drift metrics, cue visualization, replay inspection, latency calibration tools, and chart validation tools are essential engine features for this domain. A minimal authoring loop should exist early: validation, preview, transport inspection, and a starter mode template.

11. Budgets should be explicit before optimization begins.

   Define frame, memory, draw-call, cue-density, and audio-latency budgets during planning and bootstrap. This keeps abstractions honest before they spread across the codebase.

## Recommended Supporting Libraries

These are support libraries, not replacements for SDL or bgfx.

- EnTT: Recommended for ECS-style world state, event dispatch, and registries. Keep core rhythm timing and chart evaluation as plain domain code instead of forcing everything into ECS.
- glm: Recommended for gameplay-facing math types. Keep math types wrapped at module boundaries so the engine is not permanently welded to a single math library.
- fmt and spdlog: Recommended for formatting and logging.
- Tracy: Recommended for CPU, frame, and memory profiling from the first playable prototype onward.
- Dear ImGui: Recommended for debug tools, inspectors, profilers, replay scrubbers, and chart debugging panels.
- doctest or Catch2: Recommended for unit and regression tests.
- toml++ or nlohmann/json: Recommended for human-authored config and early content formats. Cook them into binary data for runtime.
- dr_wav, dr_flac, and stb_vorbis or equivalent decoders: Recommended for ingest pipelines if SDL alone is not sufficient for decoding needs.

## Important Format and Pipeline Decisions

- Canonical song timing should use 64-bit integer units.
  Store either audio sample positions or high-resolution musical ticks derived from the tempo map. Avoid floating-point drift for authoritative timing.
- Canonical music assets should prefer WAV or FLAC during authoring.
  These are easier to seek, trim, and validate precisely. OGG can be supported for shipping builds. Avoid making MP3 the canonical source format because encoder delay and gapless behavior complicate precise rhythm timing.
- Chart timing and presentation timing should be separate.
  A cue occurs at a gameplay time. How early it spawns, how far it travels, and what camera or lane projection it uses should be presentation parameters, not the cue's identity.
- Input actions and text input should be separate concepts.
  Typing games need actual text input and layout-aware character handling. Action games need physical or mapped actions. The engine must support both at the same time.
- Shaders should be compiled offline per target backend profile.
  Treat shader compilation as part of the asset pipeline and build process, not as an ad hoc manual step.

## Proposed Module Map

- Foundation: memory ownership rules, logging, assertions, file system abstraction, configuration, UUID or handle utilities, job dispatch, profiling hooks.
- Platform: SDL application, window lifecycle, event pump, input devices, text input, clipboard, controller support, audio device abstraction, paths, and platform services.
- Render: bgfx bootstrap, renderer backend abstraction, view management, frame orchestration, material and shader management, buffers, cameras, text, sprites, meshes, particles, post-processing, debug rendering.
- Audio: playback transport, decoded clip streaming, mixer or routing, metronome, preview playback, device state, latency reporting.
- Rhythm: tempo map, transport clock, cue scheduling, judge windows, scroll velocity, practice modifiers, calibration, drift correction.
- Gameplay Framework: action maps, mode contracts, scoring, fail states, replay hooks, spawn services, collision services, camera events, mode lifecycle.
- World: entity or component storage, transforms, animation state, movement, collision primitives, scene extraction.
- Content: authoring formats, importers, cookers, validators, resource manifests, hot-reload hooks.
- Tooling: chart inspector, replay inspector, performance overlays, asset build tooling, packaging tools, optional editor.
- Games: game-mode modules and example games built on top of the engine.

## Proposed Repository Layout

```text
/cmake
/external
/tools
/content/raw
/content/cooked
/engine/foundation
/engine/platform
/engine/render
/engine/audio
/engine/rhythm
/engine/world
/engine/gameplay
/engine/content
/engine/debug
/games/templates
/games/typing_slice
/games/rail_slice
/games/space_slice
/tests/unit
/tests/integration
/tests/replay
/docs/adr
```

## Phased Implementation Plan

### Phase 0 - Preproduction and Architecture Freeze

- [x] Define hard project goals, non-goals, and target game families for the first year.
- [x] Freeze the core engine invariants: fixed-step simulation, audio-authoritative music clock, data-driven cues, and render extraction.
- [x] Define target device classes and hard CPU, GPU, memory, draw-call, cue-density, and audio-latency budgets for version 1.
- [x] Choose the exact dependency strategy: pinned git submodules versus locked package manager versions for SDL and support libraries.
- [x] Vendor bx, bimg, and bgfx at pinned revisions and document the update policy.
- [x] Decide whether runtime bgfx integration will use the official amalgamated build or a maintained CMake wrapper, then record the decision in an architecture note.
- [x] Write architecture decision records for time model, render model, content pipeline, replay model, module boundaries, and extension model.
- [x] Write a dedicated ADR for the authoritative audio clock covering startup latency, seek semantics, loop behavior, offset calibration, and drift correction policy.
- [x] Plan and document an early proof-of-concept spike for authoritative song-position measurement and transport correction before gameplay scoring depends on it.
- [x] Define coding standards, ownership conventions, error handling rules, and threading rules.
- [x] Define the engine API boundary between engine modules and individual game modes.
- [x] Decide the version 1 mode-extension strategy: compile-time registered modules first, runtime or binary plugins deferred unless justified later.
- [x] Define a naming scheme and identifier strategy for assets, cues, actions, and modes.
- [x] Define the minimum authoring loop required before serious content production: chart validation, transport inspection, lightweight preview, and a starter mode template.
- [x] Create an explicit scope blacklist for version 1 to prevent premature engine bloat.

### Phase 1 - Build System and Repository Bootstrap

- [x] Create the top-level CMake project with presets for Debug, Release, and RelWithDebInfo.
- [x] Add compiler warning policy, sanitizers where available, and static-analysis targets.
- [x] Integrate SDL3 into the build in a pinned and reproducible way.
- [x] Integrate bgfx, bx, and bimg in a reproducible way without relying on the bgfx example framework.
- [x] Add a bootstrap target or script for bgfx tools such as shaderc, texturec, and geometryc.
- [x] Set up a content output layout for raw and cooked assets.
- [x] Add formatting and lint targets.
- [x] Add CI for Windows first, then prepare for Linux and macOS expansion.
- [x] Add a minimal smoke-test executable target.
- [x] Add baseline profiling and telemetry hooks so the first smoke tests already report frame time, memory use, and timing metrics against the defined budgets.
- [x] Add a starter game-mode build target or stub so extension workflow is exercised from the first bootstrap.
- [x] Add a docs target or workflow for architecture notes and generated documentation.

### Phase 2 - Platform Layer and Application Shell

- [x] Create the SDL application entry layer and ensure all SDL lifecycle work happens on the intended main thread.
- [x] Implement window creation, resize handling, fullscreen switching, DPI awareness, and orderly shutdown.
- [x] Implement native window handle extraction from SDL and feed it into bgfx platform initialization.
- [x] Create the core application loop with explicit phases for event pump, fixed-step simulation, render extraction, and present.
- [x] Add a platform time service that clearly distinguishes wall clock, frame delta, and fixed-step accumulator time.
- [x] Implement keyboard, mouse, controller, and text-input event ingestion.
- [x] Add IME-safe text input handling for future typing and language-learning modes.
- [x] Add application configuration for renderer backend selection, window mode, VSync, and debugging flags.
- [x] Add crash-safe logging and startup diagnostics.
- [x] Add a basic in-engine debug overlay showing frame time, renderer backend, and input state.

### Phase 3 - Foundation, World, Mode Host, and Runtime Contracts

- [x] Create foundational utilities for IDs, handles, lifetime tracking, and ownership-safe resource access.
- [x] Establish the composition root so subsystems are wired explicitly rather than through hidden globals.
- [x] Define a slim mode-host contract early, covering lifecycle, transport access, input surfaces, scoring callbacks, and render-extraction hooks.
- [x] Register modes through compile-time descriptors or factories rather than runtime plugin loading in version 1.
- [x] Add the world model using EnTT or an equivalent registry-backed approach.
- [x] Keep the shared world model intentionally small and push mode-specific spatial policy out of core runtime.
- [x] Create transform components and a transform propagation strategy that works for both 2D and 3D hierarchies.
- [x] Add lightweight movement and collision primitives suitable for deterministic rhythm gameplay.
- [x] Create a resource registry that separates authoring identifiers from runtime handles.
- [x] Add configuration loading for engine settings, input bindings, and mode configuration.
- [x] Add an event or messaging strategy for subsystem communication without tight coupling.
- [x] Implement deterministic random number generation and seed management.
- [x] Add replay recording scaffolding for input streams and authoritative state checkpoints.
- [x] Build a minimal reference mode or sandbox that exercises lifecycle, input, transport stubs, and render extraction before the broader gameplay framework hardens.
- [x] Add a starter mode template that new games can copy without modifying engine internals.

### Phase 4 - Rendering Foundation with bgfx

- [x] Create the render subsystem wrapper that owns bgfx initialization, reset, frame submission, and shutdown.
- [x] Define a view or pass allocation policy so subsystems do not fight over bgfx view IDs.
- [x] Implement camera abstractions for orthographic 2D, perspective 2.5D, and free 3D.
- [x] Implement sprite, quad, text, mesh, line, and debug-draw render paths.
- [x] Add material, shader program, and uniform management.
- [x] Add dynamic and transient buffer management for note fields, particles, and procedurally generated geometry.
- [x] Add texture, mesh, and font resource loading for cooked assets.
- [x] Implement render extraction so gameplay produces render packets rather than calling bgfx directly.
- [x] Add batch and instancing paths for large note counts and repeated obstacles.
- [x] Add a simple post-processing chain for color grading, bloom, vignette, and screen-space feedback.
- [x] Add debug visualizations for collision shapes, cue lanes, timing lines, and spawn windows.

### Phase 5 - Audio, Transport, and Rhythm Core

- [x] Implement the SDL-based audio device layer and formalize how device latency is queried and reported.
- [x] Create audio clip loading and decoding paths for authoring-friendly source formats.
- [x] Implement the transport controller with play, pause, stop, seek, restart, preview, and loop regions.
- [x] Make the audio transport the authoritative song-position source for gameplay timing.
- [x] Prototype and lock down how authoritative song position is measured against the real audio path before finalizing judgement logic.
- [x] Implement a tempo map that supports BPM changes, time signatures, stops, and optional warps.
- [x] Implement conversions between samples, seconds, beats, bars, and chart ticks using 64-bit integer math where authoritative.
- [x] Define seek, restart, and loop semantics so transport state changes never create hidden score-affecting discontinuities.
- [x] Implement note scroll or travel models that are independent from cue identity.
- [x] Add latency calibration flows for audio output and input response.
- [x] Add drift detection and correction between simulation time and audio time.
- [x] Define a correction policy that keeps transport aligned without retroactively changing past judgements.
- [x] Implement hit windows, timing offsets, and judgement logic at the rhythm-domain level.
- [x] Add practice features such as speed modifiers, looping segments, and offset visualization.
- [x] Add rhythm debug tooling that can display current beat, bar, timing error, and scheduled upcoming cues.
- [x] Add a transport inspector showing reported audio time, simulation time, drift, latency estimate, and correction events.

### Phase 6 - Content Model and Asset Pipeline

- [x] Define authoring schemas for charts, mode metadata, materials, fonts, and game configuration.
- [x] Create a chart data model that can represent note cues, holds, hazards, triggers, camera events, text prompts, and VFX events.
- [x] Create a content cooking pipeline that validates raw data and emits cooked runtime assets.
- [x] Add offline shader compilation for all supported bgfx backend profiles you intend to ship.
- [x] Add texture conversion and compression rules by target platform.
- [x] Add mesh import and preprocessing rules for 2.5D and 3D content.
- [x] Add font processing for UI text, gameplay glyphs, and localized typing content.
- [x] Add content manifests with hashes, versioning, and dependency metadata.
- [x] Build a lightweight chart preview tool that can scrub through time without full gameplay startup.
- [x] Add hot-reload hooks for charts, shaders, materials, and selected content during development.
- [x] Add command-line validators for charts, timing consistency, and missing assets.

### Phase 7 - Gameplay Framework and Mode Module Contract

- [x] Finalize the richer mode API after the slim reference mode has proven the workflow.
- [x] Separate input actions, text input, and analog controls into distinct gameplay-facing abstractions.
- [x] Implement action maps, input contexts, rebinding, and device-profile support.
- [x] Add a generic cue scheduler that spawns gameplay objects based on transport time and mode rules.
- [x] Add a scoring API with judgements, combo, multiplier, fail state, grade, and per-section statistics.
- [x] Add a generic modifiers system for speed mods, autoplay, no-fail, mirrored lanes, and practice assists.
- [x] Add camera-event, screen-effect, and haptics hooks that modes can trigger without depending directly on platform code.
- [x] Add pause, restart, fail, success, practice, and results flow contracts.
- [x] Add replay integration so every mode can record and deterministically re-run inputs.
- [x] Add a minimal replay inspection view for recorded input timelines and judgement offsets before building full authoring-grade tools.
- [x] Keep version 1 mode extensibility compile-time and source-level; defer a binary plugin ABI until at least two shipped mode families prove the need.
- [x] Add save-data contracts for unlocks, settings, and per-song or per-mode stats.

### Phase 8 - Vertical Slice A: Typing and Falling-Character Mode

- [x] Implement a typing-focused mode module that uses text input rather than only physical scancodes.
- [x] Add glyph, grapheme, and keyboard-layout aware prompt handling.
- [x] Implement falling-character or falling-token lanes with configurable travel time and density.
- [x] Implement judgement rules for exact character matches, optional leniency, and combo preservation.
- [x] Add lesson content structures for exercises, word groups, and progression sets.
- [x] Add UI flows for mistake feedback, hand or key hints, and per-key performance tracking.
- [x] Add analytics for common error patterns and timing distributions.
- [x] Validate that this mode can run entirely on the shared engine stack without custom one-off hacks.

### Phase 9 - Vertical Slice B: Rail, Lane, and Runner-Style Mode

- [x] Implement path or rail abstractions that can drive player motion, enemies, and note carriers.
- [x] Add 2.5D camera rails, parallax layers, and billboarded or mixed 2D and 3D presentation.
- [x] Implement obstacle spawning, trigger volumes, and deterministic collision checks.
- [x] Implement hit-scan, projectile, or target-interaction modules for shooter-like gameplay.
- [x] Add mode rules for dodge, shoot, lane-swap, jump, slide, or hold interactions.
- [x] Add synchronized environment triggers for lights, hazards, and camera pulses.
- [x] Validate that lane-based and scrolling modes can reuse the same rhythm scheduler and scoring contracts.
- [x] Stress-test this slice with dense patterns, large obstacle counts, and rapid camera transitions.

### Phase 10 - Vertical Slice C: Spatial 3D and Obstacle-Heavy Mode

- [x] Implement full 3D camera, transform, and scene extraction flows on the same runtime.
- [x] Add deterministic kinematic movement for player avatars, rotating hazards, moving walls, and tunnel geometry.
- [x] Add reusable collision volumes for capsules, boxes, spheres, and oriented trigger zones.
- [x] Add camera and presentation systems for tunnel views, radial worlds, and high-speed spatial motion.
- [x] Add procedural or pattern-driven obstacle generation that remains replay-safe.
- [x] Add presentation hooks for music-reactive environment changes, post effects, and beat-synced geometry animation.
- [x] Validate that a Super Hexagon-like or Geometry Dash-like prototype can be built without bypassing the engine architecture.
- [x] Benchmark CPU, GPU, and audio stability under high-speed gameplay.

### Phase 11 - Tooling, Debugging, and Authoring Support

- [x] Build an in-engine debug UI using Dear ImGui or an equivalent tool layer.
- [x] Add inspectors for song transport, tempo maps, active cues, mode state, and replay data.
- [x] Expand the lightweight chart preview tool into a richer authoring-grade inspector.
- [x] Add an asset browser for cooked resources and dependency tracing.
- [x] Expand early replay inspection into a full replay viewer with timing-offset overlays and failure-state inspection.
- [x] Add a performance HUD with CPU frame cost, bgfx stats, draw-call counts, audio latency, and drift metrics.
- [x] Add authoring helpers for lane layout, path editing, camera cue placement, and trigger testing.
- [x] Decide whether a full editor should be in-engine, external, or deferred until after multiple shipped slices.

### Phase 12 - Testing, Optimization, and Release Engineering

- [ ] Add unit tests for tempo maps, time conversion, scoring, hit windows, and serialization.
- [ ] Add integration tests for song transport, cue scheduling, and mode lifecycle flows.
- [ ] Add replay-based regression tests for every shipped game mode.
- [ ] Add headless or minimal-render simulation tests for deterministic progression.
- [ ] Refine and enforce CPU, GPU, memory, draw-call, cue-density, and audio-latency budgets for each target class of game.
- [ ] Profile and optimize renderer submission, resource loading, collision, and scheduling under representative stress cases.
- [ ] Validate multi-backend rendering behavior where you plan to support more than one bgfx backend.
- [ ] Add packaging, patching, and versioned content deployment workflows.
- [ ] Add accessibility support such as remapping, color-safe visual cues, configurable timing feedback, and assist modes.
- [ ] Write engine and content documentation for mode authors, content authors, and future contributors.
- [ ] Harden maintained template projects per major mode family so new games start from supported patterns instead of custom forks.

## Recommended Milestone Order

If you want the shortest path to a future-proof engine without building a giant unproven framework first, use this milestone order:

1. Complete Phases 0 through 3 and prove the slim reference mode before hardening the broader gameplay framework.
2. Validate the authoritative audio clock, latency model, seek behavior, and drift-correction strategy before final judgement and scoring logic are treated as stable.
3. Bring up chart validation, lightweight preview tooling, and baseline telemetry before authoring large content sets.
4. Build the typing slice first to validate text input, timing, judgement, UI feedback, and content pipelines.
5. Build the rail or lane slice second to validate movement, collision, 2.5D presentation, and mode-module flexibility.
6. Build the spatial 3D slice third to validate that the same runtime survives a more demanding camera and collision model.
7. Only after those slices and templates stabilize, invest heavily in advanced tooling, broader rendering features, and content-authoring UX.

## Scope Guardrails

Keep these out of the critical path until the core engine proves itself across at least two very different game modes:

- Full visual editor with WYSIWYG scene editing.
- Heavy general-purpose rigid-body physics.
- Networked multiplayer.
- Complex scripting language embedding.
- Large open-world or streaming-scene systems.
- Procedural content systems that are unrelated to rhythm gameplay.

## Definition of Success

The engine architecture is working when all of the following are true:

- A 2D typing prototype, a 2.5D rail prototype, and a 3D obstacle prototype all share the same transport, chart, input, replay, and asset infrastructure.
- Game modes can be added without editing the renderer, platform layer, or audio transport in invasive ways.
- Chart validation, replay regression, and timing inspection tools can catch most gameplay regressions before manual playtesting.
- SDL remains a clean platform boundary and bgfx remains a clean renderer boundary.
- No shipped mode depends on bespoke timing hacks tied to frame rate or renderer behavior.