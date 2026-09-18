#!/usr/bin/env bash
# ps5-native-app-boilerplate - Linux/WSL clean-room runtime reproducer.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Rebuilds twice and installs only the byte-identical recorded artifact.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work="$root/build/runtime-shim"
native="$root/tooling/native"
expected_raw="8ee6e124993e1af26420cb455890fd002f5d6c7e78883c860ce45734e7d002bb"
expected_signed="e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036"
manifest="$root/runtime/libc.prx.sha256"

grep -Fxq "$expected_signed *libc.prx" "$manifest" || {
    echo "runtime checksum manifest does not match the release digest" >&2
    exit 2
}

bash "$root/tools/setup-native-dependencies.sh" --skip-sdk >/dev/null
zlib_root="$root/.deps/native/zlib/root"
zlib_archive=$(find "$zlib_root" -type f -name libz.a -print -quit)
cxx=${CXX:-}
if [[ -z $cxx ]]; then
    cxx=$(command -v clang++-18 || command -v clang++)
fi
[[ -n $cxx ]] || { echo "Clang++ was not found" >&2; exit 2; }

mkdir -p "$work"
"$cxx" -std=c++20 -O2 -Wall -Wextra -Werror \
    "$native/libc_builder.cpp" -o "$work/libc-builder"
"$cxx" -std=c++20 -O2 -Wall -Wextra -Werror \
    -I "$zlib_root/usr/include" \
    "$native/native_app_builder.cpp" "$native/self_container.cpp" \
    "$native/elf_object.cpp" "$native/sce_module_writer.cpp" \
    "$zlib_archive" -o "$work/ps5-native-tool"

for copy in a b; do
    "$work/libc-builder" "$native/runtime/api-surface.txt" \
        "$native/runtime/imports.txt" "$work/libc-$copy.raw.elf"
done
cmp --silent "$work/libc-a.raw.elf" "$work/libc-b.raw.elf"
raw_hash=$(sha256sum "$work/libc-a.raw.elf" | cut -d ' ' -f 1)
[[ $raw_hash == "$expected_raw" ]] || {
    echo "raw runtime hash mismatch: $raw_hash" >&2
    exit 2
}

for copy in a b; do
    "$work/ps5-native-tool" self --sign \
        --in "$work/libc-$copy.raw.elf" --out "$work/libc-$copy.prx"
done
cmp --silent "$work/libc-a.prx" "$work/libc-b.prx"
signed_hash=$(sha256sum "$work/libc-a.prx" | cut -d ' ' -f 1)
[[ $signed_hash == "$expected_signed" ]] || {
    echo "signed runtime hash mismatch: $signed_hash" >&2
    exit 2
}
[[ $(stat -c %s "$work/libc-a.prx") == 1284674 ]] || {
    echo "signed runtime size mismatch" >&2
    exit 2
}

for artifact in "$work/libc-a.raw.elf" "$work/libc-a.prx"; do
    grep -aFq BlackBearReloaded "$artifact" || {
        echo "generated runtime is missing its attribution marker" >&2
        exit 2
    }
    for forbidden in W:/Build J013 Prospero_Release sys/internal; do
        if grep -aFq "$forbidden" "$artifact"; then
            echo "generated runtime contains forbidden text: $forbidden" >&2
            exit 2
        fi
    done
done

cp "$work/libc-a.prx" "$root/runtime/libc.prx"
(cd "$root/runtime" && sha256sum -c libc.prx.sha256)
printf 'Rebuilt clean-room runtime.\nRaw SHA-256:    %s\nSigned SHA-256: %s\n' \
    "$raw_hash" "$signed_hash"
