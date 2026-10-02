#!/usr/bin/env bash
# Cross-build Flycast's libretro core with this title's SDK.
#
# Output: build/cores/stage/{cores,info}/flycast_libretro.{so,info}, which
# build-title.sh stages into /app0. The core renders through the frontend's
# Vulkan device and runs the SH4/ARM7 dynarecs on executable memory the title
# provides through ps5platform/exec.h (patches/flycast/flycast-ps5.patch).
#
# The port is one patch applied to the pinned tree. Flycast needs its git
# submodules (core/deps: glslang, libchdr, libzip, tinygettext and its nested
# tinycmmc), so the fetch is a pinned clone rather than a codeload tarball, the
# same arrangement tools/build-ppsspp.sh uses. FLYCAST_DEV=1 builds the tree as
# it stands instead (no reset, no patch), which is how the port is edited; write
# the patch back with
# `git -C .deps/flycast-src diff > patches/flycast/flycast-ps5.patch`.
#
# FLYCAST_SOURCE_DIR may name an existing checkout of the pinned revision.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
# Skip the whole build when nothing it reads has changed (tools/core-stamp.sh).
source "$root/tools/core-stamp.sh"
# A FLYCAST_DEV build stages whatever the tree holds, a temporary capture
# included, so it withdraws the stamp: the next ordinary build rebuilds from the
# pinned revision and the committed patch rather than keeping that output.
[[ -z ${FLYCAST_DEV:-} ]] || rm -f -- "$core_stamp_dir/flycast"
[[ -n ${FLYCAST_DEV:-} ]] || core_stamp_skip flycast \
    "$root/build/cores/stage/cores/flycast_libretro.so" \
    "$root/build/cores/stage/info/flycast_libretro.info" \
    -- "$root/tools/build-flycast.sh" "$root/patches/flycast" "$root/tooling/flycast"
[[ $# == 0 ]] || { echo "usage: ${0##*/}" >&2; exit 2; }
sdk="$root/.deps/native/ps5-payload-sdk"
[[ -x $sdk/bin/prospero-clang ]] || { echo "error: bootstrap this project's SDK first" >&2; exit 2; }
export PS5_PAYLOAD_SDK="$sdk"
export PS5_CLANG=/usr/bin/clang

revision=e36e9df2dcc1487acdb1dc7725766f1f5ba029b5
info_revision=5a74858ab2f7a50cebb5a6330895bc38899531c0
info_sha=d32663d9bd4dfcbe8bf84cdf806ac1d7437512c2b5d75287b9a918ebed968898
cache="$root/.deps/downloads"
mkdir -p "$cache"
info="$cache/flycast_libretro.info"
if [[ ! -f $info ]]; then
    curl --fail --location --retry 3 \
        "https://raw.githubusercontent.com/libretro/libretro-core-info/$info_revision/flycast_libretro.info" \
        -o "$info.download"
    mv -- "$info.download" "$info"
fi
printf '%s  %s\n' "$info_sha" "$info" | sha256sum --check --status || {
    echo "error: cached input digest mismatch: $info" >&2; exit 1;
}

# The source tree. A supplied FLYCAST_SOURCE_DIR is used as-is and must already
# be at the pinned revision; otherwise the pinned revision is fetched into .deps,
# the cache make clean does not remove - a git checkout with submodules is
# expensive to refetch, unlike a single tarball.
source_dir="${FLYCAST_SOURCE_DIR:-$root/.deps/flycast-src}"
if [[ -n ${FLYCAST_SOURCE_DIR:-} ]]; then
    [[ -d $source_dir ]] || { echo "error: FLYCAST_SOURCE_DIR is not a directory" >&2; exit 2; }
    got=$(git -C "$source_dir" rev-parse HEAD)
    [[ $got == "$revision" ]] || {
        echo "error: FLYCAST_SOURCE_DIR is at $got, not the pinned $revision" >&2; exit 2; }
else
    if [[ ! -d $source_dir/.git ]]; then
        echo "==> [flycast] fetching $revision (with submodules; this is a large fetch)"
        rm -rf -- "$source_dir"
        git clone --filter=blob:none --no-checkout \
            https://github.com/sysfce2/libretro-flycast.git "$source_dir"
    fi
    [[ -n ${FLYCAST_DEV:-} ]] || git -C "$source_dir" cat-file -e "$revision^{commit}" 2>/dev/null ||
        git -C "$source_dir" fetch --quiet --filter=blob:none origin "$revision"
    [[ -n ${FLYCAST_DEV:-} ]] || git -C "$source_dir" checkout --force --quiet "$revision"
    got=$(git -C "$source_dir" rev-parse HEAD)
fi

# A clean tree, every time: the patch is applied to a known state, so a stale
# object or a half-applied edit cannot ship.
if [[ -n ${FLYCAST_DEV:-} ]]; then
    echo "==> [flycast] FLYCAST_DEV: building the tree as it stands"
else
    echo "==> [flycast] resetting the pinned tree and applying the port patch"
    git -C "$source_dir" checkout --force --quiet "$revision"
    git -C "$source_dir" clean -qfdx
    git -C "$source_dir" submodule update --init --recursive --quiet
    git -C "$source_dir" apply --whitespace=nowarn "$root/patches/flycast/flycast-ps5.patch"
fi

# Reproducibility: pin the embedded build timestamp to the pinned commit's own
# timestamp, the same arrangement tools/build-ppsspp.sh uses.
source_date_epoch=$(git -C "$source_dir" show -s --format=%ct "$revision")
[[ $source_date_epoch =~ ^[0-9]+$ ]] || {
    echo "error: could not read the pinned commit timestamp" >&2; exit 2; }
export SOURCE_DATE_EPOCH="$source_date_epoch"

# Objects linked into the core itself, because none of them can come from the
# title's import table:
#   core_cxx_runtime.o  the local destructor registry (upstream's version script
#                       hides it) whose fini-array callback has to run before the
#                       native loader unmaps the module
#   ps5-stubs.o         entry points Flycast references on code paths that never
#                       run here (PTY serial, unwind registration, locale_t
#                       helpers); see that file
#   ps5-net.o           getaddrinfo/freeaddrinfo/gethostbyname: the SDK routes
#                       the resolver family to a module titles do not load, so
#                       the core carries a direct UDP DNS client instead.
#                       Defined hidden in the link, so the references bind here
#                       instead of the import table.
#   ps5-libcxx-inst.o   the explicit instantiation of std::stringbuf::str(str),
#                       inline-only in the SDK headers
#   libc++ members      std::future's machinery (future/memory/system_error/
#                       thread). The whole archive is NOT linked: its locale and
#                       io members pull a FreeBSD-14 surface (catgets, *_l, *at)
#                       the table does not provide.
build="$root/build/cores/flycast"
# The build directory is kept between builds; a changed configuration is picked
# up by CMake's own re-run.
mkdir -p "$build" "$build/libcxx" "$root/build/cores/stage/cores" "$root/build/cores/stage/info"
"$sdk/bin/prospero-clang" -O2 -fPIC \
    -c "$root/tooling/flycast/ps5-stubs.c" -o "$build/ps5-stubs.o"
"$sdk/bin/prospero-clang" -O2 -fPIC \
    -c "$root/tooling/flycast/ps5-net.c" -o "$build/ps5-net.o"
"$sdk/bin/prospero-clang++" -std=c++11 -fPIC -fno-exceptions -fno-rtti \
    -c "$root/tooling/native/core_cxx_runtime.cpp" -o "$build/core_cxx_runtime.o"
"$sdk/bin/prospero-clang++" -std=c++17 -O2 -fPIC \
    -c "$root/tooling/flycast/ps5-libcxx-inst.cpp" -o "$build/ps5-libcxx-inst.o"
(cd "$build/libcxx" && "$sdk/bin/prospero-ar" x "$sdk/target/lib/libc++.a" \
    future.cpp.o memory.cpp.o system_error.cpp.o thread.cpp.o)
link_inputs="$build/core_cxx_runtime.o $build/ps5-stubs.o $build/ps5-net.o $build/ps5-libcxx-inst.o"
link_inputs="$link_inputs $build/libcxx/future.cpp.o $build/libcxx/memory.cpp.o"
link_inputs="$link_inputs $build/libcxx/system_error.cpp.o $build/libcxx/thread.cpp.o"

# An extracted core must not inherit RetroArch's parent Git version or dirty state.
export GIT_CEILING_DIRECTORIES="$root/build/cores"
echo "==> [flycast] configuring"
cmake -S "$source_dir" -B "$build/ps5-build" \
    ${core_ccache:+-DCMAKE_C_COMPILER_LAUNCHER=$core_ccache -DCMAKE_CXX_COMPILER_LAUNCHER=$core_ccache} \
    -DCMAKE_TOOLCHAIN_FILE="$root/tooling/flycast/ps5-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DPS5_CORE_LINK_INPUTS="$link_inputs" \
    -DLIBRETRO=ON -DUSE_VULKAN=ON -DUSE_OPENGL=OFF \
    -DUSE_HOST_GLSLANG=OFF -DUSE_HOST_LIBCHDR=OFF -DUSE_HOST_LIBZIP=OFF \
    -DUSE_OPENMP=OFF -DUSE_DISCORD=OFF -DUSE_MINIUPNPC=OFF \
    -DUSE_LIBCDIO=OFF -DENABLE_LOG=OFF -DENABLE_GDB_SERVER=OFF
echo "==> [flycast] building the libretro target"
cmake --build "$build/ps5-build" --target flycast_libretro --parallel "${JOBS:-16}"

core=$(find "$build/ps5-build" -maxdepth 2 -name 'flycast_libretro.so' -print -quit)
[[ -n $core && -f $core ]] || { echo "error: no flycast_libretro.so was produced" >&2; exit 2; }
cp -- "$core" "$build/flycast_libretro.so"
python3 tools/check-core.py "$build/flycast_libretro.so" --report "$build/abi.json"
cp -- "$build/flycast_libretro.so" "$root/build/cores/stage/cores/flycast_libretro.so"
cp -- "$info" "$root/build/cores/stage/info/flycast_libretro.info"

# The input identity: the commit, every submodule SHA, the patch and tooling
# hashes, and the compiler wrapper. There is no single source digest to check
# because the tree is a git checkout with submodules, so the SHA set is the
# identity.
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
report['port_inputs_sha256'] = {name: sha(pathlib.Path(name)) for name in
    ['tools/build-flycast.sh', 'tooling/flycast/ps5-toolchain.cmake',
     'tooling/flycast/ps5-stubs.c', 'tooling/flycast/ps5-net.c', 'tooling/flycast/ps5-libcxx-inst.cpp',
     'tooling/native/core_cxx_runtime.cpp', 'tooling/native/ps5-core.ld',
     'patches/flycast/flycast-ps5.patch']}
(build / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
PY
[[ -n ${FLYCAST_DEV:-} ]] || core_stamp_write
printf '==> [flycast] built and ABI-checked revision %s; console loading is a separate gate\n' "$revision"
