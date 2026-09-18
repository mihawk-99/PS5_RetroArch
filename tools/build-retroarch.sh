#!/usr/bin/env bash
# PS5 RetroArch - compile the RetroArch frontend with the pipeline's toolchain.
#
#   tools/build-retroarch.sh            compile the frontend objects and report
#   tools/build-retroarch.sh --list     print the source list it would build
#
# The source list comes from RetroArch's own build (tools/retroarch-sources.sh),
# which runs configure and `make info` and prints exactly the objects a link
# needs. Nothing here is a previous build's artefact or this project's guess.
#
# A source that does not compile is reported and skipped, not fatal: the point of
# this step is to find the real set the SDK can actually build.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

upstream="$root/vendor/retroarch"
sdk="${PS5_PAYLOAD_SDK:-$root/../ps5-native-app-boilerplate-main/.deps/native/ps5-payload-sdk}"
out="$root/build/ra"
obj="$out/obj"

[[ -d $upstream ]] || { echo "error: run tools/fetch-retroarch.sh first" >&2; exit 2; }
[[ -d $sdk ]] || { echo "error: no SDK at $sdk" >&2; exit 2; }
# RetroArch's sources include the generated header by a relative path
# ("../config.h", "../../config.h", "../../../config.h") depending on how deep the
# file sits, so the one generated file is placed where each of those resolves. This
# is what a normal in-tree build gets for free by having config.h in the tree it
# compiles.
configured="$root/build/ra-conf/config.h"
if [[ ! -f $configured ]]; then
    # One command to build: if the configured tree is not there yet, make it. The
    # ordering (configure before compiling) is a property of RetroArch, not
    # something a caller should have to know.
    echo "==> [ra] no configured tree yet; running tools/retroarch-sources.sh"
    "$root/tools/retroarch-sources.sh" >/dev/null
fi
[[ -f $configured ]] || { echo "error: configure did not produce build/ra-conf/config.h" >&2; exit 2; }
cp -a "$configured" "$root/build/config.h"
cp -a "$configured" "$root/config.h"

# The same feature set the working build used, less the features whose libraries
# this project does not carry. Every one of these is a compile-time gate in
# RetroArch's own sources, so leaving one out removes a file rather than breaking
# the build.
defines=(
    -DRARCH_INTERNAL -DHAVE_CONFIG_H
    -DHAVE_MENU -DHAVE_RGUI -DHAVE_GFX_WIDGETS -DHAVE_OVERLAY -DHAVE_THREADS
    -DHAVE_CONFIGFILE -DHAVE_COMMAND -DHAVE_STDIN_CMD -DHAVE_LANGEXTRA
    -DHAVE_SCREENSHOTS -DHAVE_REWIND -DHAVE_CHEATS -DHAVE_PATCH -DHAVE_RUNAHEAD
    -DHAVE_CC_RESAMPLER -DHAVE_NEAREST_RESAMPLER -DHAVE_DSP_FILTER
    -DHAVE_VIDEO_FILTER -DHAVE_CORE_INFO_CACHE -DHAVE_RPNG -DHAVE_RJPEG
    -DHAVE_RBMP -DHAVE_RTGA -DHAVE_RWAV -DHAVE_IBXM -DHAVE_STB_FONT
    -DHAVE_FILE_LOGGER -DHAVE_ZLIB -DHAVE_THREAD_STORAGE -DHAVE_ACCESSIBILITY
    -DHAVE_IMAGEVIEWER -DHAVE_AUDIOMIXER -DHAVE_BSV_MOVIE -DHAVE_DR_MP3
    -DHAVE_7ZIP -D_7ZIP_ST -DHAVE_TRANSLATE
    # RetroArch's own build passes this, and it is what makes both halves work:
    # it exposes the POSIX clock ids rthreads needs and keeps the BSD strlcpy the
    # frontend declares in its own headers. _POSIX_C_SOURCE alone hid strlcpy and
    # broke 60 more sources than it fixed.
    -D_GNU_SOURCE
    # This SDK's time.h only defines the POSIX clock ids when
    # __POSIX_VISIBLE >= 200112, which the -std=c11 the pipeline uses suppresses.
    # The ids are pre-defined here with the header's own values (CLOCK_REALTIME 0,
    # CLOCK_MONOTONIC 4), so this is the same declaration the header would have
    # made. Both are needed together: the header wraps the whole block in one
    # condition, so defining only the first hides the second.
    -DCLOCK_REALTIME=0 -DCLOCK_MONOTONIC=4
)

includes=(
    # The configured build's own header directory first: configure wrote config.h
    # beside the sources in build/ra-conf, and RetroArch's sources include it by
    # relative path. vendor/retroarch stays untouched.
    -I"$root/build/ra-conf"
    -I"$upstream" -I"$upstream/libretro-common/include" -I"$upstream/deps"
    -I"$upstream/deps/7zip" -I"$upstream/deps/stb" -I"$upstream/deps/ibxm"
    # The vendored zlib's public headers are RetroArch's compatibility copy.
    -I"$upstream/libretro-common/include/compat/zlib"
    -I"$upstream/deps/libz"
    -I"$upstream/libretro-db" -I"$upstream/deps/rcheevos/include"
    -I"$root/src"
)

mapfile -t sources < <("$root/tools/retroarch-sources.sh")

if [[ ${1:-} == --list ]]; then
    printf '%s\n' "${sources[@]}"
    printf '==> [ra] %s sources\n' "${#sources[@]}" >&2
    exit 0
fi

mkdir -p "$obj"
compiled=0
skipped=()
failed=()

for source in "${sources[@]}"; do
    src="$upstream/$source"
    [[ -f $src ]] || { skipped+=("$source (absent in 1.22.2)"); continue; }
    target="$obj/${source//\//_}.o"
    if [[ -f $target && $target -nt $src ]]; then
        compiled=$((compiled + 1)); continue
    fi
    if PS5_CLANG=/usr/bin/clang PS5_PAYLOAD_SDK="$sdk" \
        sh "$root/tooling/prospero-clang18" -std=c11 -O2 -w \
           -ffunction-sections -fdata-sections \
           "${defines[@]}" "${includes[@]}" -c "$src" -o "$target" 2>"$out/last-error.txt"; then
        compiled=$((compiled + 1))
    else
        failed+=("$source: $(grep -m1 'error:' "$out/last-error.txt" | sed 's/^.*error: //')")
    fi
done

echo "==> [ra] compiled $compiled of ${#sources[@]} sources"
if (( ${#skipped[@]} )); then
    printf '==> [ra] %s sources absent in 1.22.2:\n' "${#skipped[@]}"
    printf '    %s\n' "${skipped[@]:0:10}"
fi
if (( ${#failed[@]} )); then
    printf '==> [ra] %s sources did not compile:\n' "${#failed[@]}"
    printf '    %s\n' "${failed[@]:0:15}"
fi
