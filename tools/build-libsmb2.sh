#!/usr/bin/env bash
# Build libsmb2, the SMB2/3 client behind the smb:// VFS (patches/series, 0136) and the
# WebUI's SMB page, as a static archive: for the PS5, or for the host (the tests).
#
#   tools/build-libsmb2.sh [ps5|host]    prints the prefix (lib/libsmb2.a, include/smb2)
#
# libsmb2 has no release since 6.2 (2024-12); the revision is the master commit
# itsPLK/ps5-pkg-manager pins and runs on the PS5, fetched as GitHub's archive of it
# and pinned by its digest. Built as that project builds it: static, no Kerberos, no
# GSSAPI, no libdcerpc (share listing is libsmb2's own srvsvc call).
#
# Output: .deps/native/libsmb2-ps5 or build/libsmb2-host/prefix, with the revision and
# this script's digest in .stamp.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
mode=${1:-ps5}
[[ $mode == ps5 || $mode == host ]] || { echo "usage: ${0##*/} [ps5|host]" >&2; exit 2; }
revision=68f1136e7f6e7d136fd08893dc0028faeb2a120b
digest=0006174d6045ff21a565879faaa24f1615f3189f8a5d1b3f03c900239b8ecc4a
build="$root/build/libsmb2-$mode"
prefix="$root/.deps/native/libsmb2-ps5"
[[ $mode == host ]] && prefix="$build/prefix"
stamp="$revision $mode $(sha256sum "$0" | cut -c1-64)"
if [[ -f $prefix/.stamp && $(<"$prefix/.stamp") == "$stamp" ]]; then
    printf '%s\n' "$prefix"
    exit 0
fi

toolchain=()
flags="-fPIC -ffunction-sections -fdata-sections"
if [[ $mode == ps5 ]]; then
    sdk="$root/.deps/native/ps5-payload-sdk"
    [[ -x $sdk/bin/prospero-clang ]] || { echo "error: bootstrap this project's SDK first" >&2; exit 2; }
    export PS5_PAYLOAD_SDK="$sdk" PS5_CLANG=${PS5_CLANG:-/usr/bin/clang}
    toolchain=(-DCMAKE_TOOLCHAIN_FILE="$sdk/toolchain/prospero.cmake")
    # A title's link binds getaddrinfo to the platform layer's, which resolves nothing
    # (PS5_Vulkan's tools/radv-link.sh): libsmb2 asks src/ps5_smb.c's instead.
    flags+=" -march=znver2 -Dgetaddrinfo=ps5_smb_getaddrinfo -Dfreeaddrinfo=ps5_smb_freeaddrinfo"
    # Only libsmb2 sockets: per-call MSG_DONTWAIT keeps its reply-draining loop
    # bounded even if the descriptor's O_NONBLOCK setting is ineffective.
    flags+=" -Dreadv=ps5_smb_socket_readv -Dwritev=ps5_smb_socket_writev"
fi

archive="$root/.deps/downloads/libsmb2-$revision.tar.gz"
mkdir -p "$root/.deps/downloads"
[[ -f $archive ]] || curl --fail --location --retry 3 \
    "https://github.com/sahlberg/libsmb2/archive/$revision.tar.gz" -o "$archive" >&2
printf '%s  %s\n' "$digest" "$archive" | sha256sum --check --status || {
    echo "error: libsmb2 archive digest mismatch" >&2; exit 1; }

rm -rf -- "$build"
mkdir -p "$build/src"
tar -xzf "$archive" -C "$build/src" --strip-components=1
cmake -S "$build/src" -B "$build/cmake" -G Ninja "${toolchain[@]}" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="$flags" -DBUILD_SHARED_LIBS=OFF -DENABLE_LIBKRB5=OFF -DENABLE_GSSAPI=OFF \
    -DENABLE_LIBDCERPC=OFF -DENABLE_EXAMPLES=OFF \
    > "$build/configure.log" 2>&1 || { tail -30 "$build/configure.log" >&2; exit 1; }
ninja -C "$build/cmake" -j"${JOBS:-6}" smb2 > "$build/build.log" 2>&1 ||
    { grep -E "error" "$build/build.log" | head -20 >&2; exit 1; }
rm -rf -- "$prefix"
mkdir -p "$prefix/lib" "$prefix/include" "$prefix/share/licenses/libsmb2"
cp "$(find "$build/cmake" -name libsmb2.a | head -1)" "$prefix/lib/"
cp -r "$build/src/include/smb2" "$prefix/include/"
cp "$build/src/COPYING" "$build/src/LICENCE-LGPL-2.1.txt" "$prefix/share/licenses/libsmb2/"
printf '%s\n' "$stamp" > "$prefix/.stamp"
echo "==> [libsmb2] $revision ($mode): $(du -h "$prefix/lib/libsmb2.a" | cut -f1) in $prefix" >&2
printf '%s\n' "$prefix"
