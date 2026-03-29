param(
    [string]$Arch = "x64"
)

$ErrorActionPreference = "Stop"

function Get-VsWherePath {
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"),
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio\Installer\vswhere.exe")
    )

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) {
            return $candidate
        }
    }

    throw "Could not find vswhere.exe."
}

function Import-BatchEnvironment {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BatchFile,

        [string[]]$Arguments = @()
    )

    $argumentTail = if ($Arguments.Count -gt 0) {
        " " + ($Arguments -join " ")
    } else {
        ""
    }

    $cmd = """$BatchFile""$argumentTail && set"
    $lines = & cmd.exe /d /s /c $cmd

    if ($LASTEXITCODE -ne 0) {
        throw "Failed to import environment from $BatchFile"
    }

    foreach ($line in $lines) {
        $separator = $line.IndexOf("=")
        if ($separator -gt 0) {
            $name = $line.Substring(0, $separator)
            $value = $line.Substring($separator + 1)
            Set-Item -Path "Env:$name" -Value $value
        }
    }
}

$vswhere = Get-VsWherePath
$installationPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath

if (-not $installationPath) {
    throw "No Visual Studio installation with C++ tools was found."
}

$vsDevCmd = Join-Path $installationPath "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $vsDevCmd)) {
    throw "Could not find VsDevCmd.bat at $vsDevCmd"
}

Import-BatchEnvironment -BatchFile $vsDevCmd -Arguments @("-arch=$Arch", "-host_arch=$Arch")

$defaultVcpkgRoot = "C:\DEV\vcpkg"
if (-not $env:VCPKG_ROOT -and (Test-Path $defaultVcpkgRoot)) {
    $env:VCPKG_ROOT = $defaultVcpkgRoot
}

$cmakeBin = "C:\Program Files\CMake\bin"
if ((Test-Path $cmakeBin) -and ($env:PATH -notlike "*$cmakeBin*")) {
    $env:PATH = "$cmakeBin;$env:PATH"
}

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$env:SCUMMVM_ROOT = $repoRoot

Write-Host "Imported Visual Studio build environment from:"
Write-Host "  $installationPath"
Write-Host "Architecture: $Arch"
Write-Host "VCPKG_ROOT: $env:VCPKG_ROOT"
Write-Host "SCUMMVM_ROOT: $env:SCUMMVM_ROOT"
Write-Host ""
Write-Host "Example commands:"
Write-Host "  cmake -S `"$repoRoot\devtools\create_project\cmake`" -B `"$repoRoot\build\create_project`""
Write-Host "  cmake --build `"$repoRoot\build\create_project`" --config Release"
Write-Host "  & `"$repoRoot\build\create_project\Release\create_project.exe`" `"$repoRoot`" --msvc --msvc-version 17 --vcpkg"
