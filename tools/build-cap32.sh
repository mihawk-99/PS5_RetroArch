#!/usr/bin/env bash
# Cross-build pinned CAP32 with this title's SDK.
# Output: build/cores/stage/{cores,info}/; build-title.sh stages these in /app0.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

# Skip the whole build when nothing it reads has changed.
source "$root/tools/core-stamp.sh"
core_stamp_skip cap32 \
    "$root/build/cores/stage/cores/cap32_libretro.so" \
    "$root/build/cores/stage/info/cap32_libretro.info" \
    -- "$root/tools/build-cap32.sh" "$root/patches/cap32" "$root/tooling/cap32"

[[ $# == 0 ]] || { echo "usage: ${0##*/}" >&2; exit 2; }

sdk="$root/.deps/native/ps5-payload-sdk"
[[ -x $sdk/bin/prospero-clang ]] || { echo "error: bootstrap this project's SDK first" >&2; exit 2; }

export PS5_PAYLOAD_SDK="$sdk"
export PS5_CLANG=/usr/bin/clang

# Disable debug assertions to prevent unresolved __assert symbols from SceLibcInternal
export CC="$sdk/bin/prospero-clang -DNDEBUG"
export CXX="$sdk/bin/prospero-clang++ -DNDEBUG"
export AR="$sdk/bin/prospero-ar" LD="$CC"

revision="2d41a1f07e654e97c4f24ab8447c3fbd7d728bc8"
source_sha="c2274c6f2e3e7a1b0e8ad824083ea506f92bcb30a18a04da23dcb00d3367e9ea"

info_revision="5a74858ab2f7a50cebb5a6330895bc38899531c0" 
info_sha="7ef5d3ad67d195a5dc87ed62d9edf6732169cbde6da045583cc81a1264aad6fb"

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

archive="$cache/cap32-$revision.tar.gz"
info="$cache/cap32_libretro.info"

fetch "https://codeload.github.com/libretro/libretro-cap32/tar.gz/$revision" "$archive" "$source_sha"
fetch "https://raw.githubusercontent.com/libretro/libretro-core-info/$info_revision/cap32_libretro.info" "$info" "$info_sha"

work="$root/build/cores/cap32"
stage="$root/build/cores/stage"

rm -rf -- "$work"
mkdir -p "$work" "$stage/cores" "$stage/info"
tar -xzf "$archive" --strip-components=1 -C "$work"

make -C "$work" -f Makefile -j"${JOBS:-8}" platform=unix \
    CC="${core_ccache:+$core_ccache }$CC" CXX="${core_ccache:+$core_ccache }$CXX" AR="$AR" LD="$LD" \
    GIT_VERSION="\" ${revision:0:7}\"" LIBM= \
    LIBS='-lkernel_web -lSceLibcInternal -lScePosixForWebKit' \
    LDFLAGS="-nostdlib -nodefaultlibs -Wl,--build-id=sha1 -Wl,-T,$root/tooling/native/ps5-core.ld"

python3 tools/check-core.py "$work/cap32_libretro.so" --report "$work/abi.json"

cp -- "$work/cap32_libretro.so" "$stage/cores/cap32_libretro.so"
cp -- "$info" "$stage/info/cap32_libretro.info"

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

core_stamp_write
printf '==> [cap32] built and ABI-checked revision %s; console loading is a separate gate\n' "$revision"

