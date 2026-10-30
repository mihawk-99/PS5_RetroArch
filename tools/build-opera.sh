#!/usr/bin/env bash
# Cross-build Opera's libretro core (3DO), from my fork ../PS5_Opera
# (github.com/mihawk-99/PS5_Opera) at its pinned revision (tools/core-fork.sh).
#
# Output: build/cores/stage/{cores,info}/opera_libretro.{so,info}; build-title.sh
# stages those in /app0. It renders in software. Games need a 3DO BIOS in system/
# (panafz10.bin and the others its info file lists).
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
core_name=opera
source "$root/tools/core-stamp.sh"
source "$root/tools/core-fork.sh"
core_stamp_skip opera \
    "$root/build/cores/stage/cores/opera_libretro.so" \
    "$root/build/cores/stage/info/opera_libretro.info" \
    -- "$root/tools/build-opera.sh" "$root/tools/core-fork.sh" \
    "$root/tools/patches/opera-wc3-streaming-39bf070.patch"
[[ $# == 0 ]] || { echo "usage: ${0##*/}" >&2; exit 2; }

revision=ab5cb34feb169c4f65921c5ee433f7d29baccb29  # ../PS5_Opera main
core_fork_setup
core_fork_checkout PS5_Opera "$revision"
git -C "$source_dir" apply "$root/tools/patches/opera-wc3-streaming-39bf070.patch"
core_fork_info opera_libretro.info 3edce84cd4bd8afba0727a07a70fef7ccc44e7db7e1e0f00897ed6805087d94f

# LDFLAGS goes in the environment: the makefiles add their own to it.
LDFLAGS="$core_ldflags $core_libs" make -C "$source_dir" -j"${JOBS:-16}" platform=ps5 \
    CC="$core_cc" CXX="$core_cxx" AR="$AR"
core_fork_stage "$source_dir/opera_libretro.so" "$core_info" "$revision" \
    tools/build-opera.sh tools/patches/opera-wc3-streaming-39bf070.patch
