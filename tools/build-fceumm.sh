#!/usr/bin/env bash
# Cross-build pinned FCEUmm with this title's SDK. No host compiler fallback.
# Output: build/cores/stage/{cores,info}/; build-title.sh stages these in /app0.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
[[ $# == 0 ]] || { echo "usage: ${0##*/}" >&2; exit 2; }
sdk="$root/.deps/native/ps5-payload-sdk"
[[ -x $sdk/bin/prospero-clang ]] || { echo "error: bootstrap this project's SDK first" >&2; exit 2; }
export PS5_PAYLOAD_SDK="$sdk"
export PS5_CLANG=/usr/bin/clang
# Both environment and make variables are explicit: upstream assigns CC/LD.
export CC="$sdk/bin/prospero-clang" CXX="$sdk/bin/prospero-clang++"
export AR="$sdk/bin/prospero-ar" LD="$CC"
revision=236ccdfc911e84c60fea6b9d0699c2d440a8de14
source_sha=dd002cde9b5271979e0394bb9e696bd37e149ced473ff1e3629cc7fed502381f
info_revision=5a74858ab2f7a50cebb5a6330895bc38899531c0
info_sha=eda6fbfdc1fda5ea80c2caed205345b0cbbfa9427acf3626e0cffd6ee5a96721
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
archive="$cache/fceumm-$revision.tar.gz"
info="$cache/fceumm_libretro.info"
fetch "https://codeload.github.com/libretro/libretro-fceumm/tar.gz/$revision" "$archive" "$source_sha"
fetch "https://raw.githubusercontent.com/libretro/libretro-core-info/$info_revision/fceumm_libretro.info" "$info" "$info_sha"
work="$root/build/cores/fceumm"
stage="$root/build/cores/stage"
# Fresh extraction ensures stale objects or modified upstream sources never ship.
rm -rf -- "$work"
mkdir -p "$work" "$stage/cores" "$stage/info"
tar -xzf "$archive" --strip-components=1 -C "$work"
# Keep upstream source/feature selection. Invoke the compiler as link driver:
# SHARED contains -Wl options, which raw prospero-lld must not receive.
# -nodefaultlibs avoids the SDK's static libc/CRT; this title uses runtime imports.
# LIBM is empty because the public libc stub exports the math functions.
# Clang's PS5 default is --build-id=uuid; override it for repeatable artifacts.
make -C "$work" -f Makefile.libretro -j"${JOBS:-8}" platform=unix \
    CC="$CC" CXX="$CXX" AR="$AR" LD="$LD" \
    GIT_VERSION="\" ${revision:0:7}\"" LIBM= \
    LIBS='-lkernel_web -lSceLibcInternal -lScePosixForWebKit' \
    LDFLAGS="-nostdlib -nodefaultlibs -Wl,--build-id=sha1 -Wl,-T,$root/tooling/native/ps5-core.ld"
python3 tools/check-core.py "$work/fceumm_libretro.so" --report "$work/abi.json"
cp -- "$work/fceumm_libretro.so" "$stage/cores/fceumm_libretro.so"
cp -- "$info" "$stage/info/fceumm_libretro.info"
python3 - "$work" "$revision" "$source_sha" "$info_revision" "$info_sha" <<'PY'
import hashlib, json, pathlib, sys
work = pathlib.Path(sys.argv[1])
report = json.loads((work / 'abi.json').read_text())
report.update(source_revision=sys.argv[2], source_archive_sha256=sys.argv[3],
              info_revision=sys.argv[4], info_sha256=sys.argv[5])
report['sdk_compiler_wrapper_sha256'] = hashlib.sha256(
    pathlib.Path('.deps/native/ps5-payload-sdk/bin/prospero-clang').read_bytes()).hexdigest()
(work / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
PY
printf '==> [fceumm] built and ABI-checked revision %s; console loading is a separate gate\n' "$revision"
