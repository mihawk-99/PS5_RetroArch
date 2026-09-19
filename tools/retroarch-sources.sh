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
    # XMB is the compiled default when enabled; retain RGUI as a menu fallback.
    --enable-menu --enable-xmb --enable-rgui
    --disable-materialui --disable-ozone --disable-gfx_widgets
    # Vulkan is ON. ../PS5_Vulkan is the graphics backend this project consumes
    # (docs/PLAN.md, M2 and the "no direct hardware access" invariant), and the
    # path to it is now measured rather than assumed:
    #
    #  - the frontend builds and links with it enabled (the driver, its loader,
    #    gfx_display_ctx_vulkan, the glslang and SPIRV-Cross include paths, and
    #    the stubs in src/video_filters_stub.cpp);
    #  - RetroArch obtains the entry points by dlopen of "libvulkan.so.1", then
    #    "libvulkan.so" (gfx/common/vulkan_common.c), and this build carries
    #    --enable-dylib and the SDK's dlfcn.h, so a shared object beside the title
    #    is what it looks for;
    #  - RetroArch's Vulkan driver presents through VK_KHR_display - no window
    #    system - which is exactly what ../PS5_Vulkan's driver/ps5vk_wsi.c
    #    implements.
    #
    # What is *not* yet true: that object does not exist on our side, so the
    # loader finds nothing. Previously that failure was silent - "exits 1 within a
    # second of EXEC with no message" - which is why it could not be diagnosed.
    # The title now passes --log-file, so the frontend's own words land in
    # /app0/retroarch.log and the next run says which name it searched for.
    --enable-vulkan --disable-opengl --disable-opengl1 --disable-opengl_core
    --disable-sdl2 --disable-sdl --disable-cg
    # Libraries this SDK does not carry.
    --disable-ffmpeg --disable-freetype --disable-flac --disable-networking
    --disable-cheevos --disable-ssl --disable-cdrom --disable-microphone
    --disable-qt --disable-discord --disable-oss --disable-jack --disable-alsa
    --disable-pulse --disable-pipewire --disable-wayland --disable-x11
    --disable-kms --disable-caca --disable-sixel --disable-bluetooth
    --disable-nvda --disable-sapi --disable-winrawinput --disable-gdi
    --disable-angle --disable-blissbox --disable-xdelta
    # Input and audio back-ends that the console does not have and this SDK has
    # no headers for. Each one is a source that cannot compile here, so each one
    # is switched off at its own configure switch rather than left in the object
    # list to fail: udev needs libudev.h, epoll and evdev (udev also brings in
    # linux_common.c and linuxraw_input.c through the same gate); v4l2 needs
    # linux/videodev2.h for the camera and the video processor; tinyalsa needs
    # linux/ioctl.h. libusb is off because the SDK carries no libusb and nothing
    # here talks to a USB device directly. The console's own pad is reached
    # through the system's input service, which is a driver this project supplies
    # in src/, not one of these.
    --disable-udev --disable-v4l2 --disable-tinyalsa --disable-libusb
    # xkbcommon comes from a host library, not a compiler flag: configure finds
    # this machine's copy through check_val and writes the decision into
    # config.mk, which is what puts keyboard_event_xkb.o in the object list even
    # though the build targets BSD. That object needs xkbcommon/xkbcommon.h, and
    # the console has no X keyboard, so the switch goes off like the rest.
    --disable-xkbcommon
    # CRT mode switching drives a PC monitor's video timings through switchres,
    # a library that is not in RetroArch's tree and that this project does not
    # carry. configure enables it because this machine has a C++11 compiler, and
    # the object it adds then calls sr_* functions nothing defines. A console
    # plugged into a television has no such timings to switch.
    --disable-crtswitchres
    # Built-in copies this build does not use.
    # RetroArch vendors zlib in deps/libz, so baking it in costs nothing and
    # removes the dependency on a system zlib this SDK does not ship.
    --enable-builtinzlib
    --disable-builtinflac --disable-builtinbearssl
    --disable-builtinmbedtls
    # Vulkan needs a GLSL-to-SPIR-V compiler and configure refuses to build the
    # Vulkan driver without one. RetroArch vendors glslang in deps/glslang, so the
    # built-in copy costs a compile rather than a dependency this SDK does not
    # carry.
    --enable-builtinglslang
    --disable-update_cores --disable-update_core_info
    --disable-libretrodb --disable-video_filter --disable-dsp_filter
    # The BSV movie recorder compiles against zlib, which this SDK does not ship.
    --disable-bsv_movie
    # RetroArch's built-in test input driver is on by default, and while it is on
    # `video_driver_init_input` never initialises any real input driver: its first
    # instruction returns early whenever the configured driver is not "test",
    # because it assumes a test driver is already wrapping input. This port's own
    # driver was therefore constructed, registered, never initialised, and never
    # asked for a button - the pad did nothing and nothing said why. It is a
    # development driver for RetroArch's own test suite and has no place in a
    # shipping title in any case.
    --disable-test_drivers
)

if [[ ${1:-} == --config ]]; then
    printf '%s\n' "${configure_flags[@]}"
    exit 0
fi

[[ -d $upstream ]] || { echo "error: run tools/fetch-retroarch.sh first" >&2; exit 2; }
[[ -d $sdk ]] || { echo "error: no SDK at $sdk" >&2; exit 2; }

configure_id=$(printf '%s\n' "${configure_flags[@]}" | sha256sum | cut -d' ' -f1)
if [[ ! -f $work/config.mk || ! -f $work/config.h || ! -f $work/.configure-id || $(<"$work/.configure-id") != "$configure_id" ]]; then
    echo "==> [sources] configuring RetroArch in build/ra-conf (vendor/ is untouched)" >&2
    rm -rf "$work"
    mkdir -p "$(dirname "$work")"
    cp -a "$upstream" "$work"
    rm -rf "$work/.git"
    # The port's changes go in before configure runs, not after: one of them is
    # read by configure itself (qb/config.params.sh declares HAVE_XKBCOMMON so
    # that --disable-xkbcommon below is an option configure accepts). Applying
    # them here and again at compile time is deliberate - this script can be run
    # on its own, and tools/apply-port-patches.py reports `present` when an edit
    # is already in place.
    if [[ -f $root/patches/series ]]; then
        python3 "$root/tools/apply-port-patches.py" "$work" || {
            echo "error: a port change could not be applied to $work" >&2
            exit 2
        }
    fi
    (
        cd "$work"
        export PS5_PAYLOAD_SDK="$sdk"
        export CC="$sdk/bin/prospero-clang" CXX="$sdk/bin/prospero-clang++"
        export OS=BSD DISTRO=
        ./configure "${configure_flags[@]}" >"$work/configure.log" 2>&1
    ) || { echo "error: configure failed; see $work/configure.log" >&2; exit 2; }
    printf '%s\n' "$configure_id" > "$work/.configure-id"
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
    grep -oE '[A-Za-z0-9_./-]+\.[oc]+' "$work/info.log" | sort -u > "$objects"
fi

# obj-unix/release/<source>.o -> <source>.<ext>, with the leading ./ normalised.
#
# The extension is kept rather than assumed. `make info` prints the object's own
# name, and RetroArch's object list is not all C: glslang's and slang's sources are
# C++ and upstream's build compiles them with $(CXX). Rewriting every entry to .c
# turned gfx/drivers_shader/slang_process.cpp into a path that does not exist, so
# the frontend linked with slang_preprocess_parse_parameters undefined - a missing
# C++ source reported as a missing symbol, which is a much longer walk back to the
# cause than a path that says .cpp.
#
# Three of those objects are dropped here, and this is the one place this project
# filters RetroArch's list rather than asking configure for a different one. The
# reason is a substring test upstream cannot win: Makefile.common does
#
#    ifneq ($(findstring Linux,$(OS)),)
#
# and configure is given OS=BSD, which contains "Linux". So the Linux raw input
# driver, its evdev joypad and the shared linux_common.c are always in the object
# list, and all three need headers this SDK does not carry (linux/input.h,
# sys/inotify.h). They are also never reachable in the binary: retroarch.c
# registers both drivers inside `#if defined(__linux__)`, and this toolchain
# defines __FreeBSD__, __PROSPERO__ and __unix__, not __linux__. Dropping them
# changes what is compiled without changing what is linked.
skipped_linux_only='input/drivers/linuxraw_input.c
input/drivers_joypad/linuxraw_joypad.c
input/common/linux_common.c'

sed -e 's|^obj-unix/release/||' -e 's|^\./||' -e 's|\.o$||' "$objects" |
    while IFS= read -r stem; do
        for ext in c cpp cc; do
            [[ -f $upstream/$stem.$ext ]] && { printf '%s.%s\n' "$stem" "$ext"; continue 2; }
        done
        printf '%s.c\n' "$stem"
    done |
    grep -v -x -F "$skipped_linux_only"
