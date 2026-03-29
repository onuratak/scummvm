# Grim Windows Parallax Handoff

This note is the current handoff for the Grim Fandango parallax prototype on Windows.

Use this before doing more work in Grim. The long-term target is still Monkey Island 1-3 in SCUMM, but the current prototype remains Grim-only because Grim already has a true 3D camera, mixed 2D/3D scene composition, and scene z-bitmaps that make desktop parallax experiments practical.

## Current goal

Validate a convincing fixed-monitor look-around effect on Windows, using external head tracking, before carrying the ideas into Monkey Island.

The current question is no longer "can things move?" The prototype now moves the 3D scene and backdrop together. The question is how to reduce reprojection artifacts enough to make the effect useful and transferable.

## Current implementation state

The prototype is active in the Grim engine and builds on Windows.

Files with active local changes:

- `engines/grim/grim.h`
- `engines/grim/grim.cpp`
- `engines/grim/set.cpp`
- `engines/grim/objectstate.h`
- `engines/grim/objectstate.cpp`
- `engines/grim/gfx_base.h`
- `engines/grim/gfx_opengl.cpp`
- `engines/grim/gfx_opengl_shaders.cpp`
- `engines/grim/gfx_opengl_shaders.h`
- `engines/grim/gfx_tinygl.cpp`
- `engines/grim/gfx_tinygl.h`
- `engines/grim/shaders/grim_background.fragment`
- `graphics/parallax.h`
- `devtools/windows/Send-GrimParallaxOpentrackTest.ps1`

What exists now:

- Grim-only parallax debug mode.
- Mouse input, auto-motion input, and `opentrack` UDP input.
- `opentrack` receiver for the official `UDP over network` packet shape: 6 doubles on UDP port `4242`.
- Recenter support for external tracking.
- Off-axis frustum support in Grim renderers.
- Camera-space translation derived from normalized parallax input.
- Depth-aware background reprojection in the OpenGL shader renderer using the Grim backdrop z-bitmap.
- CSV logging pipeline for per-frame telemetry.
- Independent debug overlay toggle.

What changed recently and matters:

- The biggest activation bug is fixed: when parallax is toggled with `F6`, the renderer is recreated so Grim can switch into the shader renderer path at runtime.
- The shader backdrop path is now confirmed active in live captures.
- Auxiliary `ObjectState` bitmap layers are no longer shifted heuristically while the shader-backed depth-aware background path is active. That reduced obvious seam drift between warped backdrop pixels and separately shifted room layers.

## Current debug controls

- `F6`: toggle Grim parallax test on/off
- `F7`: toggle mouse / auto input
- `F8`: recenter
- `F9`: toggle mouse / `opentrack`
- `F10`: toggle CSV logging
- `F11`: toggle on-screen debug overlay
- `[` / `]`: decrease / increase strength
- Mouse move inside the game window: drive input in mouse mode

Current persisted config keys:

- `grim_parallax_test`
- `grim_parallax_test_source`
- `grim_parallax_test_strength`
- `grim_parallax_test_overlay`
- `grim_parallax_test_opentrack_port`
- `grim_parallax_test_opentrack_range_x`
- `grim_parallax_test_opentrack_range_y`

## Windows build and test

Build command:

```powershell
. .\devtools\windows\Build-ScummVM.ps1 -Project scummvm
```

Built app:

- `build/msvc-vs2022/Releasex64/scummvm.exe`

UDP sender for testing without a live tracker:

- `devtools/windows/Send-GrimParallaxOpentrackTest.ps1`

Example:

```powershell
powershell -ExecutionPolicy Bypass -File .\devtools\windows\Send-GrimParallaxOpentrackTest.ps1
```

Recommended live test:

1. Launch `build/msvc-vs2022/Releasex64/scummvm.exe`.
2. Start Grim Fandango.
3. Press `F6`.
4. Press `F9`.
5. Press `F8` while sitting in neutral position.
6. Press `F11` to hide the overlay once the path is confirmed.
7. Press `F10` only when a CSV capture is needed.

In `opentrack`:

- Input: usually `NeuralNet Tracker`
- Output: `UDP over network`
- Host: `127.0.0.1`
- Port: `4242`

## Current verified status

This is what is now true, not hypothetical:

- External head tracking works on Windows through `opentrack`.
- `opentrack` X polarity is corrected to match the mouse/debug path.
- Enabling parallax at runtime can switch Grim into the shader renderer path.
- The depth-aware background path is active in real captures.
- The effect is perceptible and can create a visible look-around result on a desktop monitor.

Most recent CSV reviewed:

- `build/msvc-vs2022/Releasex64/grim_parallax_debug.csv`

Most recent capture summary:

- `204` frames
- `input_source = opentrack`
- `set_name = mo.set`
- `setup_name = mo_ddtws`
- `depth_aware_background_active = 1` for every frame
- `strength = 0.05`
- `bg_shift_near_x` reaches about `+/-10.84` px
- `bg_shift_far_x` reaches about `+/-5.42` px

That confirms the shader path is now live and depth-dependent motion is occurring.

## What is still broken

The effect works, but it still produces visible artifacts and is not production-ready.

Main remaining issues:

1. Screen-space reprojection artifacts in the main backdrop
   - The current shader warps a single rendered backdrop image using per-pixel depth from the z-bitmap.
   - At depth discontinuities there is no hidden content behind foreground edges, so stretching, tearing, and jagged transitions appear.
   - Newly exposed regions at image borders produce black gaps or edge smearing because the source image has no extra content.

2. Border reveal artifacts
   - The screenshots now show the expected side effect of the projection working more honestly: revealed edge areas can appear as black bars or stretched edge pixels.
   - This is not a sign that the shader is inactive. It is the natural failure mode of image reprojection without inpainting or overscan.

3. Some room-layer composition assumptions are still crude
   - Disabling heuristic shifts for `ObjectState` layers reduced obvious seams.
   - But it does not solve the deeper problem that Grim scenes are composites of multiple 2D elements plus 3D actors.
   - Some setups may still show mismatch between the warped main backdrop and unwarped 2D auxiliary elements.

4. Current actor bbox telemetry is not reliable enough for precise diagnosis
   - Recent CSVs show `tracked_actor_*` values that are extremely large and partly off-screen.
   - Treat that logger field as suspect for now.
   - Do not use those bbox numbers as authoritative until they are validated.

5. Some CSV fields are still conceptual rather than actual
   - The `layer_*` and some fallback offset columns are still logged from helper math even when those offsets are not applied because the shader path is active.
   - The log is useful, but not every field reflects the exact active draw path anymore.

## Likely root cause of the visible artifacts

The current artifacts are not caused by "defragmentation" or by the UDP/head-tracking path.

They come mainly from the rendering approach:

- off-axis camera for live 3D
- plus shader-based screen-space reprojection of a pre-rendered 2D room image
- using only a z-buffer and no hidden color data behind occluders

This is fundamentally an image-warping technique, not a full scene re-render of the background. It can look convincing at small motion, but it will always expose cracks at strong depth transitions unless the content is expanded, layered more intelligently, or regenerated.

## Current math path

Shared math:

- `graphics/parallax.h`

Current Grim render path:

- `Set::Setup::setupCamera()` applies both camera translation and off-axis frustum terms.
- `Set::drawBackground()` uses the depth-aware background path when shaders are available.
- `grim_background.fragment` reconstructs depth from the z-bitmap and computes per-pixel UV shifts from camera-plane motion.

Important implementation note:

- The Grim background shader had a depth reconstruction bug earlier because the packed 16-bit depth was effectively scaled to `V / 65280` instead of `V / 65535`.
- That correction is already fixed.
- The current issue is no longer "the shader is off" or "depth decode is obviously wrong." The current issue is image-reprojection quality and content limitations.

## Current insertion points

Useful files:

- `engines/grim/grim.cpp`
  - config defaults
  - renderer selection
  - runtime renderer recreation
  - hotkeys
  - `opentrack` input
  - CSV logging
  - overlay drawing

- `engines/grim/set.cpp`
  - `Set::Setup::setupCamera()`
  - `Set::drawBackground()`
  - `Set::drawBitmaps()`

- `engines/grim/gfx_opengl.cpp`
  - fixed-function frustum support

- `engines/grim/gfx_opengl_shaders.cpp`
  - shader frustum setup
  - depth-aware background draw path

- `engines/grim/shaders/grim_background.fragment`
  - current reprojection logic

- `graphics/parallax.h`
  - shared math helpers intended to be reusable for later SCUMM work

## Current testing guidance

Prefer scenes with:

- clear near/far separation
- hard occluding edges
- furniture or door frames crossing strong depth discontinuities
- enough head motion to expose reprojection failures without forcing the input into extremes

Recent useful Grim scenes:

- `ha.set / ha_intls`
- `mo.set / mo_ddtws`

When reviewing the effect, separate these questions:

- Does the scene move in the right direction relative to the head?
- Does near depth move more than far depth?
- Are artifacts due to wrong motion, or due to missing hidden pixels?

Right now the first two are much improved. The main remaining failures are in the third category.

## Recommended next step for the next agent

Do not spend the next round redoing tracker input or basic frustum math.

The next useful work is artifact reduction in the backdrop reprojection path.

Recommended order:

1. Improve the documentation/debug truthfulness
   - Make CSV fields clearly distinguish actual applied offsets from theoretical helper values.
   - Validate or replace the actor screen-bbox telemetry.

2. Reduce reprojection artifacts near depth discontinuities
   - Inspect local depth gradients in the background shader.
   - Suppress or soften reprojection where neighboring depth changes sharply.
   - This will trade some depth strength for fewer cracks and jagged seams.

3. Handle border reveal more gracefully
   - Explore mild overscan, edge dilation, or conservative clamp behavior.
   - Do not try to solve missing image content perfectly in-engine on the next pass.

4. Only after that, revisit room-layer treatment
   - Some scenes may still need a better policy for auxiliary `ObjectState` layers.
   - But the immediate problem is the main backdrop warp quality, not the tracker.

## Non-goals for the next agent

- Do not port this to Monkey Island yet.
- Do not add embedded computer-vision tracking.
- Do not go back to moving the OS window as the control model.
- Do not assume the actor bbox telemetry is trustworthy until verified.
- Do not interpret the current border gaps as proof the effect is mathematically wrong.

## Deliverable expected from the next agent

The next valuable deliverable is:

- a cleaner depth-aware background shader with fewer cracks and edge artifacts
- better telemetry that reflects the actual active render path
- a short validation note on whether small-motion desktop parallax is now visually acceptable in Grim

If that becomes stable enough, then the project can decide which pieces are actually worth carrying into SCUMM for Monkey Island.
