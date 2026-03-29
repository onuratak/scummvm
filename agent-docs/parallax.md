# Parallax Feature Notes

This note is the high-level design starting point for the Monkey Island parallax / pseudo-3D idea.

Current Grim Fandango Windows prototype handoff:

- `agent-docs/grim-parallax-handoff.md`

## Scope

Target games:

- Monkey Island 1
- Monkey Island 2
- Curse of Monkey Island

Implementation scope:

- keep the feature inside the SCUMM engine family
- avoid a global ScummVM-wide abstraction until a prototype proves the design

## Likely insertion points

Backend side:

- `backends/platform/ios7/`
- likely starting files:
  - `ios7_common.h`
  - `ios7_osys_main.h`
  - `ios7_osys_events.cpp`
  - `ios7_video.h`
  - `ios7_video.mm`

Rendering side:

- `engines/scumm/gfx.cpp`
- `engines/scumm/room.cpp`
- `engines/scumm/camera.cpp`
- `engines/scumm/actor.cpp`
- `backends/graphics/opengl/` if the effect becomes an OpenGL-layered or post-process solution

## Current observation

No obvious `CoreMotion` / gyroscope pipeline was identified in the iOS backend during the initial pass.

That means device-motion input likely needs to be added.

## Suggested first milestone

1. Add a hidden debug option for a SCUMM parallax prototype.
2. Create a backend-independent parallax state input contract.
3. Add a Windows fake-input path so the renderer can be developed without an iPhone.
4. Restrict the feature to `monkey`, `monkey2`, and `comi` while the design is unstable.

## Recommended shape

Prefer this order:

1. define how the renderer consumes normalized parallax input
2. prototype the render behavior on Windows using fake input
3. add real iOS motion input later

## Things to avoid

- Do not fork the SCUMM engine into separate Monkey Island engines.
- Do not make the first version UIKit-only.
- Do not assume the original assets contain depth maps.

The likely effect will need heuristic layer separation, selected render-layer offsets, or room-specific metadata.
