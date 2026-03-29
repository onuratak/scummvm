# Windows Build

This is the canonical Windows build note for agents working from VS Code terminals in this checkout.

## Installed environment

Validated in this checkout:

- Visual Studio 2022 Build Tools with MSVC and MSBuild
- CMake
- `vcpkg` at `C:\DEV\vcpkg`

Repo-local generated artifacts:

- solution: `build/msvc-vs2022/scummvm.sln`
- app project: `build/msvc-vs2022/scummvm.vcxproj`
- SCUMM project: `build/msvc-vs2022/scumm.vcxproj`
- generator binary: `build/create_project/Release/create_project.exe`
- built app: `build/msvc-vs2022/Releasex64/scummvm.exe`

## Primary scripts

Use these scripts instead of guessing toolchain setup:

- `devtools/windows/Enter-ScummVMBuildEnv.ps1`
- `devtools/windows/Build-ScummVM.ps1`

Rule:

- Use the repo-local generated solution under `build/msvc-vs2022`.
- Prefer `Build-ScummVM.ps1` unless there is a specific reason not to.

## Default build commands

Incremental app build:

```powershell
. .\devtools\windows\Build-ScummVM.ps1
```

Build only the SCUMM engine library:

```powershell
. .\devtools\windows\Build-ScummVM.ps1 -Project scumm
```

Build the app after engine changes:

```powershell
. .\devtools\windows\Build-ScummVM.ps1 -Project scummvm
```

Regenerate projects and then build:

```powershell
. .\devtools\windows\Build-ScummVM.ps1 -RegenerateProjects -Project scummvm
```

Build the full solution:

```powershell
. .\devtools\windows\Build-ScummVM.ps1 -Project solution
```

## When to regenerate projects

Regenerate when:

- a source file is added or removed
- a `module.mk` file changes
- build flags or enabled features change
- a new dependency or engine configuration is introduced

If only existing `.cpp` or `.h` files changed, a normal incremental build is enough.

## Manual fallback sequence

If a future agent needs the explicit commands:

```powershell
. .\devtools\windows\Enter-ScummVMBuildEnv.ps1
cmake -S .\devtools\create_project\cmake -B .\build\create_project
cmake --build .\build\create_project --config Release
New-Item -ItemType Directory -Force .\build\msvc-vs2022 | Out-Null
Push-Location .\build\msvc-vs2022
..\create_project\Release\create_project.exe ..\.. --msvc --msvc-version 17 --vcpkg
Pop-Location
msbuild .\build\msvc-vs2022\scummvm.vcxproj /m /p:Configuration=Release /p:Platform=x64
```

## Notes

- The generated projects use vcpkg manifest integration against this repo and `C:\DEV\vcpkg`.
- Full builds are large because many engines are enabled and each project emits `.obj` plus `.lib` outputs.
- For normal iteration, prefer building `scummvm.vcxproj` rather than the full solution.
