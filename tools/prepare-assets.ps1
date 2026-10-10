<#
  ps5-native-app-boilerplate - Presentation asset preparation.
  Copyright (C) 2026 BlackBearReloaded
  SPDX-License-Identifier: GPL-3.0-or-later

Converts developer-owned artwork and audio into the supported launcher
  asset profile, or validates the current sce_sys presentation files.
#>

#requires -Version 5.1
[CmdletBinding()]
param(
    [switch]$ValidateOnly,
    [string]$Icon = "",
    [string]$Background = "",
    [string]$SelectionBackground = "",
    [string]$LaunchBackground = "",
    [string]$Audio = "",
    [string]$Texconv = "",
    [string]$Ffmpeg = "",
    [string]$At9Tool = "",
    [ValidateRange(0, 86400)]
    [double]$AudioStart = 0,
    [ValidateRange(0.1, 87.3)]
    [double]$AudioDuration = 15,
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repo "sce_sys"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)

function Fail([string]$Message) {
    throw "prepare-assets: $Message"
}

function Read-U16([byte[]]$Bytes, [int]$Offset) {
    if ($Offset -lt 0 -or $Offset + 2 -gt $Bytes.Length) {
        Fail "Unexpected end of file."
    }
    return [BitConverter]::ToUInt16($Bytes, $Offset)
}

function Read-U32([byte[]]$Bytes, [int]$Offset) {
    if ($Offset -lt 0 -or $Offset + 4 -gt $Bytes.Length) {
        Fail "Unexpected end of file."
    }
    return [BitConverter]::ToUInt32($Bytes, $Offset)
}

function Read-U32BigEndian([byte[]]$Bytes, [int]$Offset) {
    if ($Offset -lt 0 -or $Offset + 4 -gt $Bytes.Length) {
        Fail "Unexpected end of file."
    }
    return ([uint32]$Bytes[$Offset] -shl 24) -bor
        ([uint32]$Bytes[$Offset + 1] -shl 16) -bor
        ([uint32]$Bytes[$Offset + 2] -shl 8) -bor
        [uint32]$Bytes[$Offset + 3]
}

function Read-Ascii([byte[]]$Bytes, [int]$Offset, [int]$Count) {
    if ($Offset -lt 0 -or $Offset + $Count -gt $Bytes.Length) {
        Fail "Unexpected end of file."
    }
    return [Text.Encoding]::ASCII.GetString($Bytes, $Offset, $Count)
}

function Resolve-Input([string]$Path, [string]$Label) {
    if (-not $Path -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        Fail "$Label file not found: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Resolve-Executable([string]$Path, [string[]]$Names, [string]$Help) {
    if ($Path) {
        if (Test-Path -LiteralPath $Path -PathType Leaf) {
            return (Resolve-Path -LiteralPath $Path).Path
        }
        $command = Get-Command $Path -ErrorAction SilentlyContinue
        if ($command) {
            return $command.Source
        }
        Fail "Executable not found: $Path"
    }
    foreach ($name in $Names) {
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if ($command) {
            return $command.Source
        }
    }
    Fail $Help
}

function Invoke-Checked([string]$Executable, [string[]]$Arguments, [string]$Label) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        Fail "$Label failed with exit code $LASTEXITCODE."
    }
}

function Convert-BackgroundAsset(
    [string]$Source,
    [string]$Label,
    [string]$Stem,
    [string]$WorkDirectory,
    [string]$Destination,
    [string]$TexconvPath
) {
    $resolvedSource = Resolve-Input $Source $Label
    $assetWork = Join-Path $WorkDirectory $Stem
    New-Item -ItemType Directory -Path $assetWork | Out-Null
    Invoke-Checked $TexconvPath @("-nologo", "-y", "-w", "3840", "-h", "2160", "-m", "1",
        "-f", "R8G8B8A8_UNORM", "-ft", "png", "-o", $assetWork, $resolvedSource) `
        "$Label preview conversion"
    $preview = Get-ChildItem -LiteralPath $assetWork -File |
        Where-Object { $_.Extension -ieq ".png" } | Select-Object -First 1
    if (-not $preview) {
        Fail "texconv did not produce the $Label preview."
    }
    Invoke-Checked $TexconvPath @("-nologo", "-y", "-w", "3840", "-h", "2160", "-m", "1",
        "-f", "BC7_UNORM", "-dx10", "-o", $assetWork, $preview.FullName) "$Label DDS conversion"
    $dds = Get-ChildItem -LiteralPath $assetWork -File |
        Where-Object { $_.Extension -ieq ".dds" } | Select-Object -First 1
    if (-not $dds) {
        Fail "texconv did not produce the $Label BC7 DDS."
    }
    [IO.File]::Copy($dds.FullName, (Join-Path $Destination "$Stem.dds"), $true)
    return [PSCustomObject]@{ Preview = $preview.FullName; Dds = $dds.FullName }
}

function Test-Png([string]$Path, [int]$ExpectedWidth, [int]$ExpectedHeight) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $signature = [byte[]](137, 80, 78, 71, 13, 10, 26, 10)
    if ($bytes.Length -lt 24) {
        Fail "$Path is too small to be a PNG."
    }
    for ($i = 0; $i -lt $signature.Length; $i++) {
        if ($bytes[$i] -ne $signature[$i]) {
            Fail "$Path is not a PNG file."
        }
    }
    $width = Read-U32BigEndian $bytes 16
    $height = Read-U32BigEndian $bytes 20
    if ($width -ne $ExpectedWidth -or $height -ne $ExpectedHeight) {
        Fail "$Path must be ${ExpectedWidth}x${ExpectedHeight}; found ${width}x${height}."
    }
}

function Test-Dds([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 148 -or (Read-Ascii $bytes 0 4) -ne "DDS ") {
        Fail "$Path is not a DDS file."
    }
    $headerSize = Read-U32 $bytes 4
    $height = Read-U32 $bytes 12
    $width = Read-U32 $bytes 16
    $mipCount = Read-U32 $bytes 28
    $fourCc = Read-Ascii $bytes 84 4
    $format = Read-U32 $bytes 128
    $dimension = Read-U32 $bytes 132
    $arraySize = Read-U32 $bytes 140
    $expectedSize = [long]148 + ([long]$width / 4) * ([long]$height / 4) * 16
    if ($headerSize -ne 124 -or $width -ne 3840 -or $height -ne 2160 -or
        $mipCount -ne 1 -or $fourCc -ne "DX10" -or $format -ne 98 -or
        $dimension -ne 3 -or $arraySize -ne 1 -or $bytes.Length -ne $expectedSize) {
        Fail "$Path must be a single 3840x2160 BC7_UNORM (98) DX10 2D DDS without mipmaps."
    }
}

function Test-At9([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 128 -or (Read-Ascii $bytes 0 4) -ne "RIFF" -or
        (Read-Ascii $bytes 8 4) -ne "WAVE" -or (Read-U32 $bytes 4) + 8 -ne $bytes.Length) {
        Fail "$Path is not a complete RIFF/WAVE file."
    }

    $chunks = @{}
    $position = 12
    while ($position + 8 -le $bytes.Length) {
        $id = Read-Ascii $bytes $position 4
        $size = [int](Read-U32 $bytes ($position + 4))
        $payload = $position + 8
        if ($size -lt 0 -or $payload + $size -gt $bytes.Length) {
            Fail "$Path contains a truncated RIFF chunk."
        }
        $chunks[$id] = @($payload, $size)
        $position = $payload + $size + ($size % 2)
    }
    foreach ($required in @("fmt ", "fact", "smpl", "data")) {
        if (-not $chunks.ContainsKey($required)) {
            Fail "$Path is missing the required RIFF '$required' chunk."
        }
    }

    $fmt = [int]$chunks["fmt "][0]
    $smpl = [int]$chunks["smpl"][0]
    if ([int]$chunks["fmt "][1] -lt 40 -or [int]$chunks["smpl"][1] -lt 32) {
        Fail "$Path contains undersized ATRAC9 format or loop metadata."
    }
    $formatGuid = "D242E147BA368D4D88FC61654F8C836C"
    $actualGuid = -join ($bytes[($fmt + 24)..($fmt + 39)] | ForEach-Object { $_.ToString("X2") })
    $channels = Read-U16 $bytes ($fmt + 2)
    $byteRate = Read-U32 $bytes ($fmt + 8)
    $loops = Read-U32 $bytes ($smpl + 28)
    if ((Read-U16 $bytes $fmt) -ne 0xFFFE -or $channels -notin @(1, 2) -or
        (Read-U32 $bytes ($fmt + 4)) -ne 48000 -or $byteRate -le 0 -or
        $byteRate -gt ($channels * 12000) -or $actualGuid -ne $formatGuid -or
        $loops -lt 1 -or $bytes.Length -gt 2097152) {
        Fail "$Path must be looped 48 kHz ATRAC9 at no more than 96 kb/s mono or 192 kb/s stereo, and no larger than 2 MiB."
    }
}

function Test-AssetSet([string]$Directory) {
    $iconPath = Join-Path $Directory "icon0.png"
    if (-not (Test-Path -LiteralPath $iconPath -PathType Leaf)) {
        Fail "Required launcher icon not found: $iconPath"
    }
    Test-Png $iconPath 512 512

    $pic0 = Join-Path $Directory "pic0.dds"
    $pic1 = Join-Path $Directory "pic1.dds"
    $hasPic0 = Test-Path -LiteralPath $pic0 -PathType Leaf
    $hasPic1 = Test-Path -LiteralPath $pic1 -PathType Leaf
    if ($hasPic0 -xor $hasPic1) {
        Fail "Supply both pic0.dds and pic1.dds, or neither."
    }
    if ($hasPic0) {
        Test-Dds $pic0
        Test-Dds $pic1
    }

    $sound = Join-Path $Directory "snd0.at9"
    if (Test-Path -LiteralPath $sound -PathType Leaf) {
        Test-At9 $sound
    }
    Write-Host "Presentation assets validated: $Directory"
}

if ($ValidateOnly) {
    Test-AssetSet $OutputDirectory
    return
}
if ($Background -and ($SelectionBackground -or $LaunchBackground)) {
    Fail "Use -Background by itself, or use -SelectionBackground and -LaunchBackground for distinct artwork."
}
if (-not $Icon -and -not $Background -and -not $SelectionBackground -and
    -not $LaunchBackground -and -not $Audio) {
    Fail "Supply an icon, background, or audio input, or use -ValidateOnly."
}
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    Fail "Conversion uses Windows texconv and an optional Windows ATRAC9 encoder; run this command from Windows PowerShell."
}

$root = [IO.Path]::GetPathRoot($OutputDirectory)
if ($OutputDirectory.TrimEnd('\', '/') -eq $root.TrimEnd('\', '/')) {
    Fail "Refusing to use a filesystem root as the output directory."
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$work = Join-Path ([IO.Path]::GetTempPath()) ("ps5-native-assets-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $work | Out-Null

try {
    if ($Icon -or $Background -or $SelectionBackground -or $LaunchBackground) {
        $texconvPath = Resolve-Executable $Texconv @("texconv.exe", "texconv") `
            "texconv was not found. Install it with: winget install Microsoft.DirectXTex.Texconv"
    }

    if ($Icon) {
        $iconSource = Resolve-Input $Icon "Icon"
        $iconWork = Join-Path $work "icon"
        New-Item -ItemType Directory -Path $iconWork | Out-Null
        Invoke-Checked $texconvPath @("-nologo", "-y", "-w", "512", "-h", "512", "-m", "1",
            "-f", "R8G8B8A8_UNORM", "-ft", "png", "-o", $iconWork, $iconSource) "Icon conversion"
        $converted = Get-ChildItem -LiteralPath $iconWork -File |
            Where-Object { $_.Extension -ieq ".png" } | Select-Object -First 1
        if (-not $converted) {
            Fail "texconv did not produce the launcher PNG."
        }
        [IO.File]::Copy($converted.FullName, (Join-Path $OutputDirectory "icon0.png"), $true)
    }

    if ($Background) {
        $convertedBackground = Convert-BackgroundAsset $Background "Background" "pic0" $work `
            $OutputDirectory $texconvPath
        [IO.File]::Copy($convertedBackground.Dds, (Join-Path $OutputDirectory "pic1.dds"), $true)
        [IO.File]::Copy($convertedBackground.Preview,
            (Join-Path $OutputDirectory "background-source.png"), $true)
        [IO.File]::Copy($convertedBackground.Preview,
            (Join-Path $OutputDirectory "launch-background-source.png"), $true)
    } else {
        if ($SelectionBackground) {
            $convertedSelection = Convert-BackgroundAsset $SelectionBackground `
                "Selection background" "pic0" $work $OutputDirectory $texconvPath
            [IO.File]::Copy($convertedSelection.Preview,
                (Join-Path $OutputDirectory "background-source.png"), $true)
        }
        if ($LaunchBackground) {
            $convertedLaunch = Convert-BackgroundAsset $LaunchBackground `
                "Launch background" "pic1" $work $OutputDirectory $texconvPath
            [IO.File]::Copy($convertedLaunch.Preview,
                (Join-Path $OutputDirectory "launch-background-source.png"), $true)
        }
    }

    if ($Audio) {
        $audioSource = Resolve-Input $Audio "Audio"
        $soundOutput = Join-Path $OutputDirectory "snd0.at9"
        if ([IO.Path]::GetExtension($audioSource) -ieq ".at9") {
            if ($audioSource -ne $soundOutput) {
                [IO.File]::Copy($audioSource, $soundOutput, $true)
            }
        } else {
            $at9ToolPath = Resolve-Executable $At9Tool @("ps4_at9tool.exe") `
                "A compatible ATRAC9 encoder was not found. Supply your legally obtained ps4_at9tool.exe with -At9Tool."
            $wav = Join-Path $work "selection.wav"
            $startText = $AudioStart.ToString([Globalization.CultureInfo]::InvariantCulture)
            $durationText = $AudioDuration.ToString([Globalization.CultureInfo]::InvariantCulture)
            $ffmpegArgs = @("-hide_banner", "-loglevel", "error", "-y", "-ss", $startText, "-i", $audioSource,
                "-t", $durationText, "-map_metadata", "-1", "-af", "loudnorm=I=-28:LRA=11:TP=-2",
                "-ar", "48000", "-ac", "2", "-c:a", "pcm_s16le", $wav)
            $ffmpegCommand = if ($Ffmpeg) {
                Resolve-Executable $Ffmpeg @() "FFmpeg was not found."
            } else {
                $command = Get-Command ffmpeg.exe, ffmpeg -ErrorAction SilentlyContinue | Select-Object -First 1
                if ($command) { $command.Source } else { "" }
            }
            if ($ffmpegCommand) {
                Invoke-Checked $ffmpegCommand $ffmpegArgs "Audio preparation"
            } elseif (Get-Command wsl.exe -ErrorAction SilentlyContinue) {
                & wsl.exe --exec sh -lc "command -v ffmpeg >/dev/null 2>&1"
                if ($LASTEXITCODE -ne 0) {
                    Fail "FFmpeg was not found on Windows or in WSL. Install it in WSL with: sudo apt install ffmpeg"
                }
                function To-WslPath([string]$Path) {
                    if ($Path -notmatch '^([A-Za-z]):\\(.*)$') {
                        Fail "WSL FFmpeg requires source files on a Windows drive: $Path"
                    }
                    return "/mnt/$($Matches[1].ToLowerInvariant())/$($Matches[2].Replace('\', '/'))"
                }
                $wslArgs = @($ffmpegArgs)
                $wslArgs[7] = To-WslPath $audioSource
                $wslArgs[$wslArgs.Count - 1] = To-WslPath $wav
                Invoke-Checked "wsl.exe" (@("--exec", "ffmpeg") + $wslArgs) "WSL audio preparation"
            } else {
                Fail "FFmpeg was not found. Install it or pass its executable with -Ffmpeg."
            }
            $encoded = Join-Path $work "snd0.at9"
            Invoke-Checked $at9ToolPath @("-e", "-br", "192", "-wholeloop", $wav, $encoded) `
                "ATRAC9 encoding"
            [IO.File]::Copy($encoded, $soundOutput, $true)
        }

        $paramPath = Join-Path $OutputDirectory "param.json"
        if (Test-Path -LiteralPath $paramPath -PathType Leaf) {
            $param = Get-Content -LiteralPath $paramPath -Raw | ConvertFrom-Json
            $param.pubtools | Add-Member -NotePropertyName loudnessSnd0 -NotePropertyValue "-28.00" -Force
            $json = $param | ConvertTo-Json -Depth 16
            [IO.File]::WriteAllText($paramPath, $json + [Environment]::NewLine,
                [Text.UTF8Encoding]::new($false))
        }
    }

    Test-AssetSet $OutputDirectory
} finally {
    $resolvedWork = [IO.Path]::GetFullPath($work)
    $resolvedTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolvedWork.StartsWith($resolvedTemp, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedWork -PathType Container)) {
        Remove-Item -LiteralPath $resolvedWork -Recurse -Force
    }
}
