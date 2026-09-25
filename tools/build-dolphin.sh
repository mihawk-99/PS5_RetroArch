#!/usr/bin/env bash
# Cross-build Dolphin's libretro core (libretro/dolphin, which tracks upstream
# Dolphin) with this title's SDK.
#
# Output: build/cores/stage/{cores,info}/dolphin_libretro.{so,info} and Dolphin's
# Sys data in build/cores/stage/system/dolphin-emu/Sys; build-title.sh stages
# those in /app0. The core renders through the frontend's Vulkan device and runs
# the x86-64 JIT (Jit64) with fastmem.
#
# The port is one patch, patches/dolphin/ps5-port.patch, applied to the pinned
# tree. DOLPHIN_DEV=1 builds the tree as it stands instead (no reset, no patch),
# which is how the port is edited: change .deps/dolphin-src, build with
# DOLPHIN_DEV=1, then write the patch back with
# `git -C .deps/dolphin-src diff > patches/dolphin/ps5-port.patch`.
#
# Like PPSSPP, Dolphin needs its git submodules (Externals/*), so the fetch is a
# pinned clone; the input set is the commit plus every submodule SHA, recorded
# in build.json.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
source "$root/tools/core-stamp.sh"
# A DOLPHIN_DEV build stages whatever the tree holds, a temporary capture included,
# so it withdraws the stamp: the next ordinary build rebuilds from the pinned
# revision and the committed patch rather than keeping that output
# (2026-09-25: an instrumented Dolphin survived a gate build this way).
[[ -z ${DOLPHIN_DEV:-} ]] || rm -f -- "$core_stamp_dir/dolphin"
[[ -n ${DOLPHIN_DEV:-} ]] || core_stamp_skip dolphin \
    "$root/build/cores/stage/cores/dolphin_libretro.so" \
    "$root/build/cores/stage/info/dolphin_libretro.info" \
    "$root/build/cores/stage/system/dolphin-emu/Sys" \
    -- "$root/tools/build-dolphin.sh" "$root/patches/dolphin" "$root/tooling/dolphin"
[[ $# == 0 ]] || { echo "usage: ${0##*/}" >&2; exit 2; }
sdk="$root/.deps/native/ps5-payload-sdk"
[[ -x $sdk/bin/prospero-clang ]] || { echo "error: bootstrap this project's SDK first" >&2; exit 2; }
export PS5_PAYLOAD_SDK="$sdk"
export PS5_CLANG=/usr/bin/clang

revision=c6630001e05780b7c03e661a4a539b59ef716ebc  # libretro/dolphin master, Dolphin 2609
info_revision=5a74858ab2f7a50cebb5a6330895bc38899531c0
info_sha=8e4e763481ba4a44fa2421c33f79a4f011d3897542528dc876dda71e4c191346
cache="$root/.deps/downloads"
mkdir -p "$cache"
info="$cache/dolphin_libretro.info"
if [[ ! -f $info ]]; then
    curl --fail --location --retry 3 \
        "https://raw.githubusercontent.com/libretro/libretro-core-info/$info_revision/dolphin_libretro.info" \
        -o "$info.download"
    mv -- "$info.download" "$info"
fi
printf '%s  %s\n' "$info_sha" "$info" | sha256sum --check --status || {
    echo "error: cached input digest mismatch: $info" >&2; exit 1;
}

source_dir="$root/.deps/dolphin-src"
if [[ ! -d $source_dir/.git ]]; then
    echo "==> [dolphin] fetching $revision (with submodules; this is a large fetch)"
    rm -rf -- "$source_dir"
    git clone --filter=blob:none --no-checkout https://github.com/libretro/dolphin.git "$source_dir"
fi
if [[ -n ${DOLPHIN_DEV:-} ]]; then
    echo "==> [dolphin] DOLPHIN_DEV: building the tree as it stands"
else
    git -C "$source_dir" cat-file -e "$revision^{commit}" 2>/dev/null ||
        git -C "$source_dir" fetch --quiet origin "$revision"
    echo "==> [dolphin] resetting the pinned tree and applying the port patch"
    git -C "$source_dir" checkout --force --quiet "$revision"
    git -C "$source_dir" clean -qfdx
    git -C "$source_dir" submodule update --init --recursive --quiet --jobs 8
    git -C "$source_dir" apply --whitespace=nowarn "$root/patches/dolphin/ps5-port.patch"
fi
got=$(git -C "$source_dir" rev-parse HEAD)
[[ $got == "$revision" ]] || { echo "error: the tree is at $got, wanted $revision" >&2; exit 2; }

# Reproducibility: pin __DATE__/__TIME__ to the pinned commit's own timestamp.
source_date_epoch=$(git -C "$source_dir" show -s --format=%ct "$revision")
export SOURCE_DATE_EPOCH="$source_date_epoch"

# Two objects are linked into the core itself:
#   core_cxx_runtime.o  the core-local destructor registry the native loader runs
#                       before unmapping the module
#   ps5-libc-shims.o    the libc entry points Dolphin calls and the console lacks
#                       (see that file)
# and an empty libm.a satisfies the -lm that libspng adds: the math functions are
# the SDK libc's own.
build="$root/build/cores/dolphin"
mkdir -p "$build/empty-libm" "$root/build/cores/stage/cores" "$root/build/cores/stage/info"
"$sdk/bin/prospero-clang++" -std=c++11 -fPIC -fno-exceptions -fno-rtti \
    -c "$root/tooling/native/core_cxx_runtime.cpp" -o "$build/core_cxx_runtime.o"
"$sdk/bin/prospero-clang++" -std=c++17 -O2 -fPIC -Wall -Wextra \
    -c "$root/tooling/dolphin/ps5-libc-shims.cpp" -o "$build/ps5-libc-shims.o"
[[ -f $build/empty-libm/libm.a ]] || "$sdk/bin/prospero-ar" rc "$build/empty-libm/libm.a"
link_inputs="$build/core_cxx_runtime.o $build/ps5-libc-shims.o"

export GIT_CEILING_DIRECTORIES="$root/build/cores"
echo "==> [dolphin] configuring"
cmake -S "$source_dir" -B "$build/ps5-build" \
    ${core_ccache:+-DCMAKE_C_COMPILER_LAUNCHER=$core_ccache -DCMAKE_CXX_COMPILER_LAUNCHER=$core_ccache} \
    -DCMAKE_TOOLCHAIN_FILE="$root/tooling/dolphin/ps5-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DPS5_CORE_LINK_INPUTS="$link_inputs" -DPS5_EMPTY_LIBM="$build/empty-libm" \
    -DLIBRETRO=ON -DENABLE_X11=OFF -DENABLE_EGL=OFF -DUSE_SYSTEM_LIBS=OFF \
    -DUSE_MGBA=OFF -DENABLE_LLVM=OFF -DENCODE_FRAMEDUMPS=OFF -DUSE_UPNP=OFF \
    -DENABLE_SDL=OFF -DENABLE_CLI_TOOL=OFF -DENABLE_TESTS=OFF -DENABLE_QT=OFF \
    -DENABLE_AUTOUPDATE=OFF -DENABLE_ANALYTICS=OFF -DUSE_DISCORD_PRESENCE=OFF \
    -DUSE_RETRO_ACHIEVEMENTS=OFF > "$build/configure.log" ||
    { tail -30 "$build/configure.log" >&2; exit 1; }
echo "==> [dolphin] building the libretro target"
cmake --build "$build/ps5-build" --target dolphin_libretro --parallel "${JOBS:-16}"

core="$build/ps5-build/dolphin_libretro.so"
[[ -f $core ]] || { echo "error: no dolphin_libretro.so was produced" >&2; exit 2; }
cp -- "$core" "$build/dolphin_libretro.so"
python3 tools/check-core.py "$build/dolphin_libretro.so" --report "$build/abi.json"
cp -- "$build/dolphin_libretro.so" "$root/build/cores/stage/cores/dolphin_libretro.so"
cp -- "$info" "$root/build/cores/stage/info/dolphin_libretro.info"

# Dolphin's Sys tree (game settings, shaders, fonts, the GameCube IPL font data
# and the title databases), which the core resolves as <system>/dolphin-emu/Sys.
rm -rf -- "$root/build/cores/stage/system/dolphin-emu"
mkdir -p "$root/build/cores/stage/system/dolphin-emu"
cp -a -- "$source_dir/Data/Sys" "$root/build/cores/stage/system/dolphin-emu/Sys"
printf '==> [dolphin] staged Sys: %s files, %s bytes\n' \
    "$(find "$root/build/cores/stage/system/dolphin-emu/Sys" -type f | wc -l)" \
    "$(du -sb "$root/build/cores/stage/system/dolphin-emu/Sys" | cut -f1)"

python3 - "$build" "$source_dir" "$revision" "$info_revision" "$info_sha" "$source_date_epoch" <<'PY'
import hashlib, json, pathlib, subprocess, sys
build, source = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
revision, info_revision, info_sha, epoch = sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6]
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
report = json.loads((build / 'abi.json').read_text())
report.update(source_revision=revision, source_date_epoch=int(epoch),
              source_submodules=dict(sorted(
                  line.split()[0:2][::-1] for line in subprocess.check_output(
                      ['git', '-C', str(source), 'submodule', 'status', '--recursive'],
                      text=True).splitlines() if line.startswith(' '))),
              info_revision=info_revision, info_sha256=info_sha)
report['port_inputs_sha256'] = {name: sha(pathlib.Path(name)) for name in
    ['tools/build-dolphin.sh', 'tooling/dolphin/ps5-toolchain.cmake',
     'tooling/dolphin/ps5-libc-shims.cpp', 'tooling/native/core_cxx_runtime.cpp',
     'tooling/native/ps5-core.ld', 'patches/dolphin/ps5-port.patch']}
(build / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
PY
[[ -n ${DOLPHIN_DEV:-} ]] || core_stamp_write
printf '==> [dolphin] built and ABI-checked revision %s; console loading is a separate gate\n' "$revision"
