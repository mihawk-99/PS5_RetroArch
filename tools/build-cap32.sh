#!/usr/bin/env bash
# Cross-build Caprice32's libretro core (Amstrad CPC), from my fork ../PS5_Cap32 (github.com/mihawk-99/PS5_Cap32)
# at its pinned revision (tools/core-fork.sh).
#
# Output: build/cores/stage/{cores,info}/cap32_libretro.{so,info}. It is not in tools/build-title.sh's
# list of cores: the core holds Amstrad's system ROMs in its own code (cap32/rom/), and no build of
# this title ships a console's firmware. It renders in software.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
core_name=cap32
source "$root/tools/core-stamp.sh"
source "$root/tools/core-fork.sh"
core_stamp_skip cap32 \
    "$root/build/cores/stage/cores/cap32_libretro.so" \
    "$root/build/cores/stage/info/cap32_libretro.info" \
    -- "$root/tools/build-cap32.sh" "$root/tools/core-fork.sh"
[[ $# == 0 ]] || { echo "usage: ${0##*/}" >&2; exit 2; }

revision=5783666e94f9ae92f5b910ab5f5bc6c0bc991f91  # ../PS5_Cap32 main
core_fork_setup
core_fork_checkout PS5_Cap32 "$revision"
core_fork_info cap32_libretro.info 7ef5d3ad67d195a5dc87ed62d9edf6732169cbde6da045583cc81a1264aad6fb

# LDFLAGS goes in the environment: the makefiles add their own to it.
LDFLAGS="$core_ldflags $core_libs" make -C "$source_dir" -j"${JOBS:-16}" platform=ps5 \
    CC="$core_cc" CXX="$core_cxx" AR="$AR"
core_fork_stage "$source_dir/cap32_libretro.so" "$core_info" "$revision" tools/build-cap32.sh
