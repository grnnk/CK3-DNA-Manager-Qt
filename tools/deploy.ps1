#Requires -Version 5.1
<#
.SYNOPSIS
    Build and deploy CK3 DNA Manager (Qt) into a self-contained Windows folder.

.EXAMPLE
    .\tools\deploy.ps1 -Config Release

.EXAMPLE
    .\tools\deploy.ps1 -Config Release -Generator "MinGW Makefiles"

.EXAMPLE
    .\tools\deploy.ps1 -Config Release -Zip -Version 1.0.0

.EXAMPLE
    .\tools\deploy.ps1 -Config Release -SkipBuild
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
    [string]$DistDir = 'dist',
    [string]$Version = '1.0.0',

    [switch]$SkipBuild,
    [switch]$SkipTests,
    [switch]$Zip
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

if (-not $BuildDir) {
    $configFolder = $Config.ToLowerInvariant()
    switch ($Generator) {
        'Ninja' { $BuildDir = "build\$configFolder" }
        'MinGW Makefiles' { $BuildDir = "build\make-$configFolder" }
        default { $BuildDir = "build\unix-make-$configFolder" }
    }
}
$BuildDir = Get-ProjectPath $BuildDir
$DistDir = Get-ProjectPath $DistDir

if ($DistDir.TrimEnd('\', '/') -eq $ProjectRoot.TrimEnd('\', '/')) {
    throw "Refusing to use the project root as the deployment folder."
}
if ($DistDir.TrimEnd('\', '/') -eq $BuildDir.TrimEnd('\', '/') -or
    $DistDir.StartsWith($BuildDir.TrimEnd('\', '/') + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to deploy inside the build folder."
}
$root = [System.IO.Path]::GetPathRoot($DistDir)
if ($root -and $DistDir.TrimEnd('\', '/') -eq $root.TrimEnd('\', '/')) {
    throw "Refusing to use a drive root as the deployment folder."
}

# --- Build -----------------------------------------------------------------

if (-not $SkipBuild) {
    $buildArgs = @{
        Config    = $Config
        BuildDir  = $BuildDir
        Generator = $Generator
    }
    if ($QtPath) { $buildArgs['QtPath'] = $QtPath }
    if ($MingwPath) { $buildArgs['MingwPath'] = $MingwPath }
    if ($SkipTests) { $buildArgs['SkipTests'] = $true }

    & (Join-Path $PSScriptRoot 'build.ps1') @buildArgs
}

if (-not $QtPath) {
    if ($env:QT_ROOT) {
        $QtPath = $env:QT_ROOT
    }
}
if (-not $QtPath) {
    throw "Qt was not found. Pass -QtPath or set QT_ROOT."
}
$QtPath = [System.IO.Path]::GetFullPath($QtPath)

$ExePath = Join-Path $BuildDir 'CK3 DNA Manager Qt.exe'
if (-not (Test-Path -LiteralPath $ExePath)) {
    throw "The application was not built: $ExePath"
}

$WinDeployQt = Join-Path $QtPath 'bin\windeployqt.exe'
if (-not (Test-Path -LiteralPath $WinDeployQt)) {
    throw "windeployqt was not found: $WinDeployQt"
}

# --- Stage -----------------------------------------------------------------

if (Test-Path -LiteralPath $DistDir) {
    Write-Host "Cleaning $DistDir"
    Remove-Item -LiteralPath $DistDir -Recurse -Force
}
New-Item -ItemType Directory -Path $DistDir | Out-Null

Copy-Item -LiteralPath $ExePath -Destination $DistDir
foreach ($document in @('README.md', 'LICENSE')) {
    $source = Join-Path $ProjectRoot $document
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination $DistDir
    }
}

$deployMode = if ($Config -eq 'Debug') { '--debug' } else { '--release' }

Write-Host "Deploying Qt runtime with windeployqt..."
& $WinDeployQt `
    $deployMode `
    --compiler-runtime `
    --no-translations `
    --no-system-d3d-compiler `
    --no-system-dxc-compiler `
    --no-opengl-sw `
    --skip-plugin-types generic,networkinformation,tls `
    --exclude-plugins qsqlmimer,qsqlodbc,qsqlpsql `
    --dir $DistDir `
    $ExePath

if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE."
}

$required = @(
    'CK3 DNA Manager Qt.exe',
    'Qt6Core.dll',
    'Qt6Gui.dll',
    'Qt6Widgets.dll',
    'Qt6Sql.dll',
    'platforms\qwindows.dll',
    'sqldrivers\qsqlite.dll'
)
foreach ($relative in $required) {
    $full = Join-Path $DistDir $relative
    if (-not (Test-Path -LiteralPath $full)) {
        throw "Deployment is incomplete; missing: $relative"
    }
}

# SVG icons are essential for the UI. windeployqt normally finds the Svg module
# through the icon/image plugins, but keep the check explicit.
foreach ($relative in @('Qt6Svg.dll', 'imageformats\qsvg.dll')) {
    $full = Join-Path $DistDir $relative
    if (-not (Test-Path -LiteralPath $full)) {
        throw "Deployment is missing SVG support: $relative"
    }
}

# --- Optional archive ------------------------------------------------------

if ($Zip) {
    $zipPath = Join-Path $ProjectRoot "CK3-DNA-Manager-$Version-win64.zip"
    if (Test-Path -LiteralPath $zipPath) {
        Remove-Item -LiteralPath $zipPath -Force
    }
    Write-Host "Creating $zipPath"
    Compress-Archive -Path (Join-Path $DistDir '*') -DestinationPath $zipPath -CompressionLevel Optimal
}

$size = (Get-ChildItem -LiteralPath $DistDir -Recurse -File |
    Measure-Object -Property Length -Sum).Sum
$sizeMb = if ($size) { $size / 1MB } else { 0 }

Write-Host ''
Write-Host ("Deployed to: {0}" -f $DistDir) -ForegroundColor Green
Write-Host ("Total size : {0:N1} MB" -f $sizeMb)
Write-Host ("Run        : {0}" -f (Join-Path $DistDir 'CK3 DNA Manager Qt.exe'))
if ($Zip) {
    Write-Host ("Archive    : {0}" -f (Join-Path $ProjectRoot "CK3-DNA-Manager-$Version-win64.zip"))
}
