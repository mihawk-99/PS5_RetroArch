#!/usr/bin/env bash
# Cross-build pinned Genesis Plus GX with this title's SDK. No host compiler fallback.
# Output: build/cores/stage/{cores,info}/; build-title.sh stages these in /app0.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
# Skip the whole build when nothing it reads has changed (tools/core-stamp.sh).
source "$root/tools/core-stamp.sh"
core_stamp_skip genesis_plus_gx \
    "$root/build/cores/stage/cores/genesis_plus_gx_libretro.so" \
    "$root/build/cores/stage/info/genesis_plus_gx_libretro.info" \
    -- "$root/tools/build-genesis-plus-gx.sh" "$root/patches/genesis-plus-gx" "$root/tooling/genesis-plus-gx"
[[ $# == 0 ]] || { echo "usage: ${0##*/}" >&2; exit 2; }
sdk="$root/.deps/native/ps5-payload-sdk"
[[ -x $sdk/bin/prospero-clang ]] || { echo "error: bootstrap this project's SDK first" >&2; exit 2; }
export PS5_PAYLOAD_SDK="$sdk"
export PS5_CLANG=/usr/bin/clang
# Both environment and make variables are explicit: upstream assigns CC/LD.
export CC="$sdk/bin/prospero-clang" CXX="$sdk/bin/prospero-clang++"
export AR="$sdk/bin/prospero-ar" LD="$CC"
revision=c2838c7dc4236fc2fe94e5dbd08b41486067918e
source_sha=7ba2eab9d6dae71bb42e8208573300ad3475a4263e860178c4d19c36d85fc92b
info_revision=5a74858ab2f7a50cebb5a6330895bc38899531c0
info_sha=9793bff8d9e298a7ee0c94c0511dab242200ca60fbe87e3720eb9231a3e0166a
cache="$root/.deps/downloads"
mkdir -p "$cache"
fetch() {
    local url=$1 target=$2 digest=$3
    if [[ ! -f $target ]]; then
        curl --fail --location --retry 3 "$url" -o "$target.download"
        printf '%s  %s\n' "$digest" "$target.download" | sha256sum --check --status || {
            rm -f -- "$target.download"; echo 'error: download digest mismatch' >&2; exit 1;
        }
        mv -- "$target.download" "$target"
    fi
    printf '%s  %s\n' "$digest" "$target" | sha256sum --check --status || {
        echo "error: cached input digest mismatch: $target" >&2; exit 1;
    }
}
archive="$cache/genesis-plus-gx-$revision.tar.gz"
info="$cache/genesis_plus_gx_libretro.info"
fetch "https://codeload.github.com/libretro/Genesis-Plus-GX/tar.gz/$revision" "$archive" "$source_sha"
fetch "https://raw.githubusercontent.com/libretro/libretro-core-info/$info_revision/genesis_plus_gx_libretro.info" "$info" "$info_sha"
work="$root/build/cores/genesis_plus_gx"
stage="$root/build/cores/stage"
# Fresh extraction ensures stale objects or modified upstream sources never ship.
rm -rf -- "$work"
mkdir -p "$work" "$stage/cores" "$stage/info"
tar -xzf "$archive" --strip-components=1 -C "$work"
# Normalize this CRLF source in the disposable copy so the small LF patch applies.
python3 - "$work/libretro/libretro.c" <<'PYCRLF'
from pathlib import Path
import sys
p = Path(sys.argv[1])
p.write_bytes(p.read_bytes().replace(b'\r\n', b'\n'))
PYCRLF
patch --batch --fuzz=0 -d "$work" -p1 < "$root/patches/genesis-plus-gx/native-xrgb-output.patch"
# Preserve the RGB565 renderer, CHD and bundled codecs. Disable Linux-host
# autodetection of physical CD-ROM access; disc-image loading remains enabled.
# Disable optional zstd weak tracing hooks: no external trace provider is linked.
# Use the existing native import table and no separate payload CRT/libc.
CFLAGS="-I$root/tooling/genesis-plus-gx -DZSTD_TRACE=0" make -C "$work" -f Makefile.libretro -j"${JOBS:-8}" platform=unix \
    CC="${core_ccache:+$core_ccache }$CC" CXX="${core_ccache:+$core_ccache }$CXX" AR="$AR" LD="$LD" \
    GIT_VERSION="\" ${revision:0:7}\"" HAVE_CDROM=0 \
    SHARED='-shared -Wl,--version-script=libretro/link.T -Wl,-z,undefs' \
    LIBS='-lkernel_web -lSceLibcInternal -lScePosixForWebKit' \
    LDFLAGS="-nostdlib -nodefaultlibs -Wl,--build-id=sha1 -Wl,-T,$root/tooling/native/ps5-core.ld"
python3 tools/check-core.py "$work/genesis_plus_gx_libretro.so" --report "$work/abi.json"
cp -- "$work/genesis_plus_gx_libretro.so" "$stage/cores/genesis_plus_gx_libretro.so"
cp -- "$info" "$stage/info/genesis_plus_gx_libretro.info"
python3 - "$work" "$revision" "$source_sha" "$info_revision" "$info_sha" <<'PY'
import hashlib, json, pathlib, sys
work = pathlib.Path(sys.argv[1])
report = json.loads((work / 'abi.json').read_text())
report.update(source_revision=sys.argv[2], source_archive_sha256=sys.argv[3],
              info_revision=sys.argv[4], info_sha256=sys.argv[5])
report['sdk_compiler_wrapper_sha256'] = hashlib.sha256(
    pathlib.Path('.deps/native/ps5-payload-sdk/bin/prospero-clang').read_bytes()).hexdigest()
report['port_inputs_sha256'] = {name: hashlib.sha256(pathlib.Path(name).read_bytes()).hexdigest()
    for name in ('tools/build-genesis-plus-gx.sh', 'tooling/native/ps5-core.ld',
                 'patches/genesis-plus-gx/native-xrgb-output.patch', 'tooling/genesis-plus-gx/ps5-video.h')}
(work / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
PY
core_stamp_write
printf '==> [genesis_plus_gx] built and ABI-checked revision %s; console loading is a separate gate\n' "$revision"
