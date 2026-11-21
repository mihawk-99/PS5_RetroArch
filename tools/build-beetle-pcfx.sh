#!/usr/bin/env bash
# Cross-build Beetle PC-FX (mednafen_pcfx), Mednafen's NEC PC-FX core, from my fork
# ../PS5_BeetlePCFX (github.com/mihawk-99/PS5_BeetlePCFX) at its pinned revision
# (tools/core-fork.sh).
#
# Output: build/cores/stage/{cores,info}/mednafen_pcfx_libretro.{so,info};
# build-title.sh stages those in /app0. It renders in software. Games need the
# PC-FX BIOS in system/ (pcfx.rom).
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
core_name=beetle-pcfx
source "$root/tools/core-stamp.sh"
source "$root/tools/core-fork.sh"
core_stamp_skip beetle-pcfx \
    "$root/build/cores/stage/cores/mednafen_pcfx_libretro.so" \
    "$root/build/cores/stage/info/mednafen_pcfx_libretro.info" \
    -- "$root/tools/build-beetle-pcfx.sh" "$root/tools/core-fork.sh" \
    "$root/patches/beetle-pcfx/missing-disc.patch"
[[ $# == 0 ]] || { echo "usage: ${0##*/}" >&2; exit 2; }

revision=fda7d214e6326febc4c18a6a0e1949ce78eba163  # ../PS5_BeetlePCFX main
core_fork_setup
core_fork_checkout PS5_BeetlePCFX "$revision"
# A missing or unreadable disc image fails the load instead of crashing the core.
patch -d "$source_dir" -p1 < "$root/patches/beetle-pcfx/missing-disc.patch"
core_fork_info mednafen_pcfx_libretro.info d352e83266f1d965e1b0fcb190dc3300158f419985b8d77374a2994d00f38f19

# LDFLAGS goes in the environment: the makefiles add their own to it.
LDFLAGS="$core_ldflags $core_libs" make -C "$source_dir" -j"${JOBS:-16}" platform=ps5 CC="$core_cc" CXX="$core_cxx" AR="$AR"
core_fork_stage "$source_dir/mednafen_pcfx_libretro.so" "$core_info" "$revision" tools/build-beetle-pcfx.sh \
    patches/beetle-pcfx/missing-disc.patch
