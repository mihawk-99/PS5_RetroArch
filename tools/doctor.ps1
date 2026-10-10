<#
  ps5-native-app-boilerplate - Windows prerequisite-check frontend.
  Copyright (C) 2026 BlackBearReloaded
  SPDX-License-Identifier: GPL-3.0-or-later

  Runs the canonical read-only Linux/WSL doctor so both host entry points report
  the same required and optional tools.
#>

#requires -Version 5.1
$ErrorActionPreference = "Stop"

function Fail([string]$Message) {
    throw "ps5-native-app-boilerplate: $Message"
}

function Convert-ToWslPath([string]$Path) {
    $absolute = [IO.Path]::GetFullPath($Path)
    if ($absolute -notmatch '^([A-Za-z]):\\(.*)$') {
        Fail "The repository must be on a Windows drive visible to WSL."
    }
    return "/mnt/$($Matches[1].ToLowerInvariant())/$($Matches[2].Replace('\', '/'))"
}

if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    Fail "WSL was not found. Install WSL and a Linux distribution first."
}

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$doctor = Convert-ToWslPath (Join-Path $root "tools/doctor.sh")
& wsl.exe --exec bash $doctor
if ($LASTEXITCODE -ne 0) {
    Fail "One or more Linux/WSL prerequisites are missing."
}
