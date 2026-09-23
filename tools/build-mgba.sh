#!/usr/bin/env bash
# Cross-build pinned mGBA with this title's SDK. No host compiler fallback.
# Output: build/cores/stage/{cores,info}/; build-title.sh stages these in /app0.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
# Skip the whole build when nothing it reads has changed (tools/core-stamp.sh).
source "$root/tools/core-stamp.sh"
core_stamp_skip mgba \
    "$root/build/cores/stage/cores/mgba_libretro.so" \
    "$root/build/cores/stage/info/mgba_libretro.info" \
    -- "$root/tools/build-mgba.sh" "$root/patches/mgba" "$root/tooling/mgba"
[[ $# == 0 ]] || { echo "usage: ${0##*/}" >&2; exit 2; }
sdk="$root/.deps/native/ps5-payload-sdk"
[[ -x $sdk/bin/prospero-clang ]] || { echo "error: bootstrap this project's SDK first" >&2; exit 2; }
export PS5_PAYLOAD_SDK="$sdk"
export PS5_CLANG=/usr/bin/clang
# Both environment and make variables are explicit: upstream assigns CC/LD.
export CC="$sdk/bin/prospero-clang" CXX="$sdk/bin/prospero-clang++"
export AR="$sdk/bin/prospero-ar" LD="$CC"
revision=7a12d6d4b9acb14c0ae62c9166b6a2f3d08007f6
source_sha=5cbf639e527fb586bf33e14d59037eab35f78f45154e0b79c784fff474c3bc37
info_revision=5a74858ab2f7a50cebb5a6330895bc38899531c0
info_sha=64444beb8268d3a57d53a45564c55049f39ff621315401fef86e82f3849042d6
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
archive="$cache/mgba-$revision.tar.gz"
info="$cache/mgba_libretro.info"
fetch "https://codeload.github.com/libretro/mgba/tar.gz/$revision" "$archive" "$source_sha"
fetch "https://raw.githubusercontent.com/libretro/libretro-core-info/$info_revision/mgba_libretro.info" "$info" "$info_sha"
work="$root/build/cores/mgba"
stage="$root/build/cores/stage"
# Fresh extraction ensures stale objects or modified upstream sources never ship.
rm -rf -- "$work"
mkdir -p "$work" "$stage/cores" "$stage/info"
tar -xzf "$archive" --strip-components=1 -C "$work"
patch --batch --fuzz=0 -d "$work" -p1 < "$root/patches/mgba/native-locale-type.patch"
patch --batch --fuzz=0 -d "$work" -p1 < "$root/patches/mgba/native-config-path.patch"
patch --batch --fuzz=0 -d "$work" -p1 < "$root/patches/mgba/native-xrgb-output.patch"
# An extracted core must not inherit RetroArch's parent Git version/dirty state.
export GIT_CEILING_DIRECTORIES="$root/build/cores"
# The wrapper selects only upstream's libretro target and native runtime imports.
# CMake is required by this pinned upstream revision; target flags use XRGB8888.
cmake -S "$root/tooling/mgba" -B "$work/ps5-build" \
    ${core_ccache:+-DCMAKE_C_COMPILER_LAUNCHER=$core_ccache -DCMAKE_CXX_COMPILER_LAUNCHER=$core_ccache} \
    -DCMAKE_TOOLCHAIN_FILE="$root/tooling/mgba/ps5-toolchain.cmake" \
    -DMGBA_SOURCE_DIR="$work" -DCMAKE_BUILD_TYPE=Release
cmake --build "$work/ps5-build" --target mgba_libretro --parallel "${JOBS:-8}"
cp -- "$work/ps5-build/mgba/mgba_libretro.so" "$work/mgba_libretro.so"
python3 tools/check-core.py "$work/mgba_libretro.so" --report "$work/abi.json"
cp -- "$work/mgba_libretro.so" "$stage/cores/mgba_libretro.so"
cp -- "$info" "$stage/info/mgba_libretro.info"
python3 - "$work" "$revision" "$source_sha" "$info_revision" "$info_sha" <<'PY'
import hashlib, json, pathlib, sys
work = pathlib.Path(sys.argv[1])
report = json.loads((work / 'abi.json').read_text())
report.update(source_revision=sys.argv[2], source_archive_sha256=sys.argv[3],
              info_revision=sys.argv[4], info_sha256=sys.argv[5])
report['sdk_compiler_wrapper_sha256'] = hashlib.sha256(
    pathlib.Path('.deps/native/ps5-payload-sdk/bin/prospero-clang').read_bytes()).hexdigest()
report['port_inputs_sha256'] = {name: hashlib.sha256(pathlib.Path(name).read_bytes()).hexdigest()
    for name in ('tooling/mgba/CMakeLists.txt', 'tooling/mgba/ps5-toolchain.cmake',
                 'tooling/native/ps5-core.ld', 'patches/mgba/native-locale-type.patch',
                 'patches/mgba/native-config-path.patch', 'patches/mgba/native-xrgb-output.patch',
                 'tooling/mgba/ps5-video.h')}
(work / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
PY
core_stamp_write
printf '==> [mgba] built and ABI-checked revision %s; console loading is a separate gate\n' "$revision"
