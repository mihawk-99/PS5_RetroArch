#!/usr/bin/env bash
# Cross-build pinned Snes9x with this title's SDK. No host compiler fallback.
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
export AR="$sdk/bin/prospero-ar" LD="$CXX"
revision=fae2fea08f74180759ef540ee94259213f503480
source_sha=0d4b0c4181d66668ec0040eb17f90a559f7b7fa6cbd9198593c3bdbe79acd029
info_revision=5a74858ab2f7a50cebb5a6330895bc38899531c0
info_sha=fa62b78d58bc4c30f4e0a4a581f0cb3252447b7f198b55bd6ebe2e4b9bde5cc2
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
archive="$cache/snes9x-$revision.tar.gz"
info="$cache/snes9x_libretro.info"
fetch "https://codeload.github.com/libretro/snes9x/tar.gz/$revision" "$archive" "$source_sha"
fetch "https://raw.githubusercontent.com/libretro/libretro-core-info/$info_revision/snes9x_libretro.info" "$info" "$info_sha"
work="$root/build/cores/snes9x"
stage="$root/build/cores/stage"
# Fresh extraction ensures stale objects or modified upstream sources never ship.
rm -rf -- "$work"
mkdir -p "$work" "$stage/cores" "$stage/info"
tar -xzf "$archive" --strip-components=1 -C "$work"
patch --batch --fuzz=0 -d "$work" -p1 < "$root/patches/snes9x/native-xrgb-output.patch"
# Compile this core's destructor registry as a local object; upstream's version
# script hides it. Its fini-array callback runs before native-loader unmap.
"$CXX" -std=c++11 -fPIC -fno-exceptions -fno-rtti -c \
    "$root/tooling/native/core_cxx_runtime.cpp" -o "$work/core_cxx_runtime.o"
# Keep upstream renderer/feature selection; convert only at the video callback.
# C++ runtime imports resolve against the title's SDK runtime at native load time.
# No separate payload CRT/libc or host libraries are linked into this core.
make -C "$work/libretro" -j"${JOBS:-8}" platform=unix \
    CC="$CC" CXX="$CXX" AR="$AR" LD="$LD" \
    GIT_VERSION="\" ${revision:0:7}\"" LTO= \
    CPPFLAGS="-I$root/tooling/snes9x" \
    LIBS="$work/core_cxx_runtime.o -lkernel_web -lSceLibcInternal -lScePosixForWebKit" \
    LDFLAGS="-nostdlib -nodefaultlibs -Wl,-z,undefs -Wl,--build-id=sha1 -Wl,-T,$root/tooling/native/ps5-core.ld"
cp -- "$work/libretro/snes9x_libretro.so" "$work/snes9x_libretro.so"
python3 tools/check-core.py "$work/snes9x_libretro.so" --report "$work/abi.json"
cp -- "$work/snes9x_libretro.so" "$stage/cores/snes9x_libretro.so"
cp -- "$info" "$stage/info/snes9x_libretro.info"
python3 - "$work" "$revision" "$source_sha" "$info_revision" "$info_sha" <<'PY'
import hashlib, json, pathlib, sys
work = pathlib.Path(sys.argv[1])
report = json.loads((work / 'abi.json').read_text())
report.update(source_revision=sys.argv[2], source_archive_sha256=sys.argv[3],
              info_revision=sys.argv[4], info_sha256=sys.argv[5])
report['sdk_compiler_wrapper_sha256'] = hashlib.sha256(
    pathlib.Path('.deps/native/ps5-payload-sdk/bin/prospero-clang').read_bytes()).hexdigest()
report['port_inputs_sha256'] = {name: hashlib.sha256(pathlib.Path(name).read_bytes()).hexdigest()
    for name in ('tools/build-snes9x.sh', 'tooling/native/ps5-core.ld', 'tooling/native/core_cxx_runtime.cpp',
                 'patches/snes9x/native-xrgb-output.patch', 'tooling/snes9x/ps5-video.h')}
(work / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
PY
printf '==> [snes9x] built and ABI-checked revision %s; console loading is a separate gate\n' "$revision"
