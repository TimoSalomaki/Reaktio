# ADR 0002: Dependency and Build Strategy

- Status: Accepted
- Date: 2026-04-06

## Context

The engine depends on SDL3 and bgfx, and bgfx integration is sensitive to source layout and tool availability.

## Decision

- Core third-party code is vendored in source form through pinned git submodules.
- SDL3 is vendored at `external/SDL`.
- bgfx integration uses the maintained `bgfx.cmake` wrapper vendored at `external/bgfx.cmake`, which in turn pins `bx`, `bimg`, and `bgfx` revisions.
- The root build is CMake-based and builds vendored dependencies via `add_subdirectory`.
- Dependency bootstrap is performed with `tools/bootstrap-deps.ps1` and `git submodule update --init --recursive`.

## Consequences

- Checkouts are reproducible and do not depend on machine-local package manager state.
- CI and developer machines can build the same revisions.
- Dependency updates must be intentional and documented when submodule commits change.