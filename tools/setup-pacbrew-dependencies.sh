#!/usr/bin/env bash
# ps5-native-app-boilerplate - PacBrew dependency bootstrapper.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Installs the pinned PacBrew ports prefix into the ignored repository cache
# and resolves selected pkg-config modules without modifying the host SDK.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cache="$root/.deps/pacbrew"
version=v0.40.2
archive="$cache/ps5-payload-dev-$version.tar.gz"
release="$cache/$version"
sysroot="$release/sysroot"
url="https://github.com/ps5-payload-dev/pacbrew-repo/releases/download/$version/ps5-payload-dev.tar.gz"
hash=a85f65de418a8e6a898c6c3e3c870d50fff7618a200e4dd59ea9692af6ecec4d
mode=${1:-}
[[ -n $mode ]] && shift

case "$mode" in
    --all|--list|--environment|--resolve) ;;
    *)
        echo "usage: tools/setup-pacbrew-dependencies.sh --all|--list|--environment|--resolve MODULE..." >&2
        exit 2
        ;;
esac

command -v python3 >/dev/null || {
    echo "missing required command for PacBrew: python3" >&2
    exit 2
}

modules=("$@")
if [[ $mode == --environment ]]; then
    modules=()
    if [[ -n ${PACBREW_PACKAGES:-} ]]; then
        read -r -a modules <<< "$PACBREW_PACKAGES"
    fi
    if [[ -z ${PACBREW_PACKAGES:-}${PACBREW_INCLUDE_PATHS:-}${PACBREW_STATIC_ARCHIVES:-} ]]; then
        echo "==> [pacbrew] No dependencies selected through Make variables" >&2
        exit 0
    fi
fi

for command in pkg-config sha256sum tar wget; do
    command -v "$command" >/dev/null || {
        echo "missing required command for PacBrew: $command" >&2
        exit 2
    }
done

for module in "${modules[@]}"; do
    [[ $module =~ ^[A-Za-z0-9_.+-]+$ ]] || {
        echo "invalid PacBrew pkg-config module: $module" >&2
        exit 2
    }
done

mkdir -p "$cache"
marker="$release/.complete"
expected_marker="$version $hash"
actual_marker=""
[[ ! -f $marker ]] || actual_marker=$(<"$marker")
if [[ $actual_marker != "$expected_marker" || ! -d $sysroot/user/homebrew/lib ]]; then
    if [[ ! -f $archive ]] || ! printf '%s  %s\n' "$hash" "$archive" | sha256sum --check --strict >/dev/null 2>&1; then
        temporary="$archive.download"
        echo "==> [pacbrew] Downloading $version prebuilt ports (about 346 MB)" >&2
        wget -q "$url" -O "$temporary"
        printf '%s  %s\n' "$hash" "$temporary" | sha256sum --check --strict >&2
        mv "$temporary" "$archive"
    fi

    temporary_root=$(mktemp -d "$cache/.extract.XXXXXX")
    trap 'rm -rf -- "$temporary_root"' EXIT
    mkdir -p "$temporary_root/sysroot"
    echo "==> [pacbrew] Extracting the isolated ports prefix" >&2
    tar -xzf "$archive" -C "$temporary_root/sysroot" --strip-components=3 \
        opt/ps5-payload-sdk/target/user/homebrew
    [[ -d $temporary_root/sysroot/user/homebrew/include &&
        -d $temporary_root/sysroot/user/homebrew/lib ]] || {
        echo "PacBrew release has an unexpected sysroot layout" >&2
        exit 2
    }
    printf '%s\n' "$expected_marker" > "$temporary_root/.complete"
    [[ $release == "$cache/$version" ]] || exit 2
    rm -rf -- "$release"
    mv "$temporary_root" "$release"
    trap - EXIT
fi

export PKG_CONFIG_DIR=
export PKG_CONFIG_SYSROOT_DIR="$sysroot"
export PKG_CONFIG_LIBDIR="$sysroot/user/homebrew/lib/pkgconfig"
export PKG_CONFIG_PATH="$sysroot/user/homebrew/libdata/pkgconfig"

case "$mode" in
    --all)
        printf '%s\n' "$sysroot"
        ;;
    --list)
        pkg-config --list-all | LC_ALL=C sort
        ;;
    --environment)
        if (( ${#modules[@]} > 0 )); then
            pkg-config --static --exists "${modules[@]}" || {
                echo "PacBrew does not provide every selected pkg-config module: ${modules[*]}" >&2
                exit 2
            }
        fi
        echo "==> [pacbrew] Declared dependencies are cached" >&2
        ;;
    --resolve)
        python3 - "$sysroot" "${modules[@]}" <<'PY'
import json, os, shlex, subprocess, sys

sysroot, *modules = sys.argv[1:]
try:
    if modules:
        subprocess.run(["pkg-config", "--static", "--exists", *modules], check=True)
        cflags = shlex.split(subprocess.check_output(
            ["pkg-config", "--static", "--cflags", *modules], text=True))
        raw_libs = shlex.split(subprocess.check_output(
            ["pkg-config", "--static", "--libs", *modules], text=True))
    else:
        cflags, raw_libs = [], []
except subprocess.CalledProcessError:
    raise SystemExit(f"PacBrew does not provide every selected pkg-config module: {' '.join(modules)}")

libs = []
for flag in raw_libs:
    if flag == "-pthread":
        if flag not in cflags:
            cflags.append(flag)
    elif flag.startswith("-Wl,"):
        libs.extend(part for part in flag[4:].split(",") if part)
    else:
        libs.append(flag)

print(json.dumps({"version": "v0.40.2", "root": sysroot,
                  "packages": modules, "cflags": cflags, "libs": libs}))
PY
        ;;
esac
