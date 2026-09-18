#!/usr/bin/env bash
# ps5-native-app-boilerplate - Clang formatter driver.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Applies the formatting policy shared with the CPython PS5 project.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
formatter=${CLANG_FORMAT:-}
if [[ -z $formatter ]]; then
    formatter=$(command -v clang-format-18 || command -v clang-format || true)
fi
[[ -n $formatter ]] || { echo "clang-format is required" >&2; exit 2; }

mapfile -d '' sources < <(find "$root/src" "$root/tooling/native" "$root/tests" -type f \
    \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) \
    -print0)
if [[ ${1:-} == --check ]]; then
    "$formatter" --dry-run --Werror "${sources[@]}"
elif [[ $# -eq 0 ]]; then
    "$formatter" -i "${sources[@]}"
else
    echo "usage: tools/run_clang_format.sh [--check]" >&2
    exit 2
fi
