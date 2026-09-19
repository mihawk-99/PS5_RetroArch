#!/usr/bin/env bash
# PS5 RetroArch - build the title.
#
#   tools/build-title.sh            compile the frontend, then link and sign it
#   tools/build-title.sh --stage    also copy the result to handoff/<TITLE_ID>/
#
# The whole build is three steps that depend on each other in one direction:
#
#   1. tools/build-retroarch.sh   compiles RetroArch's own sources into
#                                 build/ra/libretroarch.a
#   2. make app                   compiles src/, links that archive with the
#                                 pipeline's CRT, signs the result and assembles
#                                 the title folder under dist/<TITLE_ID>/
#   3. handoff                    a copy of that folder for the console's owner
#
# Step 2 is the project's own Makefile, not a reimplementation of it. What this
# script adds is the four things the Makefile cannot know, each of which was found
# by a failed build and is why the environment is set here rather than typed:
#
#   PS5_PAYLOAD_SDK   this project's vendored SDK, not the one in $HOME and not a
#                     sibling's: the wrapper's default and the donor's differ
#   PS5_CLANG         the toolchain's wrapper defaults to clang-18, which is not
#                     installed; plain clang is what this machine builds with
#   PYTHONPATH        mbedTLS regenerates a source file by running a script that
#                     imports jsonschema; tooling/pystub supplies it
#   APP_INCLUDE_PATHS src/ includes RetroArch's headers, and those come from the
#                     configured copy under build/ so the driver is declared
#   APP_STATIC_ARCHIVES the frontend archive from step 1
#
# Signing happens inside step 2 and is not optional: the console loads a fake
# self, not an ELF, and an unsigned eboot.bin is a title that fails to start with
# no message of its own.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

stage=false
case "${1:-}" in
    '') ;;
    --stage) stage=true ;;
    *) echo "usage: ${0##*/} [--stage]" >&2; exit 2 ;;
esac

sdk="$root/.deps/native/ps5-payload-sdk"
[[ -x $sdk/bin/prospero-lld ]] || {
    echo "error: no SDK at $sdk; run this project's dependency bootstrap first" >&2
    exit 2
}

echo "==> [title] step 1/3: the frontend"
"$root/tools/build-retroarch.sh"

# The title's own sources are compiled with the same feature defines as the
# frontend, because the two share a header full of #ifdefs and a struct whose
# member order those #ifdefs decide. This is not a nicety; it was a bug with no
# symptom except a menu that never appeared. Compiled without these, src/'s copy of
# RetroArch's headers had HAVE_OVERLAY and HAVE_GFX_WIDGETS off, so video_ps5 - the
# driver table this project hands the frontend - was laid out 8 bytes shorter than
# the frontend's own view of the same struct. Everything the frontend reads after
# overlay_interface was therefore the member before it: poke_interface and
# wrap_type_to_enum both read as NULL. The driver still opened the display and
# presented 1500 frames, alive() was still the right function by luck, and the only
# consequence was that RGUI - which renders the menu into its own 320x240
# framebuffer and hands it over through poke->set_texture_frame - had nowhere to
# hand it. The frontend calls poke_interface only when it is not NULL, so the
# hand-over died in silence.
#
# The list is not written here: tools/retroarch-flags.sh reads it from the command
# `make` itself would run, tools/build-retroarch.sh compiles the archive with it,
# and this passes the same list to the title. Defining a feature the archive does
# not compile, or omitting one it does, reintroduces exactly this class of fault.
mapfile -t title_defines < <("$root/tools/retroarch-flags.sh" | tr ' ' '\n' | grep -E '^-D' || true)
(( ${#title_defines[@]} > 0 )) || { echo "error: no compile flags from tools/retroarch-flags.sh" >&2; exit 2; }
# tools/build.sh takes the names without the -D and validates each one, so the
# path-valued flags (quoted string literals) cannot go through it; the paths this
# title uses are passed to that build separately and point at /app0.
title_definition_names=()
for define in "${title_defines[@]}"; do
    [[ $define == -D*_DIR=* ]] && continue
    title_definition_names+=("${define#-D}")
done
(( ${#title_definition_names[@]} > 0 )) || { echo "error: no feature defines to pass" >&2; exit 2; }
echo "==> [title] compiling src/ with ${#title_definition_names[@]} frontend defines"

# RetroArch's headers reach their generated config as "../../config.h", a relative
# path that resolves to <tree>/config.h because the frontend is compiled with the
# configured tree as the working directory. src/ is compiled from the repository
# root, so that same include looks for build/config.h. Without the defines it never
# got that far - the include is inside HAVE_OVERLAY's block. The copy is written
# from the configured tree's own config.h rather than kept by hand, so the two
# cannot disagree about what this build is.
cp -f -- "$root/build/ra-conf/config.h" "$root/build/config.h"

echo "==> [title] step 2/3: the title"
PS5_PAYLOAD_SDK="$sdk" \
PS5_CLANG=/usr/bin/clang \
PYTHONPATH="$root/tooling/pystub${PYTHONPATH:+:$PYTHONPATH}" \
APP_DEFINITIONS="${title_definition_names[*]}" \
APP_INCLUDE_PATHS="build/ra-conf build vendor/retroarch vendor/retroarch/libretro-common/include vendor/retroarch/deps vendor/retroarch/deps/stb" \
APP_STATIC_ARCHIVES="build/ra/libretroarch.a" \
    make app

title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' \
    "$root/sce_sys/param.json")
dist="$root/dist/$title_id"
[[ -f $dist/eboot.bin ]] || { echo "error: no eboot.bin under $dist" >&2; exit 2; }

# The configuration seed goes into the title folder, after tools/build.sh has
# assembled it - the app folder is recreated on every build, so a copy made
# earlier is removed with the rest. This file is the only place the video driver is
# chosen: the safe one is named until ../PS5_Vulkan's libvulkan.so.1 is beside the
# title, because naming a driver whose library is missing makes RetroArch fail to
# initialise and the title exit 1 saying nothing. See config/retroarch.cfg.
cp -a -- "$root/config/retroarch.cfg" "$dist/retroarch.cfg"

# The Vulkan driver, beside the title, when it exists.
#
# RetroArch does not link Vulkan: it dlopens "libvulkan.so.1" at run time
# (gfx/common/vulkan_common.c), so the driver has to be a shared object in the
# title's own folder. That object is ../PS5_Vulkan's released artifact - this
# project consumes released drivers and never builds them (docs/PLAN.md, "What is
# deliberately not planned") - and its own build puts it at
# build/driver/ps5/libvulkan.so.1.
#
# It is copied only when it is there, and its absence is reported rather than
# fatal: the driver is a separate project's release, and a title built without it
# still runs (the video driver's compiled default is Vulkan, so it will report a
# failed load in /app0/retroarch.log rather than exit silently). PS5_VULKAN_ICD
# overrides the path for a release kept somewhere else.
icd="${PS5_VULKAN_ICD:-$root/../PS5_Vulkan/build/driver/ps5/libvulkan.so.1}"
if [[ -f $icd ]]; then
    cp -a -- "$icd" "$dist/libvulkan.so.1"
    # Beside libc.prx as well, which is the one module path the console's loader
    # is known to look at: libc.prx is resolved from sce_module/ by every title
    # here. A bare dlopen does not search the app directory, and whether it
    # accepts an absolute /app0 path is not yet measured, so the driver goes
    # where the loader provably looks.
    mkdir -p "$dist/sce_module"
    cp -a -- "$icd" "$dist/sce_module/libvulkan.so.1"
    printf '==> [title] staged the Vulkan driver: %s (%s bytes)\n' \
        "$(basename "$icd")" "$(stat -c %s "$dist/libvulkan.so.1")"
else
    printf '==> [title] no Vulkan driver at %s; the title will report a failed load\n' \
        "$icd" >&2
fi

# The manifest is recorded here, as part of building, because a folder published
# without one cannot be told apart from the folder published last week: this
# project has already produced a title folder whose eboot.bin was a raw link-stage
# ELF and whose libc.prx was a different build from the one in the tree, with
# every file present and every size plausible. Recording it on every build means
# the digests describe the bytes that exist now, and tools/check-manifest.sh can
# then verify the copy that reaches the console.
bash "$root/tools/check-manifest.sh" --record

printf '==> [title] built %s (%s files, eboot.bin %s bytes)\n' \
    "$dist" "$(find "$dist" -type f | wc -l)" "$(stat -c %s "$dist/eboot.bin")"

if $stage; then
    out="$root/handoff/$title_id"
    rm -rf -- "$out"
    mkdir -p -- "$root/handoff"
    cp -a -- "$dist" "$out"
    printf '==> [title] step 3/3: staged %s\n' "$out"
else
    echo "==> [title] step 3/3: not staging (pass --stage to copy to handoff/)"
fi
