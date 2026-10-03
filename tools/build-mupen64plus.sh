#!/usr/bin/env bash
# Cross-build Mupen64Plus-Next, the Nintendo 64 core, from my fork ../PS5_Mupen64Plus
# (github.com/mihawk-99/PS5_Mupen64Plus) at its pinned revision (tools/core-fork.sh).
#
# Output: build/cores/stage/{cores,info}/mupen64plus_next_libretro.{so,info};
# build-title.sh stages those in /app0.
# The fork's platform=ps5 renders through ParaLLEl-RDP on the frontend's Vulkan
# device, upscaled 8x by default, with ParaLLEl-RSP and the x86-64 dynarec; it
# leaves out GLideN64, which needs OpenGL. The dynarec's linkage is NASM
# assembly, so a pinned NASM is built for the host first.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
core_name=mupen64plus
source "$root/tools/core-stamp.sh"
source "$root/tools/core-fork.sh"
core_stamp_skip mupen64plus \
    "$root/build/cores/stage/cores/mupen64plus_next_libretro.so" \
    "$root/build/cores/stage/info/mupen64plus_next_libretro.info" \
    -- "$root/tools/build-mupen64plus.sh" "$root/tools/core-fork.sh" "$root/tools/host-nasm.sh"
[[ $# == 0 ]] || { echo "usage: ${0##*/}" >&2; exit 2; }

revision=3f1cfffc371d9f678e1034a414f5d83ad934df0f  # ../PS5_Mupen64Plus ps5-gliden64
core_fork_setup
core_fork_checkout PS5_Mupen64Plus "$revision"
core_fork_info mupen64plus_next_libretro.info 8d1fcd13a17310e233be4ff247bf1032f8f786fe685e24cef5ed4ea474ccdfb6

# NASM for the dynarec's linkage (x64/linkage_x64.asm), built for the host.
source "$root/tools/host-nasm.sh"
host_nasm

# LDFLAGS goes in the environment: the makefiles add their own to it.
# GLideN64 needs the PS5 OpenGL SDK headers; the runtime itself lives in the
# frontend and every gl* entry point reaches the core through core imports.
PS5_OPENGL_SDK="${PS5_OPENGL_SDK:-$root/.deps/native/ps5opengl}"
CPLUS_INCLUDE_PATH="$PS5_OPENGL_SDK/include" \
LDFLAGS="$core_ldflags $core_libs" make -C "$source_dir" -j"${JOBS:-16}" platform=ps5 HAVE_GLIDEN64=1 NASM="$nasm" CC="$core_cc" CXX="$core_cxx" AR="$AR" CC_AS="$CC"
core_fork_stage "$source_dir/mupen64plus_next_libretro.so" "$core_info" "$revision" tools/build-mupen64plus.sh tools/host-nasm.sh
