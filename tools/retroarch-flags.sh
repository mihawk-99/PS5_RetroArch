#!/usr/bin/env bash
# PS5 RetroArch - the compile flags RetroArch's own build uses for this project.
#
#   tools/retroarch-flags.sh            print the flags, space separated
#   tools/retroarch-flags.sh --refresh  ask make again, ignoring the cached copy
#
# Why this exists. A feature flag in RetroArch is not only a `#define` in the
# generated config.h: it is also passed on the compiler command line, because a
# source may test it before config.h reaches it. libchdr_chd.c is the proof - it
# guards its zlib code with `#ifdef HAVE_ZLIB` near the top, before any header
# that includes config.h, so a build that relied on config.h alone compiled the
# CHD reader without zlib and failed on a type zlib declares. Hand-writing the
# list fails the same way in the other direction: a flag written here that
# configure turned off compiles a feature whose sources are not in the object
# list, which is what happened with HAVE_BSV_MOVIE, HAVE_VIDEO_FILTER,
# HAVE_DSP_FILTER and HAVE_TRANSLATE.
#
# So the list is not written by hand and not read out of config.h. It is read
# from the command `make` itself would run, by dry-running the one object that
# every configuration compiles. Whatever make would do, this project does.
#
# The -D flags that name a path (GLOBAL_CONFIG_DIR and friends) keep the values
# configure chose, which point at the /user/homebrew prefix in the configure
# flags. Where this title actually reads its assets from is a separate question
# and is not settled by this file; docs/ACTIVE.md carries it.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

work="$root/build/ra-conf"
sdk="${PS5_PAYLOAD_SDK:-$root/.deps/native/ps5-payload-sdk}"
cache="$work/.rarch-flags"

[[ -d $work ]] || { echo "error: run tools/retroarch-sources.sh first" >&2; exit 2; }
[[ -d $sdk ]] || { echo "error: no SDK at $sdk" >&2; exit 2; }

if [[ ${1:-} == --refresh ]]; then
    rm -f "$cache"
elif [[ $# -ne 0 ]]; then
    echo "usage: ${0##*/} [--refresh]" >&2
    exit 2
fi

if [[ ! -s $cache ]]; then
    command=$(cd "$work" && PS5_PAYLOAD_SDK="$sdk" \
        CC="$sdk/bin/prospero-clang" CXX="$sdk/bin/prospero-clang++" \
        OS=BSD DISTRO= make -n obj-unix/release/retroarch.o 2>/dev/null |
        grep -m1 -E '(^| )-c( |$)' || true)
    [[ -n $command ]] || {
        echo "error: could not read the compile flags; see tools/retroarch-sources.sh" >&2
        exit 2
    }
    # The -D flags that name a path are dropped here. Their values are shell
    # quoted in the command make prints (DIR='"/some/path"'), and splitting that
    # line on spaces would hand the compiler a bare /some/path where a string
    # literal belongs - which is how configuration.c, retroarch.c and
    # platform_unix.c stopped compiling. They also point at the /user/homebrew
    # prefix chosen at configure time, and this title does not live there. The
    # paths this build does use are set in tools/build-retroarch.sh.
    printf '%s\n' "$command" | tr ' ' '\n' |
        grep -E '^-D' | grep -vE '^-D[A-Z_]+_DIR=' | sort -u > "$cache"
    [[ -s $cache ]] || { echo "error: 'make -n' reported no -D flags" >&2; exit 2; }
fi

tr '\n' ' ' < "$cache"
printf '\n'
