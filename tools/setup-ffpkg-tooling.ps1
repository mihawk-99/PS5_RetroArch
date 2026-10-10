<#
  ps5-native-app-boilerplate - Optional UFS2Tool bootstrapper.
  Copyright (C) 2026 BlackBearReloaded
  SPDX-License-Identifier: GPL-3.0-or-later

  Delegates to the shared WSL bootstrapper, which fetches and builds a pinned
  UFS2Tool checkout inside the ignored dependency cache.
#>

#requires -Version 5.1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$setup = Join-Path $root "tools/setup-packaging-dependencies.sh"

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

if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    Fail "WSL was not found."
}
if (-not (Test-Path -LiteralPath $setup -PathType Leaf)) {
    Fail "Shared packaging bootstrapper was not found: $setup"
}

$binary = & wsl.exe --exec bash (WslPath $setup) ffpkg
if ($LASTEXITCODE -ne 0) {
    Fail "Unable to fetch and build UFS2Tool."
}
if ($binary -is [array]) { $binary = $binary[-1] }
if (-not $binary) {
    Fail "UFS2Tool bootstrapper did not return its runner path."
}

Write-Output $binary.Trim()
