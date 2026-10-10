<#
  ps5-native-app-boilerplate - Native dependency bootstrapper.
  Copyright (C) 2026 BlackBearReloaded
  SPDX-License-Identifier: GPL-3.0-or-later

  Runs the canonical Linux/WSL dependency bootstrap and returns its paths as
  JSON for PowerShell callers.
#>

#requires -Version 5.1
param(
    [switch]$SkipSdk
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

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
    Fail "WSL was not found."
}

$script = Convert-ToWslPath (Join-Path $root "tools/setup-native-dependencies.sh")
$arguments = @("--exec", "bash", $script)
if ($SkipSdk) {
    $arguments += "--skip-sdk"
}
$lines = @(& wsl.exe @arguments)
if ($LASTEXITCODE -ne 0) {
    Fail "Native dependency bootstrap failed."
}

$values = @{}
foreach ($line in $lines) {
    if ($line -match '^([A-Z_]+)=(.*)$') {
        $values[$Matches[1]] = $Matches[2]
    }
}
foreach ($name in @("ZLIB_INCLUDE", "ZLIB_ARCHIVE")) {
    if (-not $values.ContainsKey($name)) {
        Fail "Native dependency bootstrap did not report $name."
    }
}
if (-not $SkipSdk -and -not $values.ContainsKey("SDK_ROOT")) {
    Fail "Native dependency bootstrap did not report SDK_ROOT."
}

[pscustomobject]@{
    sdkRoot = if ($SkipSdk) { "" } else { $values["SDK_ROOT"] }
    zlibInclude = $values["ZLIB_INCLUDE"]
    zlibArchive = $values["ZLIB_ARCHIVE"]
} | ConvertTo-Json -Compress
