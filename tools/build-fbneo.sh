#!/usr/bin/env bash
# Cross-build pinned FBNeo with this title's SDK. No host compiler fallback.
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
revision=6bb3167a044e19e7106a5110d5531aa9c6afa96f
source_sha=7d4cacbd55e74c5cd973fbe63d1c75b8dbe5c29b61593d8185be60483d29c892
info_revision=5a74858ab2f7a50cebb5a6330895bc38899531c0
info_sha=c95c3e177d61adf6662af7adf7361d7fefd9cd87ab0773d105a97818767e5815
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
archive="$cache/fbneo-$revision.tar.gz"
info="$cache/fbneo_libretro.info"
fetch "https://codeload.github.com/libretro/FBNeo/tar.gz/$revision" "$archive" "$source_sha"
fetch "https://raw.githubusercontent.com/libretro/libretro-core-info/$info_revision/fbneo_libretro.info" "$info" "$info_sha"
work="$root/build/cores/fbneo"
stage="$root/build/cores/stage"
# Fresh extraction ensures stale objects or modified upstream sources never ship.
rm -rf -- "$work"
mkdir -p "$work" "$stage/cores" "$stage/info"
tar -xzf "$archive" --strip-components=1 -C "$work"
patch --batch --fuzz=0 -d "$work" -p1 < "$root/patches/fbneo/native-video-and-metadata.patch"
patch --batch --fuzz=0 -d "$work" -p1 < "$root/patches/fbneo/native-audio-bounds.patch"
patch --batch --fuzz=0 -d "$work" -p1 < "$root/patches/fbneo/native-catalogue-storage.patch"
# Compile this core's destructor registry as a local object; upstream's version
# script hides it. Its fini-array callback runs before native-loader unmap.
"$CXX" -std=c++11 -fPIC -fno-exceptions -fno-rtti -c \
    "$root/tooling/native/core_cxx_runtime.cpp" -o "$work/core_cxx_runtime.o"
# Keep upstream renderer/feature selection; convert only at the video callback.
# C++ runtime imports resolve against the title's SDK runtime at native load time.
# No separate payload CRT/libc or host libraries are linked into this core.
make -C "$work/src/burner/libretro" -f "$root/tooling/fbneo/Makefile.ps5" -j"${JOBS:-8}" platform=unix \
    CC="$CC" CXX="$CXX" AR="$AR" LD="$LD" \
    GIT_VERSION="${revision:0:7}" GIT_DATE= \
    PS5_FBNEO_PORT_DIR="$root/tooling/fbneo" \
    LIBS="$work/core_cxx_runtime.o -lkernel_web -lSceLibcInternal -lScePosixForWebKit" \
    LDFLAGS="-nostdlib -nodefaultlibs -Wl,-z,undefs -Wl,--build-id=sha1 -Wl,-T,$root/tooling/native/ps5-core.ld"
cp -- "$work/src/burner/libretro/fbneo_libretro.so" "$work/fbneo_libretro.so"
python3 tools/check-core.py "$work/fbneo_libretro.so" --report "$work/abi.json"
cp -- "$work/fbneo_libretro.so" "$stage/cores/fbneo_libretro.so"
cp -- "$info" "$stage/info/fbneo_libretro.info"
python3 - "$work" "$revision" "$source_sha" "$info_revision" "$info_sha" <<'PY'
import hashlib, json, pathlib, sys
work = pathlib.Path(sys.argv[1])
report = json.loads((work / 'abi.json').read_text())
report.update(source_revision=sys.argv[2], source_archive_sha256=sys.argv[3],
              info_revision=sys.argv[4], info_sha256=sys.argv[5])
report['sdk_compiler_wrapper_sha256'] = hashlib.sha256(
    pathlib.Path('.deps/native/ps5-payload-sdk/bin/prospero-clang').read_bytes()).hexdigest()
report['port_inputs_sha256'] = {name: hashlib.sha256(pathlib.Path(name).read_bytes()).hexdigest()
    for name in ('tools/build-fbneo.sh', 'tooling/native/ps5-core.ld', 'tooling/native/core_cxx_runtime.cpp',
                 'patches/fbneo/native-video-and-metadata.patch', 'patches/fbneo/native-audio-bounds.patch',
                 'patches/fbneo/native-catalogue-storage.patch', 'tooling/fbneo/ps5-video.h',
                 'tooling/fbneo/Makefile.ps5')}
(work / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
PY
printf '==> [fbneo] built and ABI-checked revision %s; console loading is a separate gate\n' "$revision"
