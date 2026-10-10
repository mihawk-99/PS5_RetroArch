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

# Opt-in diagnostics do not change allocation routing or normal builds.
memory_diagnostics=${PS5_MEMORY_DIAGNOSTICS:-0}
[[ $memory_diagnostics == 0 || $memory_diagnostics == 1 ]] || {
    echo "PS5_MEMORY_DIAGNOSTICS must be 0 or 1" >&2; exit 2;
}
stage=false
case "${1:-}" in
    '') ;;
    --stage) stage=true ;;
    *) echo "usage: ${0##*/} [--stage]" >&2; exit 2 ;;
esac

sdk="$root/.deps/native/ps5-payload-sdk"
# The pinned SDK (my fork; tools/setup-native-dependencies.sh) is installed
# before anything is compiled against it, and the cores' stamps include its
# revision.
bash "$root/tools/setup-native-dependencies.sh" >/dev/null
[[ -x $sdk/bin/prospero-lld ]] || {
    echo "error: no SDK at $sdk; run this project's dependency bootstrap first" >&2
    exit 2
}

core_names=(fceumm mgba snes9x fbneo genesis_plus_gx ppsspp dolphin pcsx2
    mednafen_psx_hw mupen64plus_next mednafen_saturn vice_x64sc desmume azahar mame flycast
    picodrive mednafen_pce mednafen_pcfx mednafen_vb mednafen_ngp mednafen_wswan pokemini
    handy virtualjaguar stella a5200 prosystem dosbox_pure opera puae neocd scummvm)
# RPCS3 is not part of the title by default, console builds included: PS3 is no
# longer a target (2026-10-04), so no deployment puts it on a console.
# tools/build-rpcs3.sh still builds the core on this machine, and PS5_WITH_RPCS3=1
# stages it for someone who builds their own title. A release never carries it
# (GPL-2.0-only beside this port's GPL-3.0 code, docs/RELEASING.md).
with_rpcs3=0
if [[ ${PS5_WITH_RPCS3:-0} == 1 ]]; then
    if [[ -n ${PS5_RELEASE_TAG:-} ]]; then
        echo "error: PS5_WITH_RPCS3=1 with release $PS5_RELEASE_TAG; a release never carries RPCS3" >&2
        exit 2
    fi
    with_rpcs3=1
    core_names+=(rpcs3)
    echo "==> [title] RPCS3 staged (PS5_WITH_RPCS3=1, your own build only)"
fi
# PS5_REUSE_STAGED_CORES="mame ..." (development builds only): stage those cores as
# their last build left them in build/cores/stage instead of building them again.
# For deploying a title change while a long core build (MAME's, over an hour) runs
# elsewhere; the reused core keeps the title's import table covering it. A release
# always builds every core.
reuse_cores=" ${PS5_REUSE_STAGED_CORES:-} "
if [[ -n ${PS5_REUSE_STAGED_CORES:-} && -n ${PS5_RELEASE_TAG:-} ]]; then
    echo "error: PS5_REUSE_STAGED_CORES with release $PS5_RELEASE_TAG; a release builds every core" >&2
    exit 2
fi
echo "==> [title] step 1/3: the frontend"
"$root/tools/build-retroarch.sh"
core_files=()
for core_name in "${core_names[@]}"; do
    # Each library keeps its libretro name; the build script is the port's.
    case $core_name in
        pcsx2) script=lrps2 ;;
        mednafen_psx_hw) script=beetle-psx ;;
        mednafen_saturn) script=beetle-saturn ;;
        mednafen_pce) script=beetle-pce ;;
        mednafen_pcfx) script=beetle-pcfx ;;
        mednafen_vb) script=beetle-vb ;;
        mednafen_ngp) script=beetle-ngp ;;
        mednafen_wswan) script=beetle-wswan ;;
        mupen64plus_next) script=mupen64plus ;;
        vice_x64sc) script=vice ;;
        *) script=${core_name//_/-} ;;
    esac
    if [[ $reuse_cores == *" $core_name "* ]]; then
        for staged in "$root/build/cores/stage/cores/${core_name}_libretro.so" \
                "$root/build/cores/stage/info/${core_name}_libretro.info"; do
            [[ -f $staged ]] || { echo "error: no staged $staged to reuse" >&2; exit 2; }
        done
        echo "==> [title] $core_name: reusing the staged core (PS5_REUSE_STAGED_CORES), not built"
    else
        bash "$root/tools/build-$script.sh"
    fi
    core_files+=("$root/build/cores/stage/cores/${core_name}_libretro.so")
done
python3 "$root/tools/core-imports.py" "${core_files[@]}"

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
memory_wrap_flags=""
if [[ $memory_diagnostics == 1 ]]; then
    title_definition_names+=(PS5_MEMORY_DIAGNOSTICS)
    # Mesa's default Vulkan host allocator uses posix_memalign, not malloc.
    memory_wrap_flags="--wrap=posix_memalign"
fi
echo "==> [title] compiling src/ with ${#title_definition_names[@]} frontend defines"

# RetroArch's headers reach their generated config as "../../config.h", a relative
# path that resolves to <tree>/config.h because the frontend is compiled with the
# configured tree as the working directory. src/ is compiled from the repository
# root, so that same include looks for build/config.h. Without the defines it never
# got that far - the include is inside HAVE_OVERLAY's block. The copy is written
# from the configured tree's own config.h rather than kept by hand, so the two
# cannot disagree about what this build is.
cp -f -- "$root/build/ra-conf/config.h" "$root/build/config.h"

# The Vulkan driver is linked, not loaded. ../PS5_Vulkan measured that a PS5 title
# cannot dlopen a driver (sceKernelLoadStartModule refuses a linker-produced .so
# with ENOEXEC, a bare name gives ENOENT, dlopen answers NULL for every candidate
# and sceKernelDlsym gives ESRCH even for modules the process holds), so RetroArch's
# dlopen of "libvulkan.so.1" can never succeed here. The route proven on this
# console is their runner title's: link the driver and call its entry point as an
# ordinary symbol.
#
# The driver is RADV, Mesa's Vulkan driver, from ../PS5_Vulkan's port (its route
# B, docs/VULKAN_1_4_PLAN.md there): the release archive its tools/build-radv.sh
# release builds, or the one RADV_ARCHIVE names, linked by that project's
# tools/radv-link.sh, whose platform bindings this title takes but for the heap:
# the title's allocator (src/memory_ps5.cpp) stays, as the cores' imports are
# bound to its routes. src/locale_shims.c steps aside for the platform layer's
# locale functions. Releases since v0.5.0-alpha.5 ship it.
#
# PS5_VULKAN_DRIVER=ps5vk links ps5vk, the project's first driver, which the
# releases up to v0.4.0-alpha.4 shipped - its released set, exactly as
# tools/build.sh links it for a driver-enabled title:
#   libps5vk.ps5.a        the driver            (build/driver/ps5/)
#   libvk_runtime.ps5.a   Mesa's Vulkan runtime (.deps/native/vulkan-runtime/lib/)
#   libpsbc_driver.ps5.a  the shader compiler   (build/driver/ps5/)
#   libpsbc_support.ps5.a the package writer    (.deps/native/psbc/lib/)
# PS5_VULKAN_DIR overrides the sibling's root, so a release kept elsewhere works.
vulkan_dir="${PS5_VULKAN_DIR:-$root/../PS5_Vulkan}"
vulkan_driver=${PS5_VULKAN_DRIVER:-radv}
case $vulkan_driver in
    ps5vk | radv) ;;
    *) echo "PS5_VULKAN_DRIVER must be ps5vk or radv" >&2; exit 2 ;;
esac
[[ $vulkan_driver == ps5vk ]] || title_definition_names+=(PS5_RETROARCH_RADV)
vulkan_archives=(
    "$vulkan_dir/build/driver/ps5/libps5vk.ps5.a"
    "$vulkan_dir/.deps/native/vulkan-runtime/lib/libvk_runtime.ps5.a"
    "$vulkan_dir/build/driver/ps5/libpsbc_driver.ps5.a"
    "$vulkan_dir/.deps/native/psbc/lib/libpsbc_support.ps5.a"
)
vulkan_missing=()
for archive in "${vulkan_archives[@]}"; do
    [[ -f $archive ]] || vulkan_missing+=("$archive")
done
if (( ${#vulkan_missing[@]} )); then
    printf 'error: the Vulkan driver archives are missing; the title would link with\n' >&2
    printf '       vkGetInstanceProcAddr unresolved. Build them in ../PS5_Vulkan\n' >&2
    printf '       (tools/build-driver.sh) or set PS5_VULKAN_DIR.\n' >&2
    printf '       missing: %s\n' "${vulkan_missing[@]}" >&2
    exit 2
fi
# The driver may be developed concurrently. A diagnostic link uses stable local
# archive copies; hashes describe exactly which driver went into this build.
if [[ $memory_diagnostics == 1 ]]; then
    if ! snapshot_list=$(python3 - "$root" "${vulkan_archives[@]}" <<'PY_SNAPSHOT'
import hashlib, json, pathlib, shutil, sys
out = pathlib.Path(sys.argv[1]) / "build/memory-diagnostic-inputs"
out.mkdir(parents=True, exist_ok=True)
records = {}
for argument in sys.argv[2:]:
    source = pathlib.Path(argument)
    before = hashlib.sha256(source.read_bytes()).hexdigest()
    target = out / source.name
    shutil.copyfile(source, target)
    copied = hashlib.sha256(target.read_bytes()).hexdigest()
    after = hashlib.sha256(source.read_bytes()).hexdigest()
    if before != copied or before != after:
        raise SystemExit("Driver archive changed during snapshot; retry when its build finishes")
    records[source.name] = copied
    print(target)
(out / "archives.json").write_text(json.dumps(records, indent=2) + "\n")
PY_SNAPSHOT
    ); then
        echo "error: driver snapshot failed" >&2; exit 2
    fi
    mapfile -t vulkan_archives <<< "$snapshot_list"
fi

# Mesa's weak entry points resolve at link time, and the driver's own symbols must
# survive the archive boundary (--whole-archive), which is how the sibling links it.
vulkan_flags="--no-dynamic-linker -z nodynamic-undefined-weak"
linker_script=""
if [[ $vulkan_driver == radv ]]; then
    radv_archive=${RADV_ARCHIVE:-$vulkan_dir/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a}
    # shellcheck source=/dev/null
    source "$vulkan_dir/tools/radv-link.sh"
    radv_link_recipe "$vulkan_dir" "$sdk" "$radv_archive" || exit 2
    vulkan_archives=("$radv_archive")
    for flag in "${radv_link_flags[@]}"; do
        case $flag in
            --wrap=malloc | --wrap=calloc | --wrap=realloc | --wrap=free | --wrap=posix_memalign | \
            --wrap=aligned_alloc | --wrap=memalign | --wrap=malloc_usable_size | --wrap=reallocf | \
            --wrap=reallocarray | --wrap=getline | --wrap=getdelim) ;;
            *) vulkan_flags+=" $flag" ;;
        esac
    done
    # The C++ runtime, the compiler's builtins and the platform layer.
    vulkan_flags+=" ${radv_link_inputs[*]:5}"
    linker_script="$vulkan_dir/tooling/psbc/ps5-pie-unwind.ld"
fi

# Three Mesa utility sources the archives above reference but do not carry:
# ../PS5_Vulkan's PS5 object list filters u_thread.c, anon_file.c and os_file.c
# out, and its own libvulkan.so.1 only links because a shared object may leave
# symbols undefined. A title may not, so they are compiled here from that
# project's sources with its PS5 configuration and linked as plain objects.
# tools/build-mesa-util.sh says which symbols each one is for. It prints the
# object paths on stdout, so a compile failure has to be caught here: a process
# substitution would let the link fail later on symbols this step was to supply.
if [[ $vulkan_driver == radv ]]; then
    # RADV's archive carries Mesa's utilities whole.
    vulkan_objects=()
elif ! vulkan_object_list=$(PS5_VULKAN_DIR="$vulkan_dir" PS5_PAYLOAD_SDK="$sdk" \
        PS5_CLANG=/usr/bin/clang bash "$root/tools/build-mesa-util.sh"); then
    echo "error: the driver's Mesa utility objects did not build" >&2
    exit 2
else
    mapfile -t vulkan_objects <<< "$vulkan_object_list"
fi

# HTTP parser shared with the websrv reference; bounded streaming avoids whole-game buffers.
webui_http=$(bash "$root/tools/build-webui-http.sh" ps5)
webui_http=${webui_http#"$root/"}
webui_update=$(bash "$root/tools/build-webui-update.sh" ps5)
webui_update=${webui_update#"$root/"}
# The WebUI's own process, which stays up across the title's frontend changes
# (src/webui_link.h): built from the same server and updater, staged in webui/.
webui_daemon=$(bash "$root/tools/build-webui-daemon.sh")

# Extract once, before the identity is computed, so catalog changes identify the build.
python3 "$root/tools/generate-core-metadata.py" "$root/build/webui-core-metadata"
# Every shipped core has its guided Settings catalog (rpcs3 aside: never released). A
# core added without one would show no options in the WebUI, as 18 did until 2026-10-09.
for core_name in "${core_names[@]}"; do
    [[ $core_name == rpcs3 ]] && continue
    grep -q "^${core_name}_libretro.so = " "$root/build/webui-core-metadata/index.cfg" ||
        { echo "error: $core_name has no WebUI Settings catalog (tools/generate-core-metadata.py CORES)" >&2; exit 1; }
done

# Bind the running trace and FTP readback to these exact source/archive inputs.
# The console transforms the SELF container, so its whole-file digest differs.
CORE_NAMES="${core_names[*]}" python3 - "$root" "$memory_diagnostics" "${vulkan_archives[@]}" "${vulkan_objects[@]}" <<'PY'
import hashlib, os, pathlib, sys
root = pathlib.Path(sys.argv[1])
inputs = sorted(p for p in (root / "src").rglob("*") if p.is_file())
inputs += [root / name for name in (
    "build/ra/libretroarch.a", "build/ra-conf/config.h", "tools/build-title.sh",
    "build/core_imports.inc", "tools/build-webui-http.sh", "tools/build-webui-update.sh",
    "build/webui-update-ps5/libupdate.a",
    "build/webui-mhd-ps5/src/microhttpd/.libs/libmicrohttpd.a",
    # The SDK fork's revision: its platform layer is linked into the title, and
    # a change there alone changes no other input.
    ".deps/native/ps5-payload-sdk/.ps5-sdk-revision",
    *(f"build/cores/stage/cores/{name}_libretro.so" for name in os.environ["CORE_NAMES"].split()),
    "tools/build.sh", "tools/retroarch-flags.sh")]
inputs += sorted(p for p in (root / "third_party/qrcodegen").rglob("*") if p.is_file())
inputs += sorted(p for p in (root / "webui").rglob("*") if p.is_file())
inputs += sorted(p for p in (root / "daemon").rglob("*") if p.is_file())
inputs.append(root / "tools/build-webui-daemon.sh")
inputs += sorted(p for p in (root / "build/webui-core-metadata").rglob("*") if p.is_file())
inputs += [pathlib.Path(name) for name in sys.argv[3:]]
digest = hashlib.sha256()
digest.update(b"memory-diagnostics=" + sys.argv[2].encode() + b"\0")
for path in inputs:
    digest.update(path.name.encode() + b"\0")
    digest.update(hashlib.sha256(path.read_bytes()).digest())
identity = digest.hexdigest()
(root / "build/title_build_identity.h").write_text(
    '#define PS5_RETROARCH_BUILD_ID "build identity: ' + identity + '"\n')
print("==> [title] build identity: " + identity)
PY

# The title's libc++ (std::filesystem) calls libc's opendir, which the console
# refuses, and openat/fdopendir/unlinkat/fchmodat, which its libkernel does not
# export; the platform layer implements all of them (libps5platform.a, from my
# payload SDK fork), and src/platform_wraps.c binds these links to it. A core's
# own imports of the same calls are bound to it by tools/core-imports.py.
directory_wrap_flags="--wrap=opendir --wrap=readdir --wrap=closedir --wrap=fdopendir --wrap=openat --wrap=unlinkat --wrap=fchmodat"
# realpath is refused to a title, so std::filesystem::canonical and
# weakly_canonical (RPCS3's package installer) came back empty.
directory_wrap_flags+=" --wrap=realpath"
# libc's getcwd resolves to nothing in a title (it calls __getcwd, which only
# libkernel_sys exports); std::filesystem::current_path is built on it.
directory_wrap_flags+=" --wrap=getcwd"
# RetroArch's networking resolves names through libretro-common's getaddrinfo_retro, which
# would reach a getaddrinfo that refuses every lookup, and formats addresses through
# getnameinfo_retro, whose getnameinfo no module exports: src/net_shims.c replaces the
# three, asking the console's DNS resolver (libSceNet) and formatting numerically.
directory_wrap_flags+=" --wrap=getaddrinfo_retro --wrap=freeaddrinfo_retro --wrap=getnameinfo_retro --wrap=getnameinfo"
# HTTPS seeds mbedTLS from /dev/urandom; src/net_shims.c gives it the libc arc4random_buf.
directory_wrap_flags+=" --wrap=mbedtls_platform_entropy_poll"
# Diagnostics of the HTTPS steps in retroarch.log (src/net_shims.c).
directory_wrap_flags+=" --wrap=ssl_socket_init --wrap=ssl_socket_connect --wrap=socket_connect_with_timeout --wrap=mbedtls_ssl_handshake"
# Folders the title or a core creates are 0777 and files at least 0666, so FTP,
# which runs as another user, can reach them (src/permissions_ps5.cpp).
directory_wrap_flags+=" $(bash "$root/tools/path-wrap-flags.sh")"
# No module a title loads exports these: each import was null at run time, and
# RetroArch's menu search (strcasestr) jumped to address 0 from Manual Scan's
# Content Directory (src/platform_wraps.c). tools/build.sh refuses the title
# should one be imported again.
directory_wrap_flags+=" --wrap=strcasestr --wrap=mkstemp --wrap=link --wrap=symlink --wrap=readlink --wrap=pathconf"
echo "==> [title] step 2/3: the title"
# Large frontend/core buffers use mapped memory; wrap all ownership operations.
PS5_PAYLOAD_SDK="$sdk" \
PS5_CLANG=/usr/bin/clang \
PYTHONPATH="$root/tooling/pystub${PYTHONPATH:+:$PYTHONPATH}" \
APP_DEFINITIONS="${title_definition_names[*]}" \
APP_INCLUDE_PATHS="third_party build/ra-conf build vendor/retroarch build/ra-conf/libretro-common/include vendor/retroarch/deps vendor/retroarch/deps/stb .deps/webui/libmicrohttpd-1.0.10/src/include .deps/native/zlib/zlib-1.3.2 .deps/native/zlib/zlib-1.3.2/contrib/minizip vendor/retroarch/deps/mbedtls" \
APP_STATIC_ARCHIVES="build/ra/libretroarch.a $webui_http $webui_update" \
APP_SDK_ARCHIVES="libps5platform.a" \
APP_VULKAN_ARCHIVES="${vulkan_archives[*]}" \
APP_EXTRA_OBJECTS="${vulkan_objects[*]}" \
APP_LINK_FLAGS="$vulkan_flags --wrap=malloc --wrap=calloc --wrap=realloc --wrap=free $memory_wrap_flags $directory_wrap_flags" \
APP_LINKER_SCRIPT="$linker_script" \
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
# The legal notice (no piracy; RPCS3 only built from source) at the top of the
# title folder, where a person unpacking a release sees it first.
cp -a -- "$root/config/LEGAL.txt" "$dist/LEGAL.txt"
mkdir -p "$dist/cores" "$dist/info"
for core_name in "${core_names[@]}"; do
    cp -- "$root/build/cores/stage/cores/${core_name}_libretro.so" "$dist/cores/"
    cp -- "$root/build/cores/stage/info/${core_name}_libretro.info" "$dist/info/"
    # Older saved configs have an empty info path: upstream then searches cores/.
    cp -- "$root/build/cores/stage/info/${core_name}_libretro.info" "$dist/cores/"
done

# A folder in content/ for each system the staged cores run, named as RetroArch names
# it ("Nintendo - Nintendo Entertainment System"), so a fresh install has a place for
# every system's games (issue 33). The title makes the same ones at each start
# (src/ps5_library.c, ps5_library_make_system_folders): this is that code, on the host.
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$root/src" "$root/tools/system-folders.c" \
    "$root/src/ps5_library.c" -o "$root/build/system-folders"
rm -rf -- "$dist/content"
printf '==> [title] staged %s system folders in %s/content\n' \
    "$("$root/build/system-folders" "$dist")" "$dist"

# PPSSPP resolves its assets as <system>/PPSSPP, and RetroArch's system directory in
# this title is /app0/system. Without the tree a game boots with "Core system files
# missing, expect bugs": no flash0 fonts, no language files, no shaders. Only the
# cores that stage it contribute, so a build without PPSSPP is unchanged.
if [[ -d $root/build/cores/stage/system/PPSSPP ]]; then
    mkdir -p "$dist/system"
    rm -rf -- "$dist/system/PPSSPP"
    cp -a -- "$root/build/cores/stage/system/PPSSPP" "$dist/system/PPSSPP"
    printf '==> [title] staged PPSSPP assets: %s files in %s/system/PPSSPP\n' \
        "$(find "$dist/system/PPSSPP" -type f | wc -l)" "$dist"
fi
# Dolphin resolves its Sys tree as <system>/dolphin-emu/Sys (game settings, shader
# sources, fonts, title databases); its User tree lives in the save directory.
if [[ -d $root/build/cores/stage/system/dolphin-emu ]]; then
    mkdir -p "$dist/system"
    rm -rf -- "$dist/system/dolphin-emu"
    cp -a -- "$root/build/cores/stage/system/dolphin-emu" "$dist/system/dolphin-emu"
    printf '==> [title] staged Dolphin Sys: %s files in %s/system/dolphin-emu\n' \
        "$(find "$dist/system/dolphin-emu" -type f | wc -l)" "$dist"
fi

# RPCS3's folder is <system>/RPCS3, which also holds what RPCS3 writes there on
# the console (the firmware, dev_hdd0, its configuration): only the core's own
# files are staged, the loading screen's fonts, RPCS3's overlay images, its
# game patch database and its per-game configuration database.
for part in fonts Icons patches game_configs; do
    if (( with_rpcs3 )) && [[ -d $root/build/cores/stage/system/RPCS3/$part ]]; then
        mkdir -p "$dist/system/RPCS3"
        rm -rf -- "${dist:?}/system/RPCS3/$part"
        cp -a -- "$root/build/cores/stage/system/RPCS3/$part" "$dist/system/RPCS3/$part"
    fi
done

# ps5vk's shared object, beside a ps5vk title, when it exists.
#
# Both drivers are linked into eboot.bin (above), and nothing loads this file: a
# title cannot dlopen a driver. ps5vk builds keep staging it as the releases up
# to v0.4.0-alpha.4 did; a RADV build ships without it, since it would be ps5vk's
# code in a RADV title. PS5_VULKAN_ICD overrides the path.
icd="${PS5_VULKAN_ICD:-$root/../PS5_Vulkan/build/driver/ps5/libvulkan.so.1}"
if [[ $vulkan_driver == radv ]]; then
    echo "==> [title] RADV is linked into eboot.bin; no driver library is staged"
elif [[ -f $icd ]]; then
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

# Ship local assets and honest release identity; development builds have no release tag.
mkdir -p "$dist/webui"
cp -a "$root/webui/." "$dist/webui/"
cp "$webui_daemon" "$dist/webui/ps5-retroarch-webui.elf"
# The certificates the daemon verifies HTTPS against (curl over mbedTLS): Mozilla's
# bundle as EmulationStation ships it, from its pinned source.
certificates="$root/.deps/es-de/resources/certificates/curl-ca-bundle.crt"
[[ -f $certificates ]] || { echo "error: $certificates is missing (tools/build-esde.sh fetches it)" >&2; exit 1; }
cp "$certificates" "$dist/webui/ca-bundle.crt"
# Core option catalogs are available before the first game is opened.
mkdir -p "$dist/webui/core-metadata"
cp -a "$root/build/webui-core-metadata/." "$dist/webui/core-metadata/"
python3 - "$dist/webui/version.json" "${PS5_RELEASE_TAG:-}" "$root/build/title_build_identity.h" <<'PY_WEBUI'
import json, pathlib, re, sys
identity = re.search(r"build identity: ([a-f0-9]+)", pathlib.Path(sys.argv[3]).read_text())[1]
pathlib.Path(sys.argv[1]).write_text(json.dumps({"release": sys.argv[2], "build": identity,
    "repository": "mihawk-99/PS5_RetroArch"}) + "\n")
PY_WEBUI

# Pinned production effects, with dependency and development-fixture checks.
python3 "$root/tools/video-assets.py" stage "$dist"

# Frontend executables beside eboot.bin (frontends/, tools/build-frontend.sh), those
# PS5_FRONTENDS names: they need ../PS5_OpenGL's SDK and its SDL2 build, and
# ../PS5_VulkanTemplate for the picker. A development build carries none unless asked;
# a release carries both (their notices and sources are listed with the rest:
# tooling/notices/components.json, docs/RELEASING.md), unless PS5_FRONTENDS is set,
# empty included. Built before the manifest so it covers them.
if [[ -n ${PS5_RELEASE_TAG:-} && -z ${PS5_FRONTENDS+set} ]]; then
    PS5_FRONTENDS="es-de picker"
fi
if [[ -n ${PS5_FRONTENDS:-} ]]; then
    read -r -a frontends <<< "$PS5_FRONTENDS"
    for frontend in "${frontends[@]}"; do
        # The picker is a program of ../PS5_VulkanTemplate's UI module, built its way.
        if [[ $frontend == picker ]]; then
            bash "$root/tools/build-picker.sh"
        else
            bash "$root/tools/build-frontend.sh" "$frontend"
        fi
    done
fi

# The licences and notices the parts of this folder require, and the source revision
# of each (tooling/notices/components.json, docs/RELEASING.md), written before the
# manifest so the manifest covers them. It fails if a staged core is not the file its
# build report describes.
python3 "$root/tools/stage-notices.py" "$dist" --driver "$vulkan_driver" \
    --vulkan-dir "$vulkan_dir" --release-tag "${PS5_RELEASE_TAG:-}"

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

python3 "$root/tools/video-assets.py" package "$dist" --output "$root/dist/PS5_RetroArch.zip"

if $stage; then
    out="$root/handoff/$title_id"
    rm -rf -- "$out"
    mkdir -p -- "$root/handoff"
    cp -a -- "$dist" "$out"
    printf '==> [title] step 3/3: staged %s\n' "$out"
else
    echo "==> [title] step 3/3: not staging (pass --stage to copy to handoff/)"
fi
