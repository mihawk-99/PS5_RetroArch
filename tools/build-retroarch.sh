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

# The tree that gets compiled is the configured copy under build/, not
# vendor/retroarch, and that is not a matter of taste. configure writes config.h,
# config.mk and the object list into the tree it runs in, and this port's changes
# are applied to that same copy - so vendor/retroarch is upstream exactly as it
# was fetched and build/ra-conf is upstream plus the named changes, compiled. A
# build that read the sources from vendor/ and the headers from build/ compiled
# RetroArch's untouched video_driver.c against the patched video_driver.h: it
# linked, it signed, and it started with video_drivers[] holding no ps5 entry at
# all, because the file that lists the drivers came from the tree that had never
# heard of it. Source and patch have to come from the same place.
tree="$root/build/ra-conf"

# RetroArch's sources include the generated header by a relative path -
# "../config.h" from gfx/, "../../config.h" from deeper still - and every one of
# those resolves inside the tree being compiled, because configure wrote config.h
# at that tree's root. Nothing needs a copy at this repository's root, and an
# earlier version of this script left one there: a generated file, dirty in git,
# that no source read. The include path does the work instead.
configured="$tree/config.h"
if [[ ! -f $configured ]]; then
    # One command to build: if the configured tree is not there yet, make it. The
    # ordering (configure before compiling) is a property of RetroArch, not
    # something a caller should have to know.
    echo "==> [ra] no configured tree yet; running tools/retroarch-sources.sh"
    "$root/tools/retroarch-sources.sh" >/dev/null
fi
[[ -f $configured ]] || { echo "error: configure did not produce build/ra-conf/config.h" >&2; exit 2; }

# The -D flags are not written here. RetroArch passes each enabled feature on the
# compiler command line as well as defining it in config.h, so a list maintained
# by hand is wrong in both directions: a name left out compiles a feature's
# sources against headers that do not know it (libchdr without zlib), and a name
# added that configure turned off compiles code whose sources are not in the
# object list at all (the BSV movie recorder, the soft filters, the video
# filters, the translator). tools/retroarch-flags.sh asks `make` for the flags it
# would use, which is the same answer a working build gets.
mapfile -t defines < <("$root/tools/retroarch-flags.sh" | tr ' ' '\n' | grep -E '^-D' || true)
(( ${#defines[@]} > 0 )) || { echo "error: no compile flags from tools/retroarch-flags.sh" >&2; exit 2; }

# Plus the three things `make` cannot know, because they are about this title
# rather than about RetroArch:
defines+=(
    # HAVE_MAIN is deliberately NOT defined, and defining it is the mistake this
    # build already made once. The name reads like "this platform has a main", but
    # the comment above rarch_main says what it does: without HAVE_MAIN, rarch_main
    # initialises and then runs the frontend's main loop; with it, rarch_main only
    # initialises and returns. The title built with it initialised every driver -
    # this project's video driver included, its display open at 1920x1080, per the
    # trace in the title folder - and then exited zero without drawing a frame.
    # The duplicate `main` symbol it was meant to avoid is removed by renaming
    # upstream's, in patches/series 0003.
    # This SDK's time.h only defines the POSIX clock ids when
    # __POSIX_VISIBLE >= 200112, which the -std=c11 the pipeline uses suppresses.
    # The ids are pre-defined here with the header's own values (CLOCK_REALTIME 0,
    # CLOCK_MONOTONIC 4), so this is the same declaration the header would have
    # made. Both are needed together: the header wraps the whole block in one
    # condition, so defining only the first hides the second.
    -DCLOCK_REALTIME=0 -DCLOCK_MONOTONIC=4
    # Assertions are compiled out. This SDK declares a standard BSD assert - it
    # expands to __assert(__func__, __FILE__, __LINE__, #e) - and __assert lives
    # only in libc.a, which this pipeline does not link: the title's C library is
    # runtime/libc.prx, loaded at run time, and the executable links the SDK's
    # weak stubs. An assert that fires would therefore be an undefined symbol at
    # link time rather than a message at run time. NDEBUG is what a release build
    # of RetroArch uses in any case, so nothing is lost that was meant to ship.
    -DNDEBUG
    # zstd enables its tracing hooks whenever it sees GNUC, ELF and an x86-64
    # target, and a tracing hook is emitted as a weak undefined symbol on the
    # promise that the linker may leave it unresolved. This title's eboot.bin
    # goes through tools/build.sh, which builds a stub table from the symbols the
    # executable imports and refuses to write one for a symbol no public SDK stub
    # exports - so a hook nobody calls still stops the build, with
    # ZSTD_trace_decompress_end as the message. A stronger check than the linker's
    # weak-symbol rule, and the right answer is to not emit the hook: ZSTD_TRACE=0
    # is zstd's own switch for a platform without weak symbols, which is what this
    # is as far as the stub table is concerned.
    -DZSTD_TRACE=0
    # Where this title's own files live. configure baked the /user/homebrew
    # prefix from tools/retroarch-sources.sh's --prefix, and tools/retroarch-flags.sh
    # drops those four flags rather than passing a path that does not exist here:
    # a PS5 title sees its own folder mounted at /app0 and cannot write into the
    # system's homebrew tree. Asset paths point at /app0 so the menu reads the
    # copy shipped inside the title; the paths RetroArch would write to point at
    # /app0 as well, because that is the only place it may write.
    -DGLOBAL_CONFIG_DIR='"/app0"'
    -DASSETS_DIR='"/app0/assets"'
    -DFILTERS_DIR='"/app0/filters"'
    -DCORE_INFO_DIR='"/app0/info"'
)

includes=(
    # The tree that is compiled comes first, both for its headers and for
    # config.h: every -I below names a directory inside it, so a quoted include
    # and a bracketed one resolve to the same copy of every file. That is what
    # makes "the patched video_driver.c was compiled" true by construction rather
    # than by remembering to keep two lists in step.
    -I"$tree"
    -I"$tree/libretro-common/include" -I"$tree/deps"
    -I"$tree/deps/7zip" -I"$tree/deps/stb" -I"$tree/deps/ibxm"
    # libchdr includes <zstd.h>, and RetroArch vendors zstd in deps/zstd. Its
    # public headers sit one level deeper than the package root, which is the
    # difference between this source compiling and the whole CHD path being
    # absent from the frontend.
    -I"$tree/deps/zstd/lib"
    # The vendored zlib's public headers are RetroArch's compatibility copy.
    -I"$tree/libretro-common/include/compat/zlib"
    -I"$tree/deps/libz"
    -I"$tree/libretro-db" -I"$tree/deps/rcheevos/include"
    -I"$root/src"
)

mapfile -t sources < <("$root/tools/retroarch-sources.sh")

if [[ ${1:-} == --list ]]; then
    printf '%s\n' "${sources[@]}"
    printf '==> [ra] %s sources\n' "${#sources[@]}" >&2
    exit 0
fi

# The port's changes to RetroArch's own sources are applied to the configured copy
# in build/ra-conf by tools/apply-port-patches.py, which matches each change on a
# stable anchor line rather than a diff's line numbers. vendor/retroarch is never
# edited, so "upstream plus a named, re-runnable change" stays true.
#
# The patcher's decision is remembered, and that is not bookkeeping. A patch that is
# already applied reports `present` and writes nothing, so the patched file's
# timestamp does not move - and the compile loop below, which skips a source older
# than its object, then keeps an object that was compiled BEFORE the patch existed.
# That is not hypothetical: after the rename of upstream's `main` was added, the
# build kept a retroarch.c.o with no runloop_iterate in it, because the object was
# newer than the already-patched source. The build looked correct and the frontend
# had no main loop in it. So the patch list is stamped, and when it changes the
# objects belonging to patched files are deleted rather than trusted.
patch_stamp="$obj/.patches"
patched_files=()
if [[ -f $root/patches/series ]]; then
    echo "==> [ra] applying the port's changes to build/ra-conf"
    patch_report=$(python3 "$root/tools/apply-port-patches.py" "$root/build/ra-conf") || {
        echo "$patch_report"
        echo "error: a port change could not be applied; the frontend would build" >&2
        echo "       against unpatched sources and silently ignore the driver." >&2
        exit 2
    }
    printf '%s\n' "$patch_report"
    mapfile -t patched_files < <(printf '%s\n' "$patch_report" |
        sed -n 's/^  \([^:]*\): .*/\1/p')
fi

mkdir -p "$obj"

# Up-to-date is not "newer than the source". A flag changed here, or a feature
# configure flipped, changes what every object should contain while every source
# file stays put, and the mtime test above would then keep 225 objects that were
# compiled against a different feature set. The flags and the configured header
# are hashed into a stamp; when it moves, the objects are rebuilt from scratch.
fingerprint=$(printf '%s\n' "${defines[@]}" "${includes[@]}" |
    cat - "$configured" | sha256sum | cut -d' ' -f1)
stamp="$obj/.fingerprint"
if [[ ! -f $stamp || $(<"$stamp") != "$fingerprint" ]]; then
    if [[ -f $stamp ]]; then
        echo "==> [ra] the flags or config.h changed; rebuilding the objects"
        rm -f "$obj"/*.o
    fi
    printf '%s\n' "$fingerprint" > "$stamp"
fi

# The set of patched files, as of now. When it differs from last run's, the objects
# for exactly those files go, so the patch is compiled rather than assumed.
patch_list=$(printf '%s\n' "${patched_files[@]:-}" | sort)
if [[ ! -f $patch_stamp || $(<"$patch_stamp") != "$patch_list" ]]; then
    if [[ -f $patch_stamp ]]; then
        removed=0
        for name in "${patched_files[@]}"; do
            target="$obj/${name//\//_}.o"
            [[ -f $target ]] && { rm -f "$target"; removed=$((removed + 1)); }
        done
        (( removed )) && echo "==> [ra] the port's changes moved; rebuilding $removed object(s)"
    fi
    printf '%s\n' "$patch_list" > "$patch_stamp"
fi

compiled=0
skipped=()
failed=()

for source in "${sources[@]}"; do
    src="$tree/$source"
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

# The objects are collected into one archive, which is how the title's own build
# receives the frontend: tools/build.sh links what APP_STATIC_ARCHIVES names, and
# its source discovery reads src/ only.
#
# The members are the objects this run knows about, in the same order and under
# the same names the loop above produced them, rather than every .o sitting in the
# directory. That distinction is not tidiness: when configure stops building a
# source, the loop skips it and its object stays on disk, so a directory sweep
# would link a feature that is switched off - which is how a stale
# gfx/video_crt_switch.o kept calling switchres after the feature was disabled.
# An object is in the archive only if its source is in the current list and
# compiled in this run.
archive="$out/libretroarch.a"
members=()
for source in "${sources[@]}"; do
    target="$obj/${source//\//_}.o"
    [[ -f $target ]] && members+=("$target")
done
ar=$(command -v llvm-ar || command -v ar)
ranlib=$(command -v llvm-ranlib || command -v ranlib)
[[ -n $ar && -n $ranlib ]] || { echo "error: no archiver found" >&2; exit 2; }
(( ${#members[@]} > 0 )) || { echo "error: no objects to archive" >&2; exit 2; }
rm -f "$archive"
"$ar" rc "$archive" "${members[@]}"
"$ranlib" "$archive"
echo "==> [ra] archived ${#members[@]} objects into build/ra/libretroarch.a"
