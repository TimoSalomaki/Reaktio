# Authoring Schemas

## Purpose

This document defines the canonical authoring schemas for the bootstrap content pipeline.

## Cooked Content Manifest Schema

Top-level cooked manifests are emitted under `content/cooked/charts/manifest.ini` and `content/cooked/render/manifest.ini`.

Both manifests use a `[meta]` section with:

- `schema`
- `manifest_version`
- `generator`

Each cooked asset entry uses a section family such as `[chart.<id>]`, `[texture.<id>]`, `[mesh.<id>]`, or `[font.<id>]`.

Shared entry keys:

- `runtime_label`
- `version`
- `source`: path relative to the repository root when possible
- `source_hash`: deterministic `fnv1a64:` hash of the authored source file
- `payload`: path relative to the manifest file directory
- `payload_hash`: deterministic `fnv1a64:` hash of the cooked payload descriptor
- `dependencies`: comma-separated cooked file dependencies relative to the manifest file directory
- `dependency_hashes`: comma-separated `path=hash` entries aligned with `dependencies`

For chart manifests, `dependencies` may be empty when the cooked chart payload has no additional file dependencies beyond the payload file itself. For render manifests, dependencies typically include the cooked binary container referenced by the payload descriptor, such as a `.dds`, `.mesh.bin`, or `.fontatlas.bin` file.

## Development Hot Reload

The runtime smoke config supports a `[hot_reload]` section for development polling.

Supported keys:

- `enabled`
- `poll_interval_seconds`
- `watch_charts`
- `watch_shaders`
- `watch_materials`
- `watch_selected_content`
- `chart_manifest_path`
- `shader_manifest_path`
- `material_manifest_path`
- `selected_content_manifest_path`

By default the watcher polls the cooked chart manifest, the cooked render manifest, the cooked shader-program manifest, and a material-manifest path reserved for future material authoring.

Current runtime behavior:

- Chart changes reload the cooked chart library in place.
- Selected cooked render content changes reload the cooked render asset library in place.
- Shader and material changes publish typed hot-reload events and diagnostics as pending work until live GPU program and material rebinding is implemented.

### Cooked Render Manifest Example

```ini
[meta]
schema = reaktio.cooked.render_asset_manifest.v1
manifest_version = 1
generator = reaktio_content_cooker

[texture.reference.sandbox.texture.cue]
runtime_label = cooked.texture.reference-sandbox-cue
version = 1
source = content/raw/render/textures/reference-sandbox-cue.bmp
source_hash = fnv1a64:9974afc8d39f995c
payload = textures/reference-sandbox-cue.texture.ini
payload_hash = fnv1a64:cfdf7fac7ffe2530
dependencies = textures/reference-sandbox-cue.dds
dependency_hashes = textures/reference-sandbox-cue.dds=fnv1a64:10c829ff2f345c00
```
At this stage the goal is to freeze file shapes, identifier rules, and required fields before cookers and validators harden around them. Runtime still loads cooked data, but raw authoring assets should already target these schemas.

## Format Choice

- Raw authoring schemas use UTF-8 INI files with section headers and `key = value` entries.
- Comments use `#` or `;`.
- Paths are relative to the schema file unless explicitly documented otherwise.
- Stable IDs use lower-case dotted strings such as `song.reference.sandbox` or `mode.reference.sandbox`.
- Durations and trigger times use integer units where authoritative: chart timing uses ticks, transport preview windows use milliseconds, and sample-accurate data remains cook-time derived.

This choice is intentionally conservative. It matches the current repository conventions and keeps bootstrap validators simple. The eventual cooked formats remain free to change without changing authoring IDs or section names.

## Common Header Contract

Every authoring file family begins with a `[meta]` section.

Required keys:

- `schema`: schema identifier such as `reaktio.chart.v1`
- `id`: stable content identifier
- `display_name`: human-readable title

Optional keys:

- `author`
- `description`
- `tags`: comma-separated tag list
- `source_revision`: arbitrary authoring revision marker

## Chart Schema

Chart files define playable timing content and mode-facing authored events.

Required sections:

- `[meta]`
- `[audio]`
- `[tempo]`

Optional section families:

- `[scroll_profile.<id>]`
- `[cue.<id>]`
- `[event.<id>]`
- `[camera.<id>]`
- `[text_prompt.<id>]`
- `[vfx.<id>]`

### Chart Manifest Schema

Raw chart manifests live under `content/raw/charts/manifest.ini`.

Required section family:

- `[chart.<id>]`

Required keys:

- `runtime_label`
- `source`

`source` is a path relative to the manifest file. The section suffix must match the chart `[meta] id` value.

### `[audio]`

Required keys:

- `clip_id`: audio clip content ID
- `preview_start_ms`
- `preview_end_ms`

Optional keys:

- `lead_in_ms`
- `tail_out_ms`

### `[tempo]`

Required keys:

- `ticks_per_quarter`
- `beat_zero_offset_ms`

Optional keys:

- `default_scroll_profile`

Tempo and signature changes are authored as repeated `[event.<id>]` sections with `kind = tempo_change`, `kind = time_signature`, `kind = stop`, or `kind = warp`.

### `[cue.<id>]`

Required keys:

- `kind`: `tap`, `hold`, or `hazard`
- `tick`

Optional keys:

- `duration_ticks`
- `lane`
- `channel`
- `scroll_profile`
- `judgement_profile`
- `hazard_profile`

### `[event.<id>]`

`[event.<id>]` sections are used for tempo-map edits and generic trigger events.

Required keys:

- `kind`
- `tick`

Supported `kind` values:

- `tempo_change`: requires `bpm` or `microseconds_per_quarter_note`
- `time_signature`: requires `numerator` and `denominator`
- `stop`: requires `duration_ms` or `duration_microseconds`
- `warp`: requires `duration_ticks`
- `trigger`: requires `trigger_id`, optional `duration_ticks`, optional `payload`

### `[camera.<id>]`

Required keys:

- `tick`
- `camera_action_id`

Optional keys:

- `duration_ticks`
- `payload`

### `[text_prompt.<id>]`

Required keys:

- `tick`
- `prompt_text` or `prompt_token`

Optional keys:

- `duration_ticks`
- `locale_table_id`

### `[vfx.<id>]`

Required keys:

- `tick`
- `effect_id`

Optional keys:

- `duration_ticks`
- `payload`

### Chart Example

```ini
[chart.chart.reference.sandbox.normal]
runtime_label = cooked.chart.reference-sandbox-normal
source = reference-sandbox-normal.chart.ini

[meta]
schema = reaktio.chart.v1
id = chart.reference.sandbox.normal
display_name = Reference Sandbox Normal
author = reaktio.team

[audio]
clip_id = clip.reference.sandbox.preview
preview_start_ms = 12000
preview_end_ms = 28000
lead_in_ms = 500

[tempo]
ticks_per_quarter = 960
beat_zero_offset_ms = 0
default_scroll_profile = scroll.default

[scroll_profile.scroll.default]
units_per_second = 720.0
spawn_lead_ticks = 2880
release_tail_ticks = 480

[cue.note.0001]
kind = tap
tick = 1920
lane = 1

[cue.hold.0001]
kind = hold
tick = 2880
duration_ticks = 960
lane = 2

[cue.hazard.0001]
kind = hazard
tick = 4800
lane = 3
hazard_profile = hazard.wall

[event.tempo.0001]
kind = tempo_change
tick = 0
bpm = 120.0

[event.trigger.0001]
kind = trigger
tick = 5760
trigger_id = trigger.flash
payload = state:on

[camera.pulse.0001]
tick = 6240
camera_action_id = camera.pulse
payload = intensity:0.8

[text_prompt.sync.0001]
tick = 6720
prompt_text = SYNC

[vfx.spark.0001]
tick = 7200
effect_id = vfx.spark
payload = color:cyan
```

## Mode Metadata Schema

Mode metadata files define how a mode family binds authored content, rulesets, and presentation defaults.

Required sections:

- `[meta]`
- `[mode]`
- `[assets]`

Optional sections:

- `[practice]`
- `[presentation]`

### `[mode]`

Required keys:

- `mode_id`
- `family`: `typing`, `rail`, `spatial`, or future mode family IDs
- `ruleset`

Optional keys:

- `supports_text_input`
- `supports_pointer_input`
- `supports_gamepad_input`
- `default_chart_id`

### `[assets]`

Optional keys reference stable content IDs:

- `material_set`
- `font_set`
- `ui_theme`
- `camera_profile`

### Mode Metadata Example

```ini
[meta]
schema = reaktio.mode.v1
id = mode.reference.sandbox
display_name = Reference Sandbox

[mode]
mode_id = mode.reference.sandbox
family = rail
ruleset = ruleset.reference.sandbox
supports_text_input = false
supports_gamepad_input = true
default_chart_id = chart.reference.sandbox.normal

[assets]
material_set = materials.reference.sandbox
font_set = fonts.reference.debug
ui_theme = ui.reference.debug
```

## Shader Program Schema

Raw shader manifests live under `content/raw/render/shaders/manifest.ini`.

Optional sections:

- `[meta]`

Required section family:

- `[program.<id>]`

### `[program.<id>]`

Required keys:

- `runtime_label`
- `vertex`
- `fragment`
- `varying_def`

Optional keys:

- `include_dirs`: comma-separated include directories relative to the manifest file
- `defines`: comma-separated preprocessor defines passed to `shaderc`

Each program is compiled offline for every bgfx backend profile the current shipping platform supports. On Windows this means the cooked output includes Direct3D 11 (`dxbc`), Direct3D 12 (`dxil`), Vulkan (`spirv`), OpenGL (`glsl`), and OpenGL ES (`essl`) variants.

### Shader Program Example

```ini
[meta]
schema = reaktio.shader_program_manifest.v1

[program.program.unlit.color]
runtime_label = cooked.program.unlit.color
vertex = vs_unlit_color.sc
fragment = fs_unlit_color.sc
varying_def = varying.def.sc
include_dirs = .
```

## Texture Schema

Raw texture manifests live under `content/raw/render/textures/manifest.ini`.

Optional sections:

- `[meta]`

Required section family:

- `[texture.<id>]`

### `[texture.<id>]`

Required keys:

- `runtime_label`
- `source`
- `width`
- `height`
- `usage`: `color_opaque`, `color_alpha`, `normal_map`, or `ui`

Optional keys:

- `quality`: `default`, `fastest`, or `highest`
- `mips`: `true` or `false`
- `max_size`
- `premultiply_alpha`: `true` or `false`
- `linear`: `true` or `false`
- `format_override`
- `container_override`

Platform rules are resolved during cooking. On Windows, `color_opaque` textures default to `BC1` in a `dds` container, `color_alpha` defaults to `BC3`, `normal_map` defaults to `BC5`, and `ui` remains `RGBA8`. Other platforms currently fall back to `RGBA8` in a `ktx` container until platform-specific compression targets are added.

The cook step emits a `.texture.ini` metadata file beside the compiled container payload so runtime loading can validate the cooked texture contract without depending on raw authoring formats.

### Texture Example

```ini
[meta]
schema = reaktio.texture_manifest.v1

[texture.reference.sandbox.texture.cue]
runtime_label = cooked.texture.reference-sandbox-cue
source = reference-sandbox-cue.bmp
width = 2
height = 2
usage = color_opaque
quality = highest
mips = true
```

## Mesh Schema

Raw mesh manifests live under `content/raw/render/meshes/manifest.ini`.

Optional sections:

- `[meta]`

Required section family:

- `[mesh.<id>]`

### `[mesh.<id>]`

Required keys:

- `runtime_label`
- `source`: relative path to an `.obj`, `.gltf`, or `.glb` file

Optional preprocessing keys:

- `scale`
- `flip_v`: `true` or `false`
- `ccw`: `true` or `false`
- `obb_steps`
- `pack_normals`: `0` or `1`
- `pack_uv`: `0` or `1`
- `generate_tangents`: `true` or `false`
- `barycentric`: `true` or `false`
- `compress`: `true` or `false`
- `coordinate_system`: `lh-up+y`, `lh-up+z`, `rh-up+y`, or `rh-up+z`
- `output_name`

The cook step imports Wavefront OBJ and glTF 2.0 sources through `geometryc`, applies the requested preprocessing rules, and emits a `.mesh.ini` metadata file beside a bgfx geometry binary payload. Current defaults are `compress = true`, `pack_normals = 1`, `pack_uv = 1`, `scale = 1.0`, and `coordinate_system = lh-up+y`, which gives a compact baseline suitable for both 2.5D and 3D runtime content.

### Mesh Example

```ini
[meta]
schema = reaktio.mesh_manifest.v1

[mesh.reference.sandbox.mesh.rig]
runtime_label = cooked.mesh.reference-sandbox-rig
source = reference-sandbox-rig.obj
scale = 1.0
compress = true
pack_normals = 1
pack_uv = 1
flip_v = false
ccw = false
coordinate_system = lh-up+y
```

## Material Schema

Material files define authored shader permutations, textures, and uniform defaults without embedding renderer-internal handles.

Required sections:

- `[meta]`
- `[material.<id>]`

Optional section families:

- `[texture.<material_id>.<slot>]`
- `[uniform.<material_id>.<name>]`

### `[material.<id>]`

Required keys:

- `program`: shader program content ID

Optional keys:

- `blend_mode`: `opaque`, `alpha`, or `additive`
- `depth_test`: `disabled`, `less`, or `less_equal`
- `depth_write`: `true` or `false`
- `cull_mode`: `none`, `cw`, or `ccw`

### Material Example

```ini
[meta]
schema = reaktio.material.v1
id = materials.reference.sandbox
display_name = Reference Sandbox Materials

[material.note.default]
program = program.unlit.color
blend_mode = alpha
depth_test = disabled
depth_write = false

[uniform.note.default.tint]
type = vec4
value = 1.0,1.0,1.0,1.0
```

## Font Schema

Raw font manifests live under `content/raw/render/fonts/manifest.ini`.

Required sections:

- `[meta]`
- `[font.<id>]`

Optional section families:

- `[charset.<font_id>.<name>]`
- `[fallback.<font_id>.<order>]`

### `[font.<id>]`

Required keys:

- `runtime_label`
- `source`: relative path to a TTF or OTF file
- `pixel_height`

Optional keys:

- `atlas_width`
- `atlas_height`
- `atlas_padding`
- `sdf`: `true` or `false`
- `line_spacing`
- `output_name`

If no charset sections are defined for a font, the cooker defaults to Basic Latin (`U+0020-U+007E`).

### `[charset.<font_id>.<name>]`

Optional keys:

- `ranges`: comma-separated Unicode ranges such as `U+0020-U+007E`
- `codepoints`: comma-separated individual Unicode codepoints such as `U+00E4,U+00F6`

At least one of `ranges` or `codepoints` must be present.

### `[fallback.<font_id>.<order>]`

Required keys:

- `font`: another authored font id to use when the primary font is missing a glyph

The cooker resolves glyphs against the primary font first, then through fallback entries in ascending `<order>`.

The cook step bakes a single-channel atlas payload and a `.font.ini` metadata file for each font. That metadata includes glyph advances, bitmap bearings, glyph sizes, UVs, line metrics, fallback ids, and the cooked atlas payload path.

### Font Example

```ini
[meta]
schema = reaktio.font_manifest.v1

[font.reference.sandbox.font.debug]
runtime_label = cooked.font.reference-sandbox-debug
source = ../../../../external/bgfx.cmake/bgfx/examples/runtime/font/roboto-regular.ttf
pixel_height = 18
atlas_width = 512
atlas_height = 256
atlas_padding = 2
sdf = false
line_spacing = 1.0
output_name = reference-sandbox-debug

[charset.reference.sandbox.font.debug.basic-latin]
ranges = U+0020-U+007E

[charset.reference.sandbox.font.debug.localized-latin]
codepoints = U+00C4, U+00D6, U+00DC, U+00DF, U+00E4, U+00E9, U+00F1, U+00F6, U+00FC, U+2013, U+2014, U+2026
```

## Game Configuration Schema

Game configuration files define authored game-level defaults that sit above engine runtime bootstrap config.

Required sections:

- `[meta]`
- `[game]`
- `[content]`

Optional sections:

- `[startup]`
- `[window]`
- `[input]`
- `[debug]`
- `[budget]`

### `[game]`

Required keys:

- `game_id`
- `primary_mode`

Optional keys:

- `title`
- `company`
- `save_namespace`

### `[content]`

Optional keys:

- `audio_manifest`
- `chart_manifest`
- `material_manifest`
- `font_manifest`
- `mode_manifest`

### Game Configuration Example

```ini
[meta]
schema = reaktio.game.v1
id = game.reference.sandbox
display_name = Reference Sandbox Game

[game]
game_id = game.reference.sandbox
primary_mode = mode.reference.sandbox
title = Reaktio Reference Sandbox

[content]
audio_manifest = audio/manifest.ini
chart_manifest = charts/manifest.ini
material_manifest = materials/manifest.ini
font_manifest = fonts/manifest.ini
mode_manifest = modes/manifest.ini

[startup]
boot_chart = chart.reference.sandbox.normal
show_debug_overlay = true
```

## Validation Rules

- Unknown sections or keys are warnings until a parser for that file family exists, then they become schema validation errors.
- Missing required sections or keys are always errors.
- Every authored ID must be globally unique within its file family.
- References between files always use stable dotted IDs, never file paths.
- Authoring files may reference raw source paths only for import-time assets such as audio clips or font files.

## Follow-On Work

The next implementation steps build directly on this contract:

1. Freeze the detailed chart data model per cue and event kind.
2. Implement raw-file validators against these schemas.
3. Build cookers that translate these authoring files into runtime manifests and binary payloads.