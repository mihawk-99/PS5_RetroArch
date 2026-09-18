#!/usr/bin/env bash
# ps5-native-app-boilerplate - Clang static-analysis driver.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs the analyzer profile shared with the CPython PS5 project.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tidy=${CLANG_TIDY:-}
if [[ -z $tidy ]]; then
    tidy=$(command -v clang-tidy-18 || command -v clang-tidy || true)
fi
[[ -n $tidy ]] || { echo "clang-tidy is required" >&2; exit 2; }

bash "$root/tools/setup-native-dependencies.sh" >/dev/null
sdk="$root/.deps/native/ps5-payload-sdk"
zlib="$root/.deps/native/zlib/root/usr/include"

mapfile -d '' host_sources < <(find "$root/tooling/native" -maxdepth 1 \
    -type f -name '*.cpp' ! -name 'app_crt.cpp' ! -name 'app_cpp_runtime.cpp' -print0)
"$tidy" "${host_sources[@]}" --quiet --warnings-as-errors='*' -- \
    -std=c++20 -I"$zlib"

gtest=$(bash "$root/tools/setup-test-dependencies.sh")
mapfile -d '' test_sources < <(find "$root/tests" -maxdepth 1 -type f -name '*.cpp' -print0)
if (( ${#test_sources[@]} )); then
    "$tidy" "${test_sources[@]}" --quiet --warnings-as-errors='*' -- \
        -std=c++20 -I"$root/src" -isystem "$gtest/googletest/include"
fi

mapfile -d '' app_c_sources < <(find "$root/src" -type f -name '*.c' -print0)
if (( ${#app_c_sources[@]} )); then
    "$tidy" "${app_c_sources[@]}" --quiet --warnings-as-errors='*' -- \
        -std=c11 -isystem "$sdk/target/include"
fi

mapfile -d '' app_cpp_sources < <(find "$root/src" -type f \
    \( -name '*.cc' -o -name '*.cpp' \) -print0)
app_cpp_sources+=("$root/tooling/native/app_crt.cpp" "$root/tooling/native/app_cpp_runtime.cpp")
if (( ${#app_cpp_sources[@]} )); then
    "$tidy" "${app_cpp_sources[@]}" --quiet --warnings-as-errors='*' -- \
        -std=c++20 -fno-exceptions -fno-rtti --target=x86_64-sie-ps5 \
        -isystem "$sdk/target/include/c++/v1" -isystem "$sdk/target/include"
fi
