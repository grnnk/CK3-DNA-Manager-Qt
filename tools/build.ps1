#Requires -Version 5.1
<#
.SYNOPSIS
    Configure, build and test CK3 DNA Manager (Qt) with CMake, using Ninja or
    GNU Make (Makefiles generator).

.EXAMPLE
    .\tools\build.ps1 -Config Release

.EXAMPLE
    .\tools\build.ps1 -Config Release -Generator "MinGW Makefiles"

.EXAMPLE
    .\tools\build.ps1 -Config Debug -Screenshots

.EXAMPLE
    .\tools\build.ps1 -QtPath C:\Qt\6.8.3\mingw_64 -MingwPath C:\Qt\Tools\mingw1310_64
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Config = 'Release',

    [ValidateSet('Ninja', 'MinGW Makefiles', 'Unix Makefiles')]
    [string]$Generator = 'Ninja',

    [string]$QtPath,
    [string]$MingwPath,
    [string]$BuildDir,

    [switch]$Screenshots,
    [switch]$SkipTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Get-ProjectPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot $Path))
}

function Find-Executable {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [string[]]$Candidates = @()
    )

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return $candidate
        }
    }
    return $null
}

# --- Locate Qt -------------------------------------------------------------

if ($QtPath) {
    $QtPath = Get-ProjectPath $QtPath
} elseif ($env:QT_ROOT) {
    $QtPath = Get-ProjectPath $env:QT_ROOT
} else {
    $candidates = @()
    if (Test-Path -LiteralPath 'C:\Qt') {
        $candidates = @(Get-ChildItem -LiteralPath 'C:\Qt' -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName 'mingw_64' } |
            Where-Object { Test-Path -LiteralPath (Join-Path $_ 'bin\qmake.exe') })
    }
    if ($candidates.Count -gt 0) {
        $QtPath = $candidates[0]
    }
}

if (-not $QtPath -or -not (Test-Path -LiteralPath (Join-Path $QtPath 'bin\qmake.exe'))) {
    throw "Qt was not found. Pass -QtPath (for example C:\Qt\6.8.3\mingw_64) or set QT_ROOT."
}

$QtPath = [System.IO.Path]::GetFullPath($QtPath)

# --- Locate MinGW ----------------------------------------------------------

if (-not $MingwPath -and $env:MINGW_ROOT) {
    $MingwPath = Get-ProjectPath $env:MINGW_ROOT
}

if (-not $MingwPath) {
    $qtBase = Split-Path (Split-Path $QtPath -Parent) -Parent
    $toolsRoot = Join-Path $qtBase 'Tools'
    if (Test-Path -LiteralPath $toolsRoot) {
        $candidates = @(Get-ChildItem -LiteralPath $toolsRoot -Directory -Filter 'mingw*' -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object { $_.FullName } |
            Where-Object { Test-Path -LiteralPath (Join-Path $_ 'bin\g++.exe') })
        if ($candidates.Count -gt 0) {
            $MingwPath = $candidates[0]
        }
    }
}

if (-not $MingwPath) {
    $fallback = 'C:\Qt\Tools\mingw1310_64'
    if (Test-Path -LiteralPath (Join-Path $fallback 'bin\g++.exe')) {
        $MingwPath = $fallback
    }
}

if (-not $MingwPath) {
    throw "MinGW was not found. Pass -MingwPath (for example C:\Qt\Tools\mingw1310_64) or set MINGW_ROOT."
}

$MingwPath = [System.IO.Path]::GetFullPath($MingwPath)
$Compiler = Join-Path $MingwPath 'bin\g++.exe'
$ResourceCompiler = Join-Path $MingwPath 'bin\windres.exe'

if (-not (Test-Path -LiteralPath $Compiler)) {
    throw "The MinGW C++ compiler was not found: $Compiler"
}
if (-not (Test-Path -LiteralPath $ResourceCompiler)) {
    throw "The MinGW resource compiler was not found: $ResourceCompiler"
}

# --- Locate CMake and the build tool ---------------------------------------

$CMake = Find-Executable -Name 'cmake' -Candidates @(
    'C:\Qt\Tools\CMake_64\bin\cmake.exe',
    'C:\Program Files\CMake\bin\cmake.exe',
    'E:\w64devkit\bin\cmake.exe'
)
if (-not $CMake) {
    throw "CMake was not found on PATH. Install CMake 3.21 or newer."
}

$MakeProgram = $null
if ($Generator -eq 'Ninja') {
    $MakeProgram = Find-Executable -Name 'ninja' -Candidates @(
        (Join-Path $MingwPath 'bin\ninja.exe'),
        'C:\Qt\Tools\CMake_64\bin\ninja.exe',
        'C:\ProgramData\chocolatey\bin\ninja.exe',
        'E:\w64devkit\bin\ninja.exe'
    )
    if (-not $MakeProgram) {
        throw "Ninja was not found. Put ninja.exe on PATH or next to the MinGW compiler, or use -Generator 'MinGW Makefiles'."
    }
} elseif ($Generator -eq 'MinGW Makefiles') {
    $MakeProgram = Find-Executable -Name 'mingw32-make' -Candidates @(
        (Join-Path $MingwPath 'bin\mingw32-make.exe'),
        'C:\Qt\Tools\CMake_64\bin\mingw32-make.exe'
    )
    if (-not $MakeProgram) {
        $MakeProgram = Find-Executable -Name 'make'
    }
    if (-not $MakeProgram) {
        throw "GNU Make was not found. Put mingw32-make.exe or make.exe on PATH, or use -Generator Ninja."
    }
} else {
    $MakeProgram = Find-Executable -Name 'make'
    if (-not $MakeProgram) {
        throw "GNU make was not found on PATH. Install MSYS2 or w64devkit, or use -Generator Ninja."
    }
    if (-not (Find-Executable -Name 'sh' -Candidates @('E:\w64devkit\bin\sh.exe'))) {
        throw "The 'Unix Makefiles' generator requires sh.exe on PATH. Use -Generator 'MinGW Makefiles' on Windows instead."
    }
}

if (-not $BuildDir) {
    $configFolder = $Config.ToLowerInvariant()
    switch ($Generator) {
        'Ninja' { $BuildDir = "build\$configFolder" }
        'MinGW Makefiles' { $BuildDir = "build\make-$configFolder" }
        default { $BuildDir = "build\unix-make-$configFolder" }
    }
}
$BuildDir = Get-ProjectPath $BuildDir

# --- Configure and build ---------------------------------------------------

$QtCache = $QtPath.Replace('\', '/')
$CompilerCache = $Compiler.Replace('\', '/')
$ResourceCache = $ResourceCompiler.Replace('\', '/')
$MakeProgramCache = $MakeProgram.Replace('\', '/')
$ScreenshotsValue = if ($Screenshots) { 'ON' } else { 'OFF' }

Write-Host "CK3 DNA Manager build" -ForegroundColor Cyan
Write-Host "  Configuration : $Config"
Write-Host "  Qt            : $QtPath"
Write-Host "  MinGW         : $MingwPath"
Write-Host "  Build folder  : $BuildDir"
Write-Host "  CMake         : $CMake"
Write-Host "  Generator     : $Generator"
Write-Host "  Build tool    : $MakeProgram"
Write-Host ''

$configureArgs = @(
    '-S', $ProjectRoot,
    '-B', $BuildDir,
    '-G', $Generator,
    "-DCMAKE_BUILD_TYPE=$Config",
    "-DCMAKE_PREFIX_PATH=$QtCache",
    "-DCMAKE_CXX_COMPILER=$CompilerCache",
    "-DCMAKE_RC_COMPILER=$ResourceCache",
    "-DCMAKE_MAKE_PROGRAM=$MakeProgramCache",
    '-DBUILD_TESTING=ON',
    "-DBUILD_SCREENSHOTS=$ScreenshotsValue"
)

& $CMake @configureArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed with exit code $LASTEXITCODE."
}

& $CMake --build $BuildDir --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE."
}

$ExePath = Join-Path $BuildDir 'CK3 DNA Manager Qt.exe'
if (-not (Test-Path -LiteralPath $ExePath)) {
    throw "The build finished but the executable was not found: $ExePath"
}

# --- Tests -----------------------------------------------------------------

if (-not $SkipTests) {
    $TestExe = Join-Path $BuildDir 'ck3_tests.exe'
    if (-not (Test-Path -LiteralPath $TestExe)) {
        throw "The test executable was not built: $TestExe"
    }

    $env:PATH = "$(Join-Path $QtPath 'bin');$(Join-Path $MingwPath 'bin');$env:PATH"
    $TestLog = Join-Path $BuildDir 'test.log'

    Write-Host ''
    Write-Host 'Running tests...' -ForegroundColor Cyan
    Push-Location $BuildDir
    try {
        & $TestExe '-o' "$TestLog,txt"
        $testExit = $LASTEXITCODE
    } finally {
        Pop-Location
    }

    if (Test-Path -LiteralPath $TestLog) {
        $totals = Select-String -LiteralPath $TestLog -Pattern '^Totals:' |
            Select-Object -Last 1
        if ($totals) {
            Write-Host $totals.Line
        }
    }

    if ($testExit -ne 0) {
        if (Test-Path -LiteralPath $TestLog) {
            Get-Content -LiteralPath $TestLog
        }
        throw "Tests failed with exit code $testExit."
    }
}

# Keep the detected locations available to deploy.ps1 in the same session.
$env:QT_ROOT = $QtPath
$env:MINGW_ROOT = $MingwPath

Write-Host ''
Write-Host "Built: $ExePath" -ForegroundColor Green
if ($Screenshots) {
    Write-Host "Screenshot helper: $(Join-Path $BuildDir 'ck3_screenshots.exe')"
}
