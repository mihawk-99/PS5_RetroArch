#!/usr/bin/env bash
# Cross-build Beetle Saturn (mednafen_saturn), Mednafen's Sega Saturn core, from my fork ../PS5_BeetleSaturn
# (github.com/mihawk-99/PS5_BeetleSaturn) at its pinned revision (tools/core-fork.sh).
#
# Output: build/cores/stage/{cores,info}/mednafen_saturn_libretro.{so,info};
# build-title.sh stages those in /app0.
# It renders in software at the Saturn's own resolution: every Saturn core that
# upscales (Kronos, YabaSanshiro) needs OpenGL, which the console does not have.
# Saturn games need the BIOS in system/Saturn/ (sega_101.bin, mpr-17933.bin).
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
core_name=beetle-saturn
source "$root/tools/core-stamp.sh"
source "$root/tools/core-fork.sh"
core_stamp_skip beetle-saturn \
    "$root/build/cores/stage/cores/mednafen_saturn_libretro.so" \
    "$root/build/cores/stage/info/mednafen_saturn_libretro.info" \
    -- "$root/tools/build-beetle-saturn.sh" "$root/tools/core-fork.sh" \
    "$root/patches/beetle-saturn/ps5-executable-memory.patch"
[[ $# == 0 ]] || { echo "usage: ${0##*/}" >&2; exit 2; }

revision=41a8a4b6ea1629790da170af775061a25e3c91cb  # ../PS5_BeetleSaturn main
core_fork_setup
core_fork_checkout PS5_BeetleSaturn "$revision"
# Anonymous RWX mappings are not executable in a PS5 title. Use the SDK JIT pool.
patch -d "$source_dir" -p1 < "$root/patches/beetle-saturn/ps5-executable-memory.patch"
core_fork_info mednafen_saturn_libretro.info ec7785e6a9efd3000f730c463f6711273c1f7d28e87b73f8ee7e2ab6751fc779

# LDFLAGS goes in the environment: the makefiles add their own to it.
LDFLAGS="$core_ldflags $core_libs" make -C "$source_dir" -j"${JOBS:-16}" platform=ps5 CC="$core_cc" CXX="$core_cxx" AR="$AR"
core_fork_stage "$source_dir/mednafen_saturn_libretro.so" "$core_info" "$revision" tools/build-beetle-saturn.sh \
    patches/beetle-saturn/ps5-executable-memory.patch
