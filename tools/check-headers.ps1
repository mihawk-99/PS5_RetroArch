<#
  ps5-native-app-boilerplate - Source attribution header audit.
  Copyright (C) 2026 BlackBearReloaded
  SPDX-License-Identifier: GPL-3.0-or-later

  Verifies attribution in every tracked, comment-capable code and tooling file.
#>

#requires -Version 5.1

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$patterns = @("*.c", "*.cc", "*.cpp", "*.h", "*.hpp", "*.ld", "*.py", "*.ps1", "*.yml", "*.yaml", "*.sh")
$files = @(& git -C $root ls-files --cached --others --exclude-standard -- @patterns |
    Where-Object { Test-Path -LiteralPath (Join-Path $root $_) -PathType Leaf })
$files += @(
    ".editorconfig",
    ".clang-format",
    ".clang-tidy",
    ".gitattributes",
    ".gitignore",
    ".env.example",
    "Makefile",
    "tooling/prospero-clang18",
    "tooling/native/runtime/api-surface.txt",
    "tooling/native/runtime/imports.txt"
)
$files = @($files | Sort-Object -Unique)
$markers = @(
    "ps5-native-app-boilerplate",
    "Copyright (C) 2026 BlackBearReloaded",
    "SPDX-License-Identifier: GPL-3.0-or-later"
)
$missing = @()

foreach ($file in $files) {
    $path = Join-Path $root $file
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $missing += "$file (missing file)"
        continue
    }

    $header = (Get-Content -LiteralPath $path -TotalCount 20) -join "`n"
    foreach ($marker in $markers) {
        if (-not $header.Contains($marker)) {
            $missing += "$file (missing '$marker')"
        }
    }
}

if ($missing.Count -ne 0) {
    $missing | ForEach-Object { Write-Error $_ }
    throw "One or more source attribution headers are incomplete."
}

Write-Host "Verified attribution headers in $($files.Count) tracked code and tooling files."
