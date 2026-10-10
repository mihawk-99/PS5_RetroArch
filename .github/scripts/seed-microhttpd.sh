#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# libmicrohttpd's release tarball for tools/build-webui-http.sh, put where it looks
# (.deps/webui), so the build does not depend on ftp.gnu.org answering a CI runner. GNU's
# server first, then Ubuntu's archive, whose orig tarball is the same file; either is checked
# against the script's own pin (version, hash) and kept in CI_DOWNLOADS.
#
#   .github/scripts/seed-microhttpd.sh
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
script=$root/tools/build-webui-http.sh
version=$(sed -n 's/^version=\([0-9.]*\)$/\1/p' "$script")
hash=$(sed -n 's/^hash=\([0-9a-f]\{64\}\)$/\1/p' "$script")
[[ -n $version && -n $hash ]] || { echo "seed-microhttpd: no version or hash in $script" >&2; exit 2; }
target=$root/.deps/webui/libmicrohttpd-$version.tar.gz
check() { printf '%s  %s\n' "$hash" "$1" | sha256sum --check --strict --status 2>/dev/null; }
if check "$target"; then
    echo "==> [microhttpd] $target is libmicrohttpd $version"
    exit 0
fi
cache=${CI_DOWNLOADS:-$HOME/.cache/ps5-retroarch-ci}/libmicrohttpd-$version.tar.gz
mkdir -p "$(dirname -- "$cache")" "$(dirname -- "$target")"
if ! check "$cache"; then
    for url in "https://ftp.gnu.org/gnu/libmicrohttpd/libmicrohttpd-$version.tar.gz" \
        "http://archive.ubuntu.com/ubuntu/pool/universe/libm/libmicrohttpd/libmicrohttpd_$version.orig.tar.gz"; do
        if curl --fail --location --retry 3 --connect-timeout 30 --silent --show-error \
            -o "$cache.download" "$url" && check "$cache.download"; then
            mv -- "$cache.download" "$cache"
            echo "==> [microhttpd] from $url"
            break
        fi
        rm -f -- "$cache.download"
    done
fi
printf '%s  %s\n' "$hash" "$cache" | sha256sum --check --strict
cp -- "$cache" "$target"
