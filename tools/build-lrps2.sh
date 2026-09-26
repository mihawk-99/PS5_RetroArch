#!/usr/bin/env bash
# Cross-build LRPS2, libretro's PCSX2 core, with this title's SDK.
#
# Output: build/cores/stage/{cores,info}/pcsx2_libretro.{so,info};
# build-title.sh stages those in /app0. The core runs PCSX2's x86-64 recompilers
# and renders through the frontend's Vulkan device, or on the CPU with its
# software renderer. docs/LRPS2_PORT.md has the plan and the requirements.
#
# The source is my fork, ../PS5_LRPS2 (github.com/mihawk-99/PS5_LRPS2). Every
# change the port makes to the core is committed there, on its local ps5-port
# branch, and patches/lrps2/ps5-port.patch is that branch's diff against the
# pinned revision below. An ordinary build clones the pinned revision, applies
# the patch and builds it, so the title never depends on the fork's unpushed
# commits. LRPS2_DEV=1 builds the fork's working tree as it stands instead,
# which is how the port is edited: change ../PS5_LRPS2, build with LRPS2_DEV=1,
# commit there, then write the patch back with
#   git -C ../PS5_LRPS2 diff <revision> ps5-port > patches/lrps2/ps5-port.patch
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
source "$root/tools/core-stamp.sh"
# An LRPS2_DEV build stages whatever the fork holds, a temporary capture
# included, so it withdraws the stamp: the next ordinary build rebuilds from
# the pinned revision and the committed patch.
[[ -z ${LRPS2_DEV:-} ]] || rm -f -- "$core_stamp_dir/pcsx2"
[[ -n ${LRPS2_DEV:-} ]] || core_stamp_skip pcsx2 \
    "$root/build/cores/stage/cores/pcsx2_libretro.so" \
    "$root/build/cores/stage/info/pcsx2_libretro.info" \
    -- "$root/tools/build-lrps2.sh" "$root/patches/lrps2" "$root/tooling/lrps2"
[[ $# == 0 ]] || { echo "usage: ${0##*/}" >&2; exit 2; }
sdk="$root/.deps/native/ps5-payload-sdk"
[[ -x $sdk/bin/prospero-clang ]] || { echo "error: bootstrap this project's SDK first" >&2; exit 2; }
export PS5_PAYLOAD_SDK="$sdk"
export PS5_CLANG=/usr/bin/clang

revision=6d14775ead86932f48f0107b4f4d7034bfccf344  # ../PS5_LRPS2 master, 2026-09-25
info_revision=5a74858ab2f7a50cebb5a6330895bc38899531c0
info_sha=b263ac17902eb36be08e90a939b1699178e4abe32505d032b96b50f7db2d9fec
cache="$root/.deps/downloads"
mkdir -p "$cache"
info="$cache/pcsx2_libretro.info"
if [[ ! -f $info ]]; then
    curl --fail --location --retry 3 \
        "https://raw.githubusercontent.com/libretro/libretro-core-info/$info_revision/pcsx2_libretro.info" \
        -o "$info.download"
    mv -- "$info.download" "$info"
fi
printf '%s  %s\n' "$info_sha" "$info" | sha256sum --check --status || {
    echo "error: cached input digest mismatch: $info" >&2; exit 1;
}

fork="$root/../PS5_LRPS2"
if [[ -n ${LRPS2_DEV:-} ]]; then
    [[ -d $fork/.git ]] || { echo "error: LRPS2_DEV needs the fork at $fork" >&2; exit 2; }
    source_dir=$(cd -- "$fork" && pwd)
    echo "==> [lrps2] LRPS2_DEV: building $source_dir as it stands"
else
    source_dir="$root/.deps/lrps2-src"
    if [[ ! -d $source_dir/.git ]]; then
        # The fork beside this repository when it is there (no download), the
        # published one otherwise; the pinned revision is checked either way.
        origin=https://github.com/mihawk-99/PS5_LRPS2.git
        [[ -d $fork/.git ]] && origin=$(cd -- "$fork" && pwd)
        echo "==> [lrps2] fetching $revision from $origin"
        rm -rf -- "$source_dir"
        git clone --quiet --no-checkout "$origin" "$source_dir"
    fi
    git -C "$source_dir" cat-file -e "$revision^{commit}" 2>/dev/null ||
        git -C "$source_dir" fetch --quiet origin "$revision"
    echo "==> [lrps2] resetting the pinned tree and applying the port patch"
    git -C "$source_dir" checkout --force --quiet "$revision"
    git -C "$source_dir" clean -qfdx
    git -C "$source_dir" apply --whitespace=nowarn "$root/patches/lrps2/ps5-port.patch"
    got=$(git -C "$source_dir" rev-parse HEAD)
    [[ $got == "$revision" ]] || { echo "error: the tree is at $got, wanted $revision" >&2; exit 2; }
fi

# Reproducibility: pin __DATE__/__TIME__ to the pinned commit's own timestamp.
source_date_epoch=$(git -C "$source_dir" show -s --format=%ct "$revision")
export SOURCE_DATE_EPOCH="$source_date_epoch"

# Two objects are linked into the core itself:
#   core_cxx_runtime.o  the core-local destructor registry the native loader runs
#                       before unmapping the module
#   ps5-libc-shims.o    the libc entry points LRPS2 calls and the console lacks
build="$root/build/cores/lrps2"
mkdir -p "$build" "$root/build/cores/stage/cores" "$root/build/cores/stage/info"
"$sdk/bin/prospero-clang++" -std=c++11 -fPIC -fno-exceptions -fno-rtti \
    -c "$root/tooling/native/core_cxx_runtime.cpp" -o "$build/core_cxx_runtime.o"
"$sdk/bin/prospero-clang++" -std=c++17 -O2 -fPIC -Wall -Wextra \
    -c "$root/tooling/lrps2/ps5-libc-shims.cpp" -o "$build/ps5-libc-shims.o"
link_inputs="$build/core_cxx_runtime.o $build/ps5-libc-shims.o"

# The software renderer is the bring-up path and the hardware one is Vulkan;
# OpenGL, the network adapter's libpcap and the multi-ISA copies are not built.
# The fork and the pinned clone are different source trees, so each has its
# own build directory (one CMake cache cannot serve both).
cmake_build="$build/ps5-build${LRPS2_DEV:+-dev}"
export GIT_CEILING_DIRECTORIES="$root/build/cores"
echo "==> [lrps2] configuring"
cmake -S "$source_dir" -B "$cmake_build" \
    ${core_ccache:+-DCMAKE_C_COMPILER_LAUNCHER=$core_ccache -DCMAKE_CXX_COMPILER_LAUNCHER=$core_ccache} \
    -DCMAKE_TOOLCHAIN_FILE="$root/tooling/lrps2/ps5-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DPS5_CORE_LINK_INPUTS="$link_inputs" \
    -DLIBRETRO=ON -DUSE_OPENGL=OFF -DUSE_VULKAN=ON -DDISABLE_ADVANCE_SIMD=OFF \
    -DPCAP=OFF > "$build/configure.log" ||
    { tail -30 "$build/configure.log" >&2; exit 1; }
echo "==> [lrps2] building the libretro target"
cmake --build "$cmake_build" --target pcsx2_libretro --parallel "${JOBS:-16}"

core="$cmake_build/bin/pcsx2_libretro.so"
[[ -f $core ]] || { echo "error: no pcsx2_libretro.so was produced" >&2; exit 2; }
cp -- "$core" "$build/pcsx2_libretro.so"
python3 tools/check-core.py "$build/pcsx2_libretro.so" --report "$build/abi.json"
cp -- "$build/pcsx2_libretro.so" "$root/build/cores/stage/cores/pcsx2_libretro.so"
cp -- "$info" "$root/build/cores/stage/info/pcsx2_libretro.info"

python3 - "$build" "$revision" "$info_revision" "$info_sha" "$source_date_epoch" <<'PY'
import hashlib, json, pathlib, sys
build = pathlib.Path(sys.argv[1])
revision, info_revision, info_sha, epoch = sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
report = json.loads((build / 'abi.json').read_text())
report.update(source_revision=revision, source_date_epoch=int(epoch),
              info_revision=info_revision, info_sha256=info_sha)
report['port_inputs_sha256'] = {name: sha(pathlib.Path(name)) for name in
    ['tools/build-lrps2.sh', 'tooling/lrps2/ps5-toolchain.cmake',
     'tooling/lrps2/ps5-libc-shims.cpp', 'tooling/native/core_cxx_runtime.cpp',
     'tooling/native/ps5-core.ld', 'patches/lrps2/ps5-port.patch']}
(build / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
PY
[[ -n ${LRPS2_DEV:-} ]] || core_stamp_write
printf '==> [lrps2] built and ABI-checked revision %s; console loading is a separate gate\n' "$revision"
