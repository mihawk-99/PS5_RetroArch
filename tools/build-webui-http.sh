#!/usr/bin/env bash
# The streaming HTTP parser used by ps5-payload-dev/websrv; no TLS server needed on LAN.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:-ps5}
[[ $mode == ps5 || $mode == host ]] || exit 2
version=1.0.10
hash=04bfe8ef75db7d629a33de767599765cecadc56274a39822d5d081030d577685
cache="$root/.deps/webui"
source_dir="$cache/libmicrohttpd-$version"
build="$root/build/webui-mhd-$mode"
archive="$cache/libmicrohttpd-$version.tar.gz"
mkdir -p "$cache" "$build"
if [[ ! -f $archive ]]; then
    curl -fL "https://ftp.gnu.org/gnu/libmicrohttpd/libmicrohttpd-$version.tar.gz" -o "$archive.download"
    printf '%s  %s\n' "$hash" "$archive.download" | sha256sum -c - >&2
    mv "$archive.download" "$archive"
fi
printf '%s  %s\n' "$hash" "$archive" | sha256sum -c - >/dev/null
[[ -d $source_dir ]] || tar -xzf "$archive" -C "$cache"
stamp="$hash $mode $(sha256sum "$0" | cut -d' ' -f1)"
if [[ $mode == ps5 ]]; then
    sdk="$root/.deps/native/ps5-payload-sdk"
    stamp+=" $(cat "$sdk/.ps5-sdk-revision")"
    export PS5_CLANG=${PS5_CLANG:-clang}
    export CC="$sdk/bin/prospero-clang" AR="$sdk/bin/prospero-ar" RANLIB="$sdk/bin/prospero-ranlib"
    export CFLAGS='-O1 -fPIC'
    # Cross-configure assumes full FreeBSD libc; the title SDK lacks these symbols.
    # Select MHD's bundled tree implementation and ordinary pipe wakeups instead.
    export mhd_cv_sys_tsearch_usable=no mhd_cv_works_func_pipe2=no
    host=(--host=x86_64-pc-freebsd)
else
    host=()
fi
if [[ ! -f $build/.stamp || $(cat "$build/.stamp") != "$stamp" ]]; then
    (
        cd "$build"
        if [[ -f Makefile ]]; then make distclean > clean.log 2>&1; fi
        "$source_dir/configure" "${host[@]}" --disable-shared --enable-static --disable-https \
            --disable-curl --disable-examples --disable-doc --disable-tools > configure.log 2>&1
        make -j"${BUILD_JOBS:-8}" > build.log 2>&1
    ) || { tail -30 "$build/configure.log" "$build/build.log" >&2; exit 1; }
    printf '%s\n' "$stamp" > "$build/.stamp"
fi
printf '%s\n' "$build/src/microhttpd/.libs/libmicrohttpd.a"
