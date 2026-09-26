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

# Each C++ test with the flags its tests/test_*.py builds it with: they include
# the sources they test, and those include RetroArch's headers or the platform
# layer's. input_ps5_test.cpp includes a header its test extracts from
# RetroArch's source at run time, so it has no standalone build to check.
platform_include=$(mktemp -d)
trap 'rm -rf -- "$platform_include"' EXIT
ln -s "$sdk/target/include/ps5platform" "$platform_include/ps5platform"
vendor="-I$root/vendor/retroarch -I$root/vendor/retroarch/libretro-common/include"
declare -A test_flags=(
    [audio_ps5_test.cpp]="-std=c++17 $vendor"
    [frontend_ps5_test.cpp]="-std=c++17 $vendor -I$platform_include"
    [memory_diagnostics_test.cpp]="-std=c++20 -I$root -DPS5_MEMORY_DIAGNOSTICS"
    [memory_ps5_test.cpp]="-std=c++17 -I$root"
    [memory_vulkan_test.cpp]="-std=c++20 -I$root -I$root/vendor/retroarch -DPS5_MEMORY_DIAGNOSTICS"
    [menu_memory_test.cpp]="-std=c++17 -I$root"
    [input_ps5_test.cpp]="skip"
)
for source in "$root"/tests/*.cpp; do
    name=$(basename -- "$source")
    [[ -n ${test_flags[$name]:-} ]] || {
        echo "tests/$name has no tidy flags in tools/run_clang_tidy.sh" >&2
        exit 2
    }
    [[ ${test_flags[$name]} == skip ]] && continue
    read -r -a flags <<< "${test_flags[$name]}"
    "$tidy" "$source" --quiet --warnings-as-errors='*' -- "${flags[@]}"
done

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
