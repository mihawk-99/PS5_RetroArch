#!/usr/bin/env bash
# Cross-build pinned PPSSPP's libretro core with this title's SDK.
#
# Output: build/cores/stage/{cores,info}/ppsspp_libretro.{so,info}; build-title.sh
# stages those in /app0. Track A of PPSSPP_Implementation_Plan.md: this builds the
# core, checks its ABI, and says nothing about whether it runs.
#
# Unlike the other cores, PPSSPP needs its git submodules (ext/glslang, ext/armips,
# libretro/libretro-common and the rest), so the fetch is a pinned clone rather than
# a codeload tarball. The input set is identified by the commit plus every submodule
# SHA, recorded in build.json, because there is no single archive digest to check.
#
# PPSSPP_SOURCE_DIR may name an existing checkout of the pinned revision: it is used
# instead of the fetched one, which is how a development tree is iterated against.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
[[ $# == 0 ]] || { echo "usage: ${0##*/}" >&2; exit 2; }
sdk="$root/.deps/native/ps5-payload-sdk"
[[ -x $sdk/bin/prospero-clang ]] || { echo "error: bootstrap this project's SDK first" >&2; exit 2; }
export PS5_PAYLOAD_SDK="$sdk"
export PS5_CLANG=/usr/bin/clang

revision=f293b10fb2d9dc0c2bc10281444ee3d3e932e6ad
info_revision=5a74858ab2f7a50cebb5a6330895bc38899531c0
info_sha=2e9becab17d0db4222e9655bb9b065167e5bcf599ca16256be72beb8f38e646d
cache="$root/.deps/downloads"
mkdir -p "$cache"
info="$cache/ppsspp_libretro.info"
if [[ ! -f $info ]]; then
    curl --fail --location --retry 3 \
        "https://raw.githubusercontent.com/libretro/libretro-core-info/$info_revision/ppsspp_libretro.info" \
        -o "$info.download"
    mv -- "$info.download" "$info"
fi
printf '%s  %s\n' "$info_sha" "$info" | sha256sum --check --status || {
    echo "error: cached input digest mismatch: $info" >&2; exit 1;
}

# The source tree. A supplied PPSSPP_SOURCE_DIR is used as-is and must already be at
# the pinned revision; otherwise the pinned revision is fetched into .deps, which is
# the cache make clean does not remove - a git checkout with submodules is expensive
# to refetch, unlike the other cores' single tarball.
source_dir="${PPSSPP_SOURCE_DIR:-$root/.deps/ppsspp-src}"
if [[ -n ${PPSSPP_SOURCE_DIR:-} ]]; then
    [[ -d $source_dir ]] || { echo "error: PPSSPP_SOURCE_DIR is not a directory" >&2; exit 2; }
    got=$(git -C "$source_dir" rev-parse HEAD)
    [[ $got == "$revision" ]] || {
        echo "error: PPSSPP_SOURCE_DIR is at $got, not the pinned $revision" >&2; exit 2; }
else
    if [[ ! -d $source_dir/.git ]]; then
        echo "==> [ppsspp] fetching $revision (with submodules; this is a large fetch)"
        rm -rf -- "$source_dir"
        git clone --filter=blob:none --no-checkout \
            https://github.com/hrydgard/ppsspp.git "$source_dir"
    fi
    git -C "$source_dir" fetch --quiet origin "$revision" 2>/dev/null ||
        git -C "$source_dir" fetch --quiet origin
    git -C "$source_dir" checkout --force --quiet "$revision"
    got=$(git -C "$source_dir" rev-parse HEAD)
    [[ $got == "$revision" ]] || { echo "error: fetched $got, wanted $revision" >&2; exit 2; }
fi

# A clean tree, every time: the patches below are applied to a known state, so a
# stale object or a half-applied edit cannot ship. -e keeps nothing; the build
# directory is inside the tree and is recreated.
echo "==> [ppsspp] resetting the pinned tree and applying port patches"
git -C "$source_dir" checkout --force --quiet "$revision"
git -C "$source_dir" clean -qfdx
git -C "$source_dir" submodule update --init --recursive --quiet
for patch_file in "$root"/patches/ppsspp/*.patch; do
    patch --batch --fuzz=0 -d "$source_dir" -p1 < "$patch_file"
done

# Reproducibility. libpng (ext/libpng17/pngerror.c) embeds __DATE__ and __TIME__ in a
# banner string, and clang derives both from the clock unless SOURCE_DATE_EPOCH is
# set. That one varying string also changes the linker's string tail-merging, which
# moved ~40 KiB of .rodata between two otherwise identical builds; pinning the epoch
# to the pinned commit's own timestamp removes the only varying input. Deriving it
# from the revision rather than a literal keeps the value self-documenting: the same
# checkout has the same epoch forever.
source_date_epoch=$(git -C "$source_dir" show -s --format=%ct "$revision")
[[ $source_date_epoch =~ ^[0-9]+$ ]] || {
    echo "error: could not read the pinned commit timestamp" >&2; exit 2; }
export SOURCE_DATE_EPOCH="$source_date_epoch"

# This core's destructor registry. Upstream's version script hides it, and its
# Two objects are linked into the core itself, because neither can come from the
# title's import table:
#   core_cxx_runtime.o  the local destructor registry (upstream's version script
#                       hides it) whose fini-array callback has to run before the
#                       native loader unmaps the module
#   ps5-libc-shims.o    the four libc entry points vendored third-party code calls
#                       and the payload SDK does not export (see that file)
build="$root/build/cores/ppsspp"
rm -rf -- "$build"
mkdir -p "$build" "$root/build/cores/stage/cores" "$root/build/cores/stage/info"
"$sdk/bin/prospero-clang++" -std=c++11 -fPIC -fno-exceptions -fno-rtti \
    -c "$root/tooling/native/core_cxx_runtime.cpp" -o "$build/core_cxx_runtime.o"
"$sdk/bin/prospero-clang++" -std=c++17 -O2 -fPIC -Wall -Wextra \
    -c "$root/tooling/ppsspp/ps5-libc-shims.cpp" -o "$build/ps5-libc-shims.o"
link_inputs="$build/core_cxx_runtime.o $build/ps5-libc-shims.o"

# An extracted core must not inherit RetroArch's parent Git version or dirty state.
export GIT_CEILING_DIRECTORIES="$root/build/cores"
# The pinned tree is the top-level CMake project: upstream resolves module paths and
# source lists through ${CMAKE_SOURCE_DIR}, so a wrapper project would break them.
echo "==> [ppsspp] configuring"
cmake -S "$source_dir" -B "$build/ps5-build" \
    -DCMAKE_TOOLCHAIN_FILE="$root/tooling/ppsspp/ps5-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DPS5_CORE_LINK_INPUTS="$link_inputs" \
    -DLIBRETRO=ON -DUNITTEST=OFF -DUSE_CCACHE=OFF \
    -DUSE_FFMPEG=OFF -DUSE_DISCORD=OFF -DUSE_MINIUPNPC=OFF \
    -DUSE_SYSTEM_LIBPNG=OFF -DUSE_SYSTEM_ZSTD=OFF -DUSE_SYSTEM_LIBZIP=OFF \
    -DUSE_SYSTEM_FREETYPE=OFF \
    -DUSE_NO_MMAP=ON -DUSING_GLES2=ON -DOPENGL_LIBRARIES= -DX11_LIBRARIES=
echo "==> [ppsspp] building the libretro target"
cmake --build "$build/ps5-build" --target ppsspp_libretro --parallel "${JOBS:-8}"

core=$(find "$build/ps5-build" -maxdepth 2 -name 'ppsspp_libretro.so' -print -quit)
[[ -n $core && -f $core ]] || { echo "error: no ppsspp_libretro.so was produced" >&2; exit 2; }
cp -- "$core" "$build/ppsspp_libretro.so"
python3 tools/check-core.py "$build/ppsspp_libretro.so" --report "$build/abi.json"
cp -- "$build/ppsspp_libretro.so" "$root/build/cores/stage/cores/ppsspp_libretro.so"

# PPSSPP's runtime asset tree, which the core resolves as <system>/PPSSPP: flash0
# fonts, the language files, the shader sources, the debugger, the zim atlases,
# compat.ini and the cheat/known-function tables. Without it a game boots with
# "Core system files missing, expect bugs" and text, audio and effects are missing.
#
# The source checkout is the canonical tree and the complete one: PPSSPP ships
# assets/ in the repository (193 files), while the build directory only receives a
# partial copy through CMake's file(INSTALL) rules for the GUI bundle (187 files -
# it omits cheats.json, knownfuncs.ini, compatvr.ini, redump.csv and two others).
# The source tree is therefore preferred, and the build tree is only a fallback so
# an upstream that stops shipping assets/ is noticed here rather than at runtime.
assets="$source_dir/assets"
[[ -d $assets ]] || assets=$(find "$build/ps5-build" -maxdepth 2 -type d -name assets -print -quit)
[[ -n $assets && -d $assets ]] || { echo "error: no PPSSPP asset tree in the source or build tree" >&2; exit 2; }
rm -rf -- "$root/build/cores/stage/system/PPSSPP"
mkdir -p "$root/build/cores/stage/system"
cp -a -- "$assets" "$root/build/cores/stage/system/PPSSPP"
printf '==> [ppsspp] staged the runtime assets from %s: %s files, %s bytes\n' \
    "${assets#"$root"/}" \
    "$(find "$root/build/cores/stage/system/PPSSPP" -type f | wc -l)" \
    "$(du -sb "$root/build/cores/stage/system/PPSSPP" | cut -f1)"
cp -- "$info" "$root/build/cores/stage/info/ppsspp_libretro.info"

# The input identity: the commit, every submodule SHA, the patch and tooling hashes,
# and the compiler wrapper. There is no single source digest to check because the
# tree is a git checkout with submodules, so the SHA set is the identity.
python3 - "$build" "$source_dir" "$revision" "$info_revision" "$info_sha" "$source_date_epoch" <<'PY'
import hashlib, json, pathlib, subprocess, sys
build, source, revision, info_revision, info_sha = (pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]),
                                                    sys.argv[3], sys.argv[4], sys.argv[5])
source_date_epoch = sys.argv[6]
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
report = json.loads((build / 'abi.json').read_text())
report.update(source_revision=revision,
              source_date_epoch=int(source_date_epoch),
              source_submodules=dict(sorted(
                  line.split()[0:2][::-1] for line in subprocess.check_output(
                      ['git', '-C', str(source), 'submodule', 'status', '--recursive'],
                      text=True).splitlines() if line.startswith(' '))),
              info_revision=info_revision, info_sha256=info_sha)
report['sdk_compiler_wrapper_sha256'] = sha(pathlib.Path('.deps/native/ps5-payload-sdk/bin/prospero-clang'))
# The asset tree is a directory, so its identity is a digest over its file names and
# contents in sorted order rather than one file hash.
assets_dir = pathlib.Path('build/cores/stage/system/PPSSPP')
asset_files = sorted(p for p in assets_dir.rglob('*') if p.is_file()) if assets_dir.is_dir() else []
asset_digest = hashlib.sha256()
for path in asset_files:
    asset_digest.update(str(path.relative_to(assets_dir)).encode() + b'\0')
    asset_digest.update(hashlib.sha256(path.read_bytes()).digest())
report['ppsspp_assets_files'] = len(asset_files)
report['ppsspp_assets_sha256'] = asset_digest.hexdigest()
report['port_inputs_sha256'] = {name: sha(pathlib.Path(name)) for name in
    ['tools/build-ppsspp.sh', 'tooling/ppsspp/ps5-toolchain.cmake',
     'tooling/ppsspp/ps5-libc-shims.cpp', 'tooling/native/core_cxx_runtime.cpp',
     'tooling/native/ps5-core.ld']
    + [str(path) for path in sorted(pathlib.Path('patches/ppsspp').glob('*.patch'))]}
(build / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
PY
printf '==> [ppsspp] built and ABI-checked revision %s; console loading is a separate gate\n' "$revision"
