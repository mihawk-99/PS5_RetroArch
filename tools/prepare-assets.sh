#!/usr/bin/env bash
# ps5-native-app-boilerplate - Bash presentation asset preparation frontend.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Provides Linux/WSL-style arguments for the Windows DirectXTex and ATRAC9
# conversion workflow while keeping validation available on native Linux.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output_directory="$root/sce_sys"
validate_only=0
conversion_inputs=0
declare -a forwarded=()

usage() {
    cat <<'EOF'
usage: tools/prepare-assets.sh [options]

  --icon PATH                  Prepare sce_sys/icon0.png
  --background PATH            Use one image for pic0.dds and pic1.dds
  --selection-background PATH  Prepare pic0.dds from distinct artwork
  --launch-background PATH     Prepare pic1.dds from distinct artwork
  --audio PATH                 Copy ready AT9 or prepare supported source audio
  --audio-start SECONDS        Start of the audio excerpt (default: 0)
  --audio-duration SECONDS     Excerpt duration (default: 15; maximum: 87.3)
  --texconv COMMAND            texconv.exe name or path
  --ffmpeg COMMAND             Windows FFmpeg name or path
  --at9-tool COMMAND           Legally obtained compatible ATRAC9 encoder
  --output-directory PATH      Destination (default: sce_sys)
  --validate-only              Validate current files on Linux or WSL
  -h, --help                   Show this help

Conversion runs through Windows PowerShell and therefore requires WSL or a
Windows Bash environment. Validation is portable and requires only Python 3.
EOF
}

need_value() {
    [[ $# -ge 2 && -n $2 ]] || {
        echo "missing value for $1" >&2
        exit 2
    }
}

while (($#)); do
    case "$1" in
        --icon|--background|--selection-background|--launch-background|--audio)
            need_value "$@"
            forwarded+=("$1" "$2")
            ((conversion_inputs += 1))
            shift 2
            ;;
        --texconv|--ffmpeg|--at9-tool|--audio-start|--audio-duration)
            need_value "$@"
            forwarded+=("$1" "$2")
            shift 2
            ;;
        --output-directory)
            need_value "$@"
            output_directory=$2
            shift 2
            ;;
        --validate-only)
            validate_only=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if ((validate_only)); then
    ((${#forwarded[@]} == 0)) || {
        echo "--validate-only cannot be combined with conversion options" >&2
        exit 2
    }
    exec bash "$root/tools/validate-assets.sh" "$output_directory"
fi
((conversion_inputs > 0)) || {
    echo "supply an icon, background, or audio input, or use --validate-only" >&2
    exit 2
}

if command -v powershell.exe >/dev/null 2>&1; then
    powershell_command=$(command -v powershell.exe)
elif command -v powershell >/dev/null 2>&1; then
    powershell_command=$(command -v powershell)
else
    echo "conversion requires Windows PowerShell through WSL or Windows Bash" >&2
    exit 2
fi

to_windows_path() {
    local path=$1
    case "$path" in
        [A-Za-z]:\\*) printf '%s\n' "$path"; return ;;
    esac
    if command -v wslpath >/dev/null 2>&1; then
        wslpath -w -- "$(realpath -m -- "$path")"
    elif command -v cygpath >/dev/null 2>&1; then
        cygpath -w -- "$path"
    else
        echo "cannot translate path for Windows: $path" >&2
        exit 2
    fi
}

to_windows_command() {
    local command=$1
    if [[ -e $command || $command == */* ]]; then
        to_windows_path "$command"
    else
        printf '%s\n' "$command"
    fi
}

script_path=$(to_windows_path "$root/tools/prepare-assets.ps1")
declare -a powershell_args=(-NoProfile -ExecutionPolicy Bypass -File "$script_path")
for ((index = 0; index < ${#forwarded[@]}; index += 2)); do
    option=${forwarded[index]}
    value=${forwarded[index + 1]}
    case "$option" in
        --icon) powershell_args+=(-Icon "$(to_windows_path "$value")") ;;
        --background) powershell_args+=(-Background "$(to_windows_path "$value")") ;;
        --selection-background) powershell_args+=(-SelectionBackground "$(to_windows_path "$value")") ;;
        --launch-background) powershell_args+=(-LaunchBackground "$(to_windows_path "$value")") ;;
        --audio) powershell_args+=(-Audio "$(to_windows_path "$value")") ;;
        --texconv) powershell_args+=(-Texconv "$(to_windows_command "$value")") ;;
        --ffmpeg) powershell_args+=(-Ffmpeg "$(to_windows_command "$value")") ;;
        --at9-tool) powershell_args+=(-At9Tool "$(to_windows_command "$value")") ;;
        --audio-start) powershell_args+=(-AudioStart "$value") ;;
        --audio-duration) powershell_args+=(-AudioDuration "$value") ;;
    esac
done
powershell_args+=(-OutputDirectory "$(to_windows_path "$output_directory")")

"$powershell_command" "${powershell_args[@]}"
