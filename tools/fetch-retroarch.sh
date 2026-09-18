#!/usr/bin/env bash
# PS5 RetroArch - fetch the pinned RetroArch source once.
#
#   tools/fetch-retroarch.sh            fetch it if it is not already there
#   tools/fetch-retroarch.sh --check    report the pin and what is present
#   tools/fetch-retroarch.sh --verify   re-check an existing tree against the pin
#
# This is the pin. The revision below is the one thing that decides which
# RetroArch this project is, and it is stated in exactly one place: a version
# string elsewhere in these docs would be a second copy that drifts silently.
#
# What it produces is vendor/retroarch: upstream's tree at that tag, with its
# history removed. The history is dropped on purpose. Nothing here reads it, and
# keeping it would make vendor/retroarch a checkout that someone could commit
# into - the invariant is that upstream is never edited, and a tree with no
# repository cannot be. This project's own changes live in patches/ and are
# applied to a copy under build/ by tools/apply-port-patches.py.
#
# vendor/ is git-ignored: it is fetched, not committed. A clean checkout becomes
# buildable with this command and nothing else.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

upstream="$root/vendor/retroarch"
version="1.22.2"
# The commit the tag points at. A tag can be moved; this cannot, so the tree is
# checked against the commit and not only against the name.
revision="69a4f0ea1e8aaf442ae4858f2e7f2b31a1776576"
url="https://github.com/libretro/RetroArch.git"

mode=${1:-}
case "$mode" in
    ''|--check|--verify) ;;
    *) echo "usage: ${0##*/} [--check|--verify]" >&2; exit 2 ;;
esac

present=false
[[ -f $upstream/retroarch.c && -f $upstream/Makefile.common ]] && present=true
# A tree fetched by this script carries the revision it was taken at, so the pin
# can be checked without the history this script removes.
stamp="$upstream/.rarch-revision"
fetched=none
[[ -f $stamp ]] && fetched=$(<"$stamp")

# A tree that still has its history - a clone made before this script existed, or
# by hand - can be pinned in place rather than downloaded again: its HEAD is the
# same fact the stamp records, and a 200 MB re-fetch would buy nothing. This is
# also what lets --verify accept such a tree.
if [[ $fetched == none && -d $upstream/.git ]] && command -v git >/dev/null; then
    if head=$(git -C "$upstream" rev-parse HEAD 2>/dev/null); then
        fetched="HEAD:$head"
    fi
fi

resolved=false
[[ $fetched == "$revision" || $fetched == "HEAD:$revision" ]] && resolved=true

if [[ $mode == --check ]]; then
    printf 'pin:      %s (%s)\n' "$version" "$revision"
    printf 'url:      %s\n' "$url"
    printf 'tree:     %s\n' "$upstream"
    printf 'present:  %s\n' "$present"
    printf 'revision: %s\n' "$fetched"
    exit 0
fi

if [[ $mode == --verify ]]; then
    $present || { echo "error: no tree at $upstream; run ${0##*/}" >&2; exit 2; }
    if ! $resolved; then
        echo "error: $upstream was taken at $fetched, the pin is $revision" >&2
        echo "       rm -rf $upstream and run ${0##*/} to re-fetch it" >&2
        exit 2
    fi
    echo "==> [fetch] $upstream is at the pinned revision"
    exit 0
fi

if $present; then
    if $resolved; then
        echo "==> [fetch] vendor/retroarch is already at $version ($revision)"
        # A tree that arrived with its history is pinned here rather than fetched
        # again: the same fact is recorded, and the history goes, so what a build
        # reads afterwards is identical to what a fresh clone would give.
        if [[ $fetched == HEAD:* ]]; then
            rm -rf "$upstream/.git"
            printf '%s\n' "$revision" > "$stamp"
            echo "==> [fetch] dropped its history and recorded the revision"
        fi
        exit 0
    fi
    echo "error: vendor/retroarch is present but was taken at $fetched, not $revision" >&2
    echo "       rm -rf vendor/retroarch and run ${0##*/} to replace it" >&2
    exit 2
fi

command -v git >/dev/null || { echo "error: git is required" >&2; exit 2; }

mkdir -p "$root/vendor"
staging="$root/vendor/.retroarch-fetch"
rm -rf "$staging"
echo "==> [fetch] cloning RetroArch $version"
# The tag is asked for by name because that is what a person verifies; the commit
# is what gets used, and a tag that has moved is a mismatch rather than a silent
# upgrade.
git clone --quiet --depth 1 --branch "v$version" "$url" "$staging"

actual=$(git -C "$staging" rev-parse HEAD)
if [[ $actual != "$revision" ]]; then
    rm -rf "$staging"
    echo "error: tag v$version is now $actual, this project pins $revision" >&2
    echo "       upstream moved the tag; decide deliberately and update the pin" >&2
    exit 2
fi

for required in retroarch.c Makefile.common gfx/video_driver.c gfx/video_driver.h \
                qb/config.params.sh libretro-common/include/retroarch.h; do
    [[ -f $staging/$required ]] || {
        echo "error: the fetched tree has no $required" >&2
        rm -rf "$staging"
        exit 2
    }
done

rm -rf "$staging/.git"
printf '%s\n' "$revision" > "$staging/.rarch-revision"
rm -rf "$upstream"
mv "$staging" "$upstream"

echo "==> [fetch] vendor/retroarch is $version at $revision"
echo "==> [fetch] no history was kept, so upstream cannot be committed into"
