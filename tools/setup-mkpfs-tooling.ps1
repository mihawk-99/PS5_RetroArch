<#
  ps5-native-app-boilerplate - Optional MkPFS packaging bootstrapper.
  Copyright (C) 2026 BlackBearReloaded
  SPDX-License-Identifier: GPL-3.0-or-later

  Fetches a pinned MkPFS checkout and installs it in an ignored virtual env.
#>

#requires -Version 5.1
param(
    [string]$Python = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$dependencyRoot = Join-Path $root ".deps"
$checkout = Join-Path $dependencyRoot "MkPFS"
$repository = "https://github.com/PSBrew/MkPFS.git"
$revision = "6cb8313dfe0c988ac52617794553f343243d3a56"

function Fail([string]$Message) {
    throw "ps5-native-app-boilerplate: $Message"
}

function Invoke-Git([string[]]$Arguments) {
    & git @Arguments
    if ($LASTEXITCODE -ne 0) {
        Fail "Git command failed: git $($Arguments -join ' ')"
    }
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Fail "Git was not found. Install Git for Windows and retry."
}

if (-not $Python) {
    $pythonCommand = Get-Command python -ErrorAction SilentlyContinue
} else {
    $pythonCommand = Get-Command $Python -ErrorAction SilentlyContinue
}
if (-not $pythonCommand) {
    Fail "Python 3.9 or newer was not found. Install Python or pass -Python C:\path\to\python.exe."
}
$Python = $pythonCommand.Source
$pythonVersion = (& $Python -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')").Trim()
if ($LASTEXITCODE -ne 0 -or [version]$pythonVersion -lt [version]"3.9") {
    Fail "MkPFS requires Python 3.9 or newer; found $pythonVersion."
}

if (-not (Test-Path -LiteralPath $checkout)) {
    New-Item -ItemType Directory -Path $dependencyRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $checkout | Out-Null
    Invoke-Git @("-C", $checkout, "init", "--quiet")
    Invoke-Git @("-C", $checkout, "config", "core.autocrlf", "false")
    Invoke-Git @("-C", $checkout, "remote", "add", "origin", $repository)
    Invoke-Git @("-C", $checkout, "fetch", "--quiet", "--depth", "1", "origin", $revision)
    Invoke-Git @("-C", $checkout, "checkout", "--quiet", "--detach", "FETCH_HEAD")
} elseif (-not (Test-Path -LiteralPath (Join-Path $checkout ".git") -PathType Container)) {
    Fail "$checkout exists but is not a managed Git checkout."
}

$actualRevision = (& git -C $checkout rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualRevision -ne $revision) {
    Fail "MkPFS cache is at $actualRevision; expected $revision. Remove .deps/MkPFS and retry."
}
& git -C $checkout diff --quiet
if ($LASTEXITCODE -ne 0) {
    Fail "MkPFS cache has local changes. Remove .deps/MkPFS and retry."
}

foreach ($required in @("pyproject.toml", "LICENSE", "mkpfs/__main__.py")) {
    $requiredPath = Join-Path $checkout $required
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        Fail "Required MkPFS file not found: $requiredPath"
    }
}

$venv = Join-Path $checkout ".venv"
$venvPython = Join-Path $venv "Scripts/python.exe"
$stamp = Join-Path $venv ".boilerplate-revision"
if (-not (Test-Path -LiteralPath $venvPython -PathType Leaf)) {
    & $Python -m venv $venv
    if ($LASTEXITCODE -ne 0) {
        Fail "Unable to create the MkPFS virtual environment."
    }
}
$installedRevision = if (Test-Path -LiteralPath $stamp -PathType Leaf) {
    (Get-Content -LiteralPath $stamp -Raw).Trim()
} else {
    ""
}
if ($installedRevision -ne $revision) {
    & $venvPython -m pip install --disable-pip-version-check $checkout | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Fail "MkPFS dependency installation failed."
    }
    Set-Content -LiteralPath $stamp -Value $revision -Encoding ascii
}

& $venvPython -m mkpfs --help *> $null
if ($LASTEXITCODE -ne 0) {
    Fail "The cached MkPFS command failed its startup check."
}

Write-Host "MkPFS ready at revision $revision."
Write-Output $venvPython
