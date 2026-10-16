#!/usr/bin/env bash
# Optional melonDS DS core: upstream sources pinned in tooling/melondsds/sources.json.
# Run alone, or set PS5_WITH_MELONDSDS=1 when building the title.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
source "$root/tools/core-stamp.sh"
core_stamp_skip melondsds \
    "$root/build/cores/stage/cores/melondsds_libretro.so" \
    "$root/build/cores/stage/info/melondsds_libretro.info" \
    "$root/build/cores/stage/info/melondsds-LICENSE.txt" \
    -- "$root/tools/build-melondsds.sh" "$root/tooling/melondsds"
[[ $# == 0 ]] || { echo "usage: ${0##*/}" >&2; exit 2; }
sdk="$root/.deps/native/ps5-payload-sdk"
[[ -x $sdk/bin/prospero-clang ]] || { echo "error: bootstrap this project's SDK first" >&2; exit 2; }
export PS5_PAYLOAD_SDK="$sdk" PS5_CLANG=/usr/bin/clang
python3 "$root/tooling/melondsds/prepare.py"
work="$root/build/cores/melondsds"
mkdir -p "$work/empty-libs" "$root/build/cores/stage/cores" "$root/build/cores/stage/info"
for name in m rt pthread dl util c++; do
    [[ -f $work/empty-libs/lib$name.a ]] || "$sdk/bin/prospero-ar" rc "$work/empty-libs/lib$name.a"
done
cmake -S "$work/source" -B "$work/ps5-build" -G Ninja \
    -C "$work/dependencies.cmake" \
    -DCMAKE_TOOLCHAIN_FILE="$root/tooling/melondsds/ps5-toolchain.cmake" \
    ${core_ccache:+-DCMAKE_C_COMPILER_LAUNCHER=$core_ccache -DCMAKE_CXX_COMPILER_LAUNCHER=$core_ccache} \
    -DCMAKE_BUILD_TYPE=Release -DPS5=ON -DPS5_PORT_DIR="$root/tooling/melondsds" \
    -DENABLE_JIT=ON -DENABLE_THREADED_RENDERER=ON -DENABLE_OPENGL=OFF \
    -DENABLE_COMPUTE_RENDERER=OFF -DENABLE_NETWORKING=ON -DENABLE_DYNAMIC=OFF \
    -DENABLE_LTO=OFF -DENABLE_LTO_RELEASE=OFF -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=OFF
cmake --build "$work/ps5-build" --target melondsds_libretro --parallel "${JOBS:-4}"
built="$work/ps5-build/src/libretro/melondsds_libretro.so"
python3 "$root/tools/check-core.py" "$built" --report "$work/abi.json"
cp -- "$built" "$root/build/cores/stage/cores/melondsds_libretro.so"
cp -- "$work/ps5-build/melondsds_libretro.info" "$root/build/cores/stage/info/melondsds_libretro.info"
cp -- "$work/ps5-build/melondsds-LICENSE.txt" "$root/build/cores/stage/info/melondsds-LICENSE.txt"
python3 - "$root" <<'PY'
import hashlib, json, pathlib, sys
root = pathlib.Path(sys.argv[1]); work = root/'build/cores/melondsds'
report = json.loads((work/'abi.json').read_text())
report['sources'] = json.loads((work/'source-revisions.json').read_text())
report['source_revision'] = report['sources']['melondsds'][1]
report['source_submodules'] = {name: {'url': url, 'revision': revision}
    for name, (url, revision) in report['sources'].items() if name != 'melondsds'}
report['port_inputs_sha256'] = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
    for p in [root/'tools/build-melondsds.sh', *sorted((root/'tooling/melondsds').glob('*'))]
    if p.is_file()}
report.update(jit=True, fastmem=False, renderer='software', microphone='libretro frontend API')
(work/'build.json').write_text(json.dumps(report, indent=2)+'\n')
PY
core_stamp_write
echo '==> [melondsds] built and ABI checked; microphone behavior requires console validation'
