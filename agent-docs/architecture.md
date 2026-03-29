# Architecture

This note is intentionally high-level. It is the starting point for agents who need to understand where code should live before doing a deeper audit.

## Repo shape

Top-level directories that matter first:

- `base/`: startup, command-line handling, plugin and engine bootstrapping.
- `common/`: shared core abstractions, including `OSystem`.
- `backends/`: platform integrations and graphics/audio/input backends.
- `engines/`: one directory per game-engine family.
- `graphics/`: shared rendering helpers, surfaces, palettes, scalers, OpenGL helpers.
- `gui/`: launcher, dialogs, options UI.
- `audio/`, `video/`, `image/`: shared media subsystems.
- `dists/`: packaging and target-specific distribution assets.
- `devtools/`: project generators and helper tooling.

## Runtime flow

High-level boot flow:

1. Platform backend creates `g_system` as an `OSystem` implementation.
2. `base/main.cpp` runs `scummvm_main(...)`.
3. Config, plugins, graphics, event manager, keymapper, and launcher are initialized.
4. A selected game is identified through its engine metaengine.
5. The chosen engine instance is created and `engine->run()` takes over.

Important files:

- `common/system.h`: `OSystem`, the main backend interface.
- `base/main.cpp`: engine selection, launch flow, and top-level runtime setup.
- `engines/metaengine.*`: shared plugin model.

## SCUMM and Monkey Island

Monkey Island 1, 2, and 3 all go through `engines/scumm`.

Game IDs:

- MI1: `monkey`
- MI2: `monkey2`
- Curse of Monkey Island: `comi`

Relevant files:

- `engines/scumm/detection_tables.h`
- `engines/scumm/metaengine.cpp`
- `engines/scumm/scumm.cpp`
- `engines/scumm/gfx.cpp`
- `engines/scumm/room.cpp`
- `engines/scumm/camera.cpp`
- `engines/scumm/actor.cpp`

Important constraint:

- Do not fork Monkey Island into separate engines.
- A Monkey Island feature should usually be implemented as a SCUMM feature with game-specific gating where needed.
