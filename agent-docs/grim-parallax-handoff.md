# Grim Windows Parallax Handoff

This note is the current handoff for the Grim Fandango parallax prototype on Windows.

Use this before doing more work in Grim. The long-term target is still Monkey Island 1-3 in SCUMM, but the current prototype remains Grim-only because Grim already has a true 3D camera, mixed 2D/3D scene composition, and scene z-bitmaps that make desktop parallax experiments practical.

## Latest session status

The latest session added a second render path for comparison:

- `warp`: the existing shader-based depth reprojection path
- `layered`: a new manifest + PNG plate compositor intended to test broad depth-band layering

Current end-of-session status:

- Layered mode can now be selected at runtime with `F12`.
- Runtime telemetry was extended so `F10` CSV captures and the `F11` overlay report whether layered assets were actually loaded or whether the room fell back to the static bitmap.
- The generated plate assets live in `C:\Users\onur_\Downloads\Grim Fandango\parallax_layers`.
- The active Grim target path in ScummVM is `C:\Users\onur_\Downloads\Grim Fandango\GRIMDATA\`.
- Because the engine target path points at `GRIMDATA`, the live test only started finding layered assets after creating a filesystem junction:
  - `C:\Users\onur_\Downloads\Grim Fandango\GRIMDATA\parallax_layers`
  - pointing to
  - `C:\Users\onur_\Downloads\Grim Fandango\parallax_layers`

Important current blocker:

- Layered mode now loads, but the room backdrops render almost black while characters and some objects remain visible.
- That means the current blocker is no longer path discovery.
- The next debugging target is plate/image decode or color/alpha handling for the loaded PNG-backed `Bitmap` objects.

## Current goal

Validate a convincing fixed-monitor look-around effect on Windows, using external head tracking, before carrying the ideas into Monkey Island.

The current question is no longer just "can things move?" There are now two experimental backdrop strategies:

- shader depth warp
- layered broad-band backdrop plates

The immediate question is which path has the better ceiling for a usable desktop parallax effect, and what the real failure mode is for each path.

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
- Layered backdrop compositor that loads `manifest.json` plus PNG plates from disk.
- CSV logging pipeline for per-frame telemetry.
- Independent debug overlay toggle.

What changed recently and matters:

- The biggest activation bug is fixed: when parallax is toggled with `F6`, the renderer is recreated so Grim can switch into the shader renderer path at runtime.
- The shader backdrop path is now confirmed active in live captures.
- Auxiliary `ObjectState` bitmap layers are no longer shifted heuristically while the shader-backed depth-aware background path is active. That reduced obvious seam drift between warped backdrop pixels and separately shifted room layers.
- The layered path now has explicit runtime status reporting in both the overlay and CSV.
- The first major layered-mode discovery bug was naming/path related:
  - Grim setup names in live rooms look like `mo_ddtws`
  - generated asset folders were named like `mo_0_ddtws`
  - the live target path points to `...\GRIMDATA\`
  - the generated assets were emitted to a sibling `...\parallax_layers\`
- The current working filesystem setup that made layered mode discoverable is the `GRIMDATA\parallax_layers` junction described above.
- After discovery was fixed, the loaded layered plates rendered almost black. That is the current blocker.

## Current debug controls

- `F6`: toggle Grim parallax test on/off
- `F7`: toggle mouse / auto input
- `F8`: recenter
- `F9`: toggle mouse / `opentrack`
- `F10`: toggle CSV logging
- `F11`: toggle on-screen debug overlay
- `F12`: toggle render mode between `warp` and `layered`
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

Most recent shader capture summary:

- `204` frames
- `input_source = opentrack`
- `set_name = mo.set`
- `setup_name = mo_ddtws`
- `depth_aware_background_active = 1` for every frame
- `strength = 0.05`
- `bg_shift_near_x` reaches about `+/-10.84` px
- `bg_shift_far_x` reaches about `+/-5.42` px

That confirms the shader path is now live and depth-dependent motion is occurring.

Most recent layered capture summary:

- `render_mode = layered`
- `setup_name = mo_ddtws`
- initial failure mode was:
  - `layered_requested = 1`
  - `layered_loaded = 0`
  - `layered_fallback_static = 1`
  - `layered_status = not_found`
- after the `GRIMDATA\parallax_layers` junction was added, layered mode stopped being a pure discovery failure and started drawing loaded content
- current visible result is almost-black room plates with characters still visible on top

That means the next session should not spend time rediscovering the asset location problem unless the junction disappears.

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

6. Layered backdrop mode currently renders almost-black room plates
   - This is the newest blocker.
   - The room is being discovered and loaded, but the loaded plate images render nearly black.
   - Characters and some scene objects still appear, which suggests the problem is in the plate image path rather than the full scene render path.
   - Most likely areas:
     - PNG decode into `Graphics::Surface`
     - `Bitmap` construction for PNG-backed images
     - pixel format / channel ordering
     - alpha handling causing the room to multiply/blend against black
     - draw path differences between native Grim bitmaps and PNG-backed bitmaps

## Likely root cause of the visible artifacts

The current artifacts are not caused by "defragmentation" or by the UDP/head-tracking path.

They come mainly from the rendering approach:

- off-axis camera for live 3D
- plus shader-based screen-space reprojection of a pre-rendered 2D room image
- using only a z-buffer and no hidden color data behind occluders

This is fundamentally an image-warping technique, not a full scene re-render of the background. It can look convincing at small motion, but it will always expose cracks at strong depth transitions unless the content is expanded, layered more intelligently, or regenerated.

The layered path has a different current failure mode:

- the assets are no longer merely missing
- the loaded room plates appear too dark / almost black
- that points to image ingest / compositing correctness, not to the conceptual viability of layered parallax itself

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
  - layered manifest/PNG loading
  - layered manifest candidate search
  - `Set::drawBitmaps()`

- `engines/grim/gfx_opengl.cpp`
  - fixed-function frustum support

- `engines/grim/gfx_opengl_shaders.cpp`
  - shader frustum setup
  - depth-aware background draw path

- `engines/grim/shaders/grim_background.fragment`
  - current reprojection logic

- `engines/grim/bitmap.cpp`
  - PNG-backed `Bitmap` transparency / surface ingestion path

- `devtools/grim_parallax_layer_suggest.py`
  - emits broad-band runtime layered assets
  - current output naming uses bitmap stems such as `mo_0_ddtws`

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

The next useful work is now split by render mode:

- for `warp`: artifact reduction in the shader reprojection path
- for `layered`: fix the almost-black plate rendering first, before judging the visual quality of the layered approach

Recommended order:

1. Fix layered plate rendering correctness
   - Verify the loaded `manifest.json`, `base_plate.png`, and `overlay_*.png` images visually in code, not just by existence.
   - Inspect the PNG-backed `Bitmap` path and compare it against native Grim backdrop bitmap creation.
   - Determine whether the black result is caused by wrong RGB ordering, premultiplied alpha assumptions, or draw/blend state.

2. Keep the runtime asset discovery story simple
   - The current live test depends on:
     - `C:\Users\onur_\Downloads\Grim Fandango\parallax_layers`
     - plus the `GRIMDATA\parallax_layers` junction
   - Do not rip that out until the layered renderer is visually correct.

3. Once layered plates render correctly, compare `warp` vs `layered`
   - Then decide whether layered broad-band plates are worth further investment.
   - Only after the visuals are trustworthy should quality comparisons be made.

4. If layered remains promising, then continue with shader artifact work or layered tuning as needed
   - For `warp`, reduce cracks and border artifacts.
   - For `layered`, tune plate factors and layer definitions.

## Non-goals for the next agent

- Do not port this to Monkey Island yet.
- Do not add embedded computer-vision tracking.
- Do not go back to moving the OS window as the control model.
- Do not assume the actor bbox telemetry is trustworthy until verified.
- Do not interpret the current border gaps as proof the effect is mathematically wrong.
- Do not waste time re-debugging the original `not_found` symptom if the `GRIMDATA\parallax_layers` junction is still present.

## Deliverable expected from the next agent

The next valuable deliverable is:

- a correct-looking layered backdrop draw path
- or a clear technical reason why the current PNG/manifest plate approach is not worth continuing
- plus updated notes comparing `warp` and `layered` once both are visually truthful

If that becomes stable enough, then the project can decide which pieces are actually worth carrying into SCUMM for Monkey Island.
