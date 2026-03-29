param(
    [ValidateSet("Debug", "Release", "ASan", "LLVM")]
    [string]$Configuration = "Release",

    [ValidateSet("x64")]
    [string]$Platform = "x64",

    [string]$Project = "scummvm",

    [switch]$RegenerateProjects
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)

. (Join-Path $scriptDir "Enter-ScummVMBuildEnv.ps1") -Arch $Platform

$createProjectBuildDir = Join-Path $repoRoot "build\create_project"
$createProjectExe = Join-Path $createProjectBuildDir "Release\create_project.exe"
$msvcBuildDir = Join-Path $repoRoot "build\msvc-vs2022"
$solutionFile = Join-Path $msvcBuildDir "scummvm.sln"

if (-not (Test-Path $createProjectExe)) {
    cmake -S (Join-Path $repoRoot "devtools\create_project\cmake") -B $createProjectBuildDir
    cmake --build $createProjectBuildDir --config Release
}

if ($RegenerateProjects -or -not (Test-Path $solutionFile)) {
    New-Item -ItemType Directory -Force -Path $msvcBuildDir | Out-Null
    Push-Location $msvcBuildDir
    try {
        & $createProjectExe ..\.. --msvc --msvc-version 17 --vcpkg
    } finally {
        Pop-Location
    }
}

if ($Project -eq "solution") {
    $buildTarget = $solutionFile
} else {
    $buildTarget = Join-Path $msvcBuildDir "$Project.vcxproj"
    if (-not (Test-Path $buildTarget)) {
        throw "Project file not found: $buildTarget"
    }
}

msbuild $buildTarget /m /p:Configuration=$Configuration /p:Platform=$Platform

if ($Project -eq "scummvm" -or $Project -eq "solution") {
    $exePath = Join-Path $msvcBuildDir "Release$Platform\scummvm.exe"
    if (Test-Path $exePath) {
        Write-Host ""
        Write-Host "Built executable:"
        Write-Host "  $exePath"
    }
}
