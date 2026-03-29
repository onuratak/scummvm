# iOS Build

This note covers the high-level iPhone deployment path.

## Important constraint

Real iPhone builds in this repo require macOS.

From this Windows checkout, you can prepare engine and backend code, but you cannot complete the documented iPhone packaging and signing flow without:

- a Mac
- Xcode
- Apple signing setup

## Relevant files

- `backends/platform/ios7/README.md`
- `doc/docportal/other_platforms/ios_build.rst`
- `ports.mk`
- `dists/ios7/`

## Documented iOS flow

On a Mac:

1. Build `devtools/create_project/xcode/create_project`.
2. Generate an Xcode project with `--xcode --ios`.
3. Open the generated `scummvm.xcodeproj`.
4. Set bundle identifier and signing.
5. Build and run from Xcode on device.

The docs also refer to separate iOS support libraries/frameworks and iOS bundle assets.

## Build clues in the repo

The current iOS backend in use is `ios7`.

Relevant build identifiers:

- `--backend=ios7`
- `--host=ios7`
- `--host=ios7-arm64`

Packaging rules in `ports.mk` include:

- `ios7bundle`
- `scummvm-static-ios`

## Backend files likely relevant to platform work

- `backends/platform/ios7/ios7_common.h`
- `backends/platform/ios7/ios7_osys_main.h`
- `backends/platform/ios7/ios7_osys_events.cpp`
- `backends/platform/ios7/ios7_video.h`
- `backends/platform/ios7/ios7_video.mm`

## Practical workflow

- Develop engine-side behavior and Windows test paths first.
- Add iOS-specific motion or platform input only after the cross-platform rendering contract is stable.
- Use a Mac/Xcode cycle only when the feature is ready to validate on real hardware.
