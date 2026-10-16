#!/usr/bin/env bash
# Optional SwanStation core, built as a native shared object with Vulkan and JIT.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
source "$root/tools/core-stamp.sh"
core_stamp_skip swanstation \
    "$root/build/cores/stage/cores/swanstation_libretro.so" \
    "$root/build/cores/stage/info/swanstation_libretro.info" \
    "$root/build/cores/stage/info/swanstation-LICENSE.txt" \
    -- "$root/tools/build-swanstation.sh" "$root/tooling/swanstation"
[[ $# == 0 ]] || { echo "usage: ${0##*/}" >&2; exit 2; }
sdk="$root/.deps/native/ps5-payload-sdk"
[[ -x $sdk/bin/prospero-clang ]] || { echo 'error: bootstrap the project SDK first' >&2; exit 2; }
export PS5_PAYLOAD_SDK="$sdk" PS5_CLANG=/usr/bin/clang
export CC="$sdk/bin/prospero-clang" CXX="$sdk/bin/prospero-clang++" AR="$sdk/bin/prospero-ar"
python3 "$root/tooling/swanstation/prepare.py"
work="$root/build/cores/swanstation"
mkdir -p "$work/empty-libs" "$root/build/cores/stage/cores" "$root/build/cores/stage/info"
for name in m rt pthread dl util; do
    [[ -f $work/empty-libs/lib$name.a ]] || "$AR" rc "$work/empty-libs/lib$name.a"
done
runtime="$work/core_cxx_runtime.o"
"$CXX" -std=c++11 -fPIC -fno-exceptions -fno-rtti -c \
    "$root/tooling/native/core_cxx_runtime.cpp" -o "$runtime"
revision=$(python3 -c 'import json; print(json.load(open("tooling/swanstation/sources.json"))["revision"])')
build_date=$(python3 -c 'import json; print(json.load(open("tooling/swanstation/sources.json"))["commit_date"])')
flags='-O2 -fPIC -march=znver2 -DZSTD_TRACE=0'
ldflags="-nostdlib -nodefaultlibs -Wl,-m,elf_x86_64_fbsd -Wl,-z,undefs -Wl,--build-id=sha1"
ldflags+=" -Wl,-T,$root/tooling/native/ps5-core.ld -L$work/empty-libs"
ldflags+=" $runtime -Wl,--no-as-needed -lkernel_web -lSceLibcInternal -lScePosixForWebKit"
CFLAGS="$flags" CXXFLAGS="$flags -frtti -fexceptions" LDFLAGS="$ldflags" \
make -C "$work/source" -f Makefile.libretro -f "$root/tooling/swanstation/ps5.mk" \
    -j"${JOBS:-4}" ps5-core platform=unix CPU_ARCH=x64 WITH_MMAP_FASTMEM=0 \
    LINK_STATIC_LIBCPLUSPLUS=0 CONFIG_HAVE_ALLOCA_H= \
    CC="$CC" CXX="$CXX" LD="$CXX" AR="$AR" PS5_CORE_RUNTIME="$runtime" \
    SHARED='-shared -Wl,--version-script=link.T' GIT_VERSION="${revision:0:7}" \
    GIT_HASH="$revision" BUILD_DATE="$build_date"
core="$work/source/swanstation_libretro.so"
python3 "$root/tools/check-core.py" "$core" --report "$work/abi.json"
cp -- "$core" "$root/build/cores/stage/cores/"
cp -- "$root/.deps/downloads/swanstation_libretro.info" "$root/build/cores/stage/info/"
python3 "$root/tooling/swanstation/notices.py" "$work/source" \
    "$root/build/cores/stage/info/swanstation-LICENSE.txt"
python3 - "$root" <<'PY'
import hashlib, json, pathlib, sys
root = pathlib.Path(sys.argv[1]); work = root/'build/cores/swanstation'
report = json.loads((work/'abi.json').read_text())
report['sources'] = json.loads((work/'source-revisions.json').read_text())
report['source_revision'] = report['sources']['revision']
report['source_archive_sha256'] = report['sources']['sha256']
report.update(jit=True, mmap_fastmem=False, renderer='Vulkan', console_loading_verified=False)
report['port_inputs_sha256'] = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
    for p in [root/'tools/build-swanstation.sh', *sorted((root/'tooling/swanstation').glob('*')),
              root/'tooling/native/ps5-core.ld', root/'tooling/native/core_cxx_runtime.cpp'] if p.is_file()}
(work/'build.json').write_text(json.dumps(report, indent=2)+'\n')
PY
core_stamp_write
echo '==> [swanstation] shared core ABI checked; runtime behavior requires console validation'
