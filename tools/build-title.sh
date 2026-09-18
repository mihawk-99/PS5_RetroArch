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

echo "==> [title] step 2/3: the title"
PS5_PAYLOAD_SDK="$sdk" \
PS5_CLANG=/usr/bin/clang \
PYTHONPATH="$root/tooling/pystub${PYTHONPATH:+:$PYTHONPATH}" \
APP_INCLUDE_PATHS="build/ra-conf vendor/retroarch vendor/retroarch/libretro-common/include vendor/retroarch/deps vendor/retroarch/deps/stb" \
APP_STATIC_ARCHIVES="build/ra/libretroarch.a" \
    make app

title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' \
    "$root/sce_sys/param.json")
dist="$root/dist/$title_id"
[[ -f $dist/eboot.bin ]] || { echo "error: no eboot.bin under $dist" >&2; exit 2; }

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
