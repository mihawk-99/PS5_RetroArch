#!/usr/bin/env bash
# ps5-native-app-boilerplate - Linux/WSL package-tool bootstrapper.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Fetches UFS2Tool or MkPFS into the ignored cache without a global install.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
kind=${1:-}

case "$kind" in
    ffpkg)
        for command in dotnet git; do
            command -v "$command" >/dev/null || {
                echo "missing required command: $command" >&2
                exit 2
            }
        done
        dotnet_major=$(dotnet --version | cut -d. -f1)
        [[ $dotnet_major =~ ^[0-9]+$ && $dotnet_major -ge 8 ]] || {
            echo "UFS2Tool requires the .NET SDK 8 or newer" >&2
            exit 2
        }
        checkout="$root/.deps/UFS2Tool"
        revision="b5307a60d5b4e3a68ba680e0e33cfadf05017c77"
        if [[ ! -d $checkout/.git ]]; then
            mkdir -p "$checkout"
            git -C "$checkout" init --quiet
            git -C "$checkout" remote add origin https://github.com/SvenGDK/UFS2Tool.git
            git -C "$checkout" fetch --quiet --depth 1 origin "$revision"
            git -C "$checkout" checkout --quiet --detach FETCH_HEAD
        fi
        actual=$(git -C "$checkout" rev-parse HEAD)
        [[ $actual == "$revision" ]] || {
            echo "UFS2Tool cache is at $actual; expected $revision" >&2
            exit 2
        }
        git -C "$checkout" diff --quiet || {
            echo "UFS2Tool cache has local changes" >&2
            exit 2
        }
        output="$checkout/.build-linux"
        binary="$output/UFS2Tool.dll"
        stamp="$output/.boilerplate-revision"
        if [[ ! -f $binary || ! -f $stamp || $(<"$stamp") != "$revision" ]]; then
            dotnet build "$checkout/UFS2Tool.csproj" --configuration Release \
                --output "$output" --nologo --verbosity quiet >&2
            printf '%s\n' "$revision" > "$stamp"
        fi
        runner="$checkout/.ufs2tool-run-linux"
        printf '#!/bin/sh\nDOTNET_ROLL_FORWARD=Major exec dotnet "%s" "$@"\n' \
            "$binary" > "$runner"
        chmod +x "$runner"
        "$runner" --help >/dev/null || [[ $? -eq 1 ]]
        printf '%s\n' "$runner"
        ;;
    ffpfsc)
        for command in git python3; do
            command -v "$command" >/dev/null || {
                echo "missing required command: $command" >&2
                exit 2
            }
        done
        checkout="$root/.deps/MkPFS"
        revision="6cb8313dfe0c988ac52617794553f343243d3a56"
        if [[ ! -d $checkout/.git ]]; then
            mkdir -p "$checkout"
            git -C "$checkout" init --quiet
            git -C "$checkout" remote add origin https://github.com/PSBrew/MkPFS.git
            git -C "$checkout" fetch --quiet --depth 1 origin "$revision"
            git -C "$checkout" checkout --quiet --detach FETCH_HEAD
        fi
        actual=$(git -C "$checkout" rev-parse HEAD)
        [[ $actual == "$revision" ]] || {
            echo "MkPFS cache is at $actual; expected $revision" >&2
            exit 2
        }
        git -C "$checkout" diff --quiet || {
            echo "MkPFS cache has local changes" >&2
            exit 2
        }
        venv="$checkout/.venv-linux"
        python="$venv/bin/python"
        stamp="$venv/.boilerplate-revision"
        if [[ ! -x $python ]]; then
            rm -rf -- "$venv"
            python3 -m venv "$venv" || {
                echo "Python venv support is required (install python3-venv)" >&2
                exit 2
            }
        fi
        if ! "$python" -m pip --version >/dev/null 2>&1; then
            python3 -m pip --python "$python" install \
                --disable-pip-version-check --quiet pip >&2 || {
                echo "Unable to seed pip in the MkPFS virtual environment" >&2
                exit 2
            }
        fi
        if [[ ! -f $stamp || $(<"$stamp") != "$revision" ]]; then
            "$python" -m pip install --disable-pip-version-check --quiet \
                "$checkout" >&2
            printf '%s\n' "$revision" > "$stamp"
        fi
        runner="$checkout/.mkpfs-run-linux"
        printf '#!/bin/sh\nexec "%s" -m mkpfs "$@"\n' "$python" > "$runner"
        chmod +x "$runner"
        "$runner" --help >/dev/null
        printf '%s\n' "$runner"
        ;;
    *)
        echo "usage: tools/setup-packaging-dependencies.sh <ffpkg|ffpfsc>" >&2
        exit 2
        ;;
esac
