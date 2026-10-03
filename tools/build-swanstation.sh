#!/usr/bin/env bash
# Cross-build pinned SwanStation (libretro's DuckStation fork) with this title's
# SDK. No host compiler fallback.
# Output: build/cores/stage/{cores,info}/swanstation_libretro.{so,info};
# build-title.sh stages these in /app0.
#
# The port is one patch (patches/swanstation/swanstation-ps5.patch): the x64
# recompiler's code buffer comes from ps5platform/exec.h instead of an
# RWX mmap a title cannot make, the 48 MiB in-image code buffer the loader's
# exec region cannot keep writable is dropped, and the 4 GiB MMap fastmem
# scheme - aliased views plus a fault handler inside a title sandbox - is left
# out so the recompiler uses its LUT path.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
# Skip the whole build when nothing it reads has changed (tools/core-stamp.sh).
source "$root/tools/core-stamp.sh"
core_stamp_skip swanstation \
    "$root/build/cores/stage/cores/swanstation_libretro.so" \
    "$root/build/cores/stage/info/swanstation_libretro.info" \
    -- "$root/tools/build-swanstation.sh" "$root/patches/swanstation" "$root/tooling/swanstation"
[[ $# == 0 ]] || { echo "usage: ${0##*/}" >&2; exit 2; }
sdk="$root/.deps/native/ps5-payload-sdk"
[[ -x $sdk/bin/prospero-clang ]] || { echo "error: bootstrap this project's SDK first" >&2; exit 2; }
export PS5_PAYLOAD_SDK="$sdk"
export PS5_CLANG=/usr/bin/clang
revision=b6c30a7b270a3f68ac41f268eafdfa678d17dea2
source_sha=7f5711abd802d38095fc9239250704fa57ebff8c7d69d31f8702d1cc1e3e0256
info_revision=de2472e24f255bc094e800f2f78fbee79e89fb9e
info_sha=aa93c787b9a021e698547efc8c933f136d059cce48ad89faac9fdb0dafaf3c58
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
archive="$cache/swanstation-$revision.tar.gz"
info="$cache/swanstation_libretro.info"
fetch "https://codeload.github.com/libretro/swanstation/tar.gz/$revision" "$archive" "$source_sha"
fetch "https://raw.githubusercontent.com/libretro/libretro-core-info/$info_revision/swanstation_libretro.info" "$info" "$info_sha"
work="$root/build/cores/swanstation"
stage="$root/build/cores/stage"
# Fresh extraction ensures stale objects or modified upstream sources never ship.
rm -rf -- "$work"
mkdir -p "$work" "$stage/cores" "$stage/info"
tar -xzf "$archive" --strip-components=1 -C "$work"
patch --batch --fuzz=0 -d "$work" -p1 < "$root/patches/swanstation/swanstation-ps5.patch"
# Compile this core's destructor registry as a local object; the title's libc++
# atexit registration would outlive the unmapped core.
"$sdk/bin/prospero-clang++" -std=c++11 -fPIC -fno-exceptions -fno-rtti -c \
    "$root/tooling/native/core_cxx_runtime.cpp" -o "$work/core_cxx_runtime.o"
# An extracted core must not inherit RetroArch's parent Git version/dirty state.
export GIT_CEILING_DIRECTORIES="$root/build/cores"
echo "==> [swanstation] configuring"
cmake -S "$work" -B "$work/ps5-build" \
    ${core_ccache:+-DCMAKE_C_COMPILER_LAUNCHER=$core_ccache -DCMAKE_CXX_COMPILER_LAUNCHER=$core_ccache} \
    -DCMAKE_TOOLCHAIN_FILE="$root/tooling/swanstation/ps5-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DPS5_CORE_LINK_INPUTS="$work/core_cxx_runtime.o"
echo "==> [swanstation] building the libretro target"
cmake --build "$work/ps5-build" --target swanstation_libretro --parallel "${JOBS:-16}"
core="$work/ps5-build/swanstation_libretro.so"
[[ -f $core ]] || { echo "error: no swanstation_libretro.so was produced" >&2; exit 2; }
cp -- "$core" "$work/swanstation_libretro.so"
python3 tools/check-core.py "$work/swanstation_libretro.so" --report "$work/abi.json"
cp -- "$work/swanstation_libretro.so" "$stage/cores/swanstation_libretro.so"
cp -- "$info" "$stage/info/swanstation_libretro.info"
python3 - "$work" "$revision" "$source_sha" "$info_revision" "$info_sha" <<'PY'
import hashlib, json, pathlib, sys
work = pathlib.Path(sys.argv[1])
report = json.loads((work / 'abi.json').read_text())
report.update(source_revision=sys.argv[2], source_archive_sha256=sys.argv[3],
              info_revision=sys.argv[4], info_sha256=sys.argv[5])
report['sdk_compiler_wrapper_sha256'] = hashlib.sha256(
    pathlib.Path('.deps/native/ps5-payload-sdk/bin/prospero-clang').read_bytes()).hexdigest()
report['port_inputs_sha256'] = {name: hashlib.sha256(pathlib.Path(name).read_bytes()).hexdigest()
    for name in ('tools/build-swanstation.sh', 'tooling/swanstation/ps5-toolchain.cmake',
                 'tooling/native/ps5-core.ld', 'tooling/native/core_cxx_runtime.cpp',
                 'patches/swanstation/swanstation-ps5.patch')}
(work / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
PY
core_stamp_write
printf '==> [swanstation] built and ABI-checked revision %s; console loading is a separate gate\n' "$revision"
