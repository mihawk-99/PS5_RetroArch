#!/usr/bin/env bash
# PS5 RetroArch - ask RetroArch's own build which sources a frontend needs.
#
#   tools/retroarch-sources.sh            print the source list, one per line
#   tools/retroarch-sources.sh --config   print the configure flags it used
#
# This exists so the build has one source of truth that is not this project's
# guess and not a previous build's artefact. RetroArch's `Makefile` grows its
# OBJ list from 248 conditional `OBJ +=` lines in Makefile.common, so the list
# for a given configuration can only be produced by make itself:
#
#   ./configure <flags> && make info
#
# Its `info` target prints RARCH_OBJ, which is exactly the objects a link needs.
# configure writes config.h and config.mk into the tree it runs in, so it runs in
# a copy under build/ and vendor/retroarch is never touched.
#
# The flags below are this project's: the console's frame comes from the display
# layer, RGUI is the menu, and everything that needs a library this SDK does not
# ship is off. They are passed through configure rather than edited into a
# Makefile, so upgrading RetroArch means changing a version, not a patch.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

upstream="$root/vendor/retroarch"
work="$root/build/ra-conf"
sdk="${PS5_PAYLOAD_SDK:-$root/../ps5-native-app-boilerplate-main/.deps/native/ps5-payload-sdk}"

configure_flags=(
    --prefix=/user/homebrew
    # The menu, and only the menu that needs no GPU display context.
    --enable-menu --enable-rgui
    --disable-materialui --disable-xmb --disable-ozone --disable-gfx_widgets
    # No graphics API: the frame is presented by src/display.cpp.
    --disable-vulkan --disable-opengl --disable-opengl1 --disable-opengl_core
    --disable-sdl2 --disable-sdl --disable-cg
    # Libraries this SDK does not carry.
    --disable-ffmpeg --disable-freetype --disable-flac --disable-networking
    --disable-cheevos --disable-ssl --disable-cdrom --disable-microphone
    --disable-qt --disable-discord --disable-oss --disable-jack --disable-alsa
    --disable-pulse --disable-pipewire --disable-wayland --disable-x11
    --disable-kms --disable-caca --disable-sixel --disable-bluetooth
    --disable-nvda --disable-sapi --disable-winrawinput --disable-gdi
    --disable-angle --disable-blissbox --disable-xdelta
    # Built-in copies this build does not use.
    # RetroArch vendors zlib in deps/libz, so baking it in costs nothing and
    # removes the dependency on a system zlib this SDK does not ship.
    --enable-builtinzlib
    --disable-builtinflac --disable-builtinbearssl
    --disable-builtinmbedtls --disable-builtinglslang
    --disable-update_cores --disable-update_core_info
    --disable-libretrodb --disable-video_filter --disable-dsp_filter
    # The BSV movie recorder compiles against zlib, which this SDK does not ship.
    --disable-bsv_movie
)

if [[ ${1:-} == --config ]]; then
    printf '%s\n' "${configure_flags[@]}"
    exit 0
fi

[[ -d $upstream ]] || { echo "error: run tools/fetch-retroarch.sh first" >&2; exit 2; }
[[ -d $sdk ]] || { echo "error: no SDK at $sdk" >&2; exit 2; }

if [[ ! -f $work/config.mk || ! -f $work/config.h ]]; then
    echo "==> [sources] configuring RetroArch in build/ra-conf (vendor/ is untouched)" >&2
    rm -rf "$work"
    mkdir -p "$(dirname "$work")"
    cp -a "$upstream" "$work"
    rm -rf "$work/.git"
    (
        cd "$work"
        export PS5_PAYLOAD_SDK="$sdk"
        export CC="$sdk/bin/prospero-clang" CXX="$sdk/bin/prospero-clang++"
        export OS=BSD DISTRO=
        ./configure "${configure_flags[@]}" >"$work/configure.log" 2>&1
    ) || { echo "error: configure failed; see $work/configure.log" >&2; exit 2; }
fi

objects="$work/.rarch-obj"
if [[ ! -s $objects ]]; then
    (
        cd "$work"
        export PS5_PAYLOAD_SDK="$sdk"
        export CC="$sdk/bin/prospero-clang" CXX="$sdk/bin/prospero-clang++"
        export OS=BSD DISTRO=
        make info >"$work/info.log" 2>&1
    ) || { echo "error: 'make info' failed; see $work/info.log" >&2; exit 2; }
    grep -oE '[A-Za-z0-9_./-]+\.o' "$work/info.log" | sort -u > "$objects"
fi

# obj-unix/release/<source>.o -> <source>.c, with the leading ./ normalised.
sed -e 's|^obj-unix/release/||' -e 's|^\./||' -e 's|\.o$|.c|' "$objects"
