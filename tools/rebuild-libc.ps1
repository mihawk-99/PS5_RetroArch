<#
  ps5-native-app-boilerplate - Clean-room runtime-shim reproducer.
  Copyright (C) 2026 BlackBearReloaded
  SPDX-License-Identifier: GPL-3.0-or-later

  Rebuilds twice with the native C++ tools and installs the shim only after
  deterministic hash and attribution checks pass.
#>

#requires -Version 5.1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$native = Join-Path $root "tooling/native"
$setupNativeDependencies = Join-Path $root "tools/setup-native-dependencies.ps1"
$apiManifest = Join-Path $root "tooling/native/runtime/api-surface.txt"
$importManifest = Join-Path $root "tooling/native/runtime/imports.txt"
$work = Join-Path $root "build/runtime-shim"
$rawA = Join-Path $work "libc-a.raw.elf"
$rawB = Join-Path $work "libc-b.raw.elf"
$signedA = Join-Path $work "libc-a.prx"
$signedB = Join-Path $work "libc-b.prx"
$builder = Join-Path $work "libc-builder"
$tool = Join-Path $work "ps5-native-tool"
$output = Join-Path $root "runtime/libc.prx"
$manifest = Join-Path $root "runtime/libc.prx.sha256"
$expectedRaw = "8EE6E124993E1AF26420CB455890FD002F5D6C7E78883C860CE45734E7D002BB"
$expectedSigned = "E6FF45D16ADF687855CC3B33B0C8A4132B6504360B221E0A34C7E99FB3BA0036"

function Fail([string]$Message) {
    throw "ps5-native-app-boilerplate: $Message"
}

function WslPath([string]$Path) {
    $absolute = [IO.Path]::GetFullPath($Path)
    if ($absolute -notmatch '^([A-Za-z]):\\(.*)$') {
        Fail "Path is not on a Windows drive visible to WSL: $absolute"
    }
    return "/mnt/$($Matches[1].ToLowerInvariant())/$($Matches[2].Replace('\', '/'))"
}

function Invoke-Wsl([string[]]$Arguments) {
    & wsl.exe --exec @Arguments
    if ($LASTEXITCODE -ne 0) { Fail "Native runtime build failed." }
}

if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    Fail "WSL was not found."
}
foreach ($required in @($apiManifest, $importManifest,
        $manifest,
        $setupNativeDependencies,
        (Join-Path $native "libc_builder.cpp"),
        (Join-Path $native "native_app_builder.cpp"))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        Fail "Required source file not found: $required"
    }
}

$recordedSigned = ((Get-Content -LiteralPath $manifest -Raw).Trim() -split '\s+')[0]
if ($recordedSigned -ne $expectedSigned) {
    Fail "The runtime checksum manifest does not match the release digest."
}

New-Item -ItemType Directory -Path $work -Force | Out-Null
$dependencyJson = & $setupNativeDependencies -SkipSdk
$nativeDependencies = ($dependencyJson -join "`n") | ConvertFrom-Json
$zlibInclude = [string]$nativeDependencies.zlibInclude
$zlibArchive = [string]$nativeDependencies.zlibArchive
if (-not $zlibInclude -or -not $zlibArchive) {
    Fail "Native dependency bootstrap returned incomplete zlib paths."
}

Invoke-Wsl @("clang++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
    (WslPath (Join-Path $native "libc_builder.cpp")), "-o", (WslPath $builder))
Invoke-Wsl @("clang++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
    "-I", $zlibInclude,
    (WslPath (Join-Path $native "native_app_builder.cpp")),
    (WslPath (Join-Path $native "self_container.cpp")),
    (WslPath (Join-Path $native "elf_object.cpp")),
    (WslPath (Join-Path $native "sce_module_writer.cpp")),
    $zlibArchive, "-o", (WslPath $tool))

Invoke-Wsl @((WslPath $builder), (WslPath $apiManifest),
    (WslPath $importManifest), (WslPath $rawA))
Invoke-Wsl @((WslPath $builder), (WslPath $apiManifest),
    (WslPath $importManifest), (WslPath $rawB))
$rawHashA = (Get-FileHash -LiteralPath $rawA -Algorithm SHA256).Hash
$rawHashB = (Get-FileHash -LiteralPath $rawB -Algorithm SHA256).Hash
if ($rawHashA -ne $rawHashB -or $rawHashA -ne $expectedRaw) {
    Fail "The raw module does not match the deterministic release artifact."
}

Invoke-Wsl @((WslPath $tool), "self", "--sign", "--in", (WslPath $rawA),
    "--out", (WslPath $signedA))
Invoke-Wsl @((WslPath $tool), "self", "--sign", "--in", (WslPath $rawB),
    "--out", (WslPath $signedB))
$signedHashA = (Get-FileHash -LiteralPath $signedA -Algorithm SHA256).Hash
$signedHashB = (Get-FileHash -LiteralPath $signedB -Algorithm SHA256).Hash
if ($signedHashA -ne $signedHashB -or $signedHashA -ne $expectedSigned) {
    Fail "The signed module does not match the deterministic release artifact."
}
if ((Get-Item -LiteralPath $signedA).Length -ne 1284674) {
    Fail "The signed module is not 1,284,674 bytes."
}

foreach ($artifact in @($rawA, $signedA)) {
    $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($artifact))
    if (-not $text.Contains("BlackBearReloaded")) {
        Fail "Generated output is missing the attribution marker."
    }
    foreach ($forbidden in @("W:/Build", "J013", "Prospero_Release", "sys/internal")) {
        if ($text.Contains($forbidden)) {
            Fail "Generated output contains forbidden historical text: $forbidden"
        }
    }
}

[IO.File]::Copy($signedA, $output, $true)
$installedHash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
if ($installedHash -ne $expectedSigned) {
    Fail "The installed runtime does not match the release digest."
}
Write-Host "Rebuilt deterministic clean-room runtime module with native C++ tools."
Write-Host "Raw SHA-256:    $rawHashA"
Write-Host "Signed SHA-256: $signedHashA"
Write-Host "Output:         $output"
