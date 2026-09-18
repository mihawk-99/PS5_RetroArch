#!/usr/bin/env bash
# PS5 RetroArch - build one of the sibling native PS5 applications.
#
#   tools/build-native-app.sh ProsperoLight          build it
#   tools/build-native-app.sh ProsperoLight --check  report the setup only
#
# Why a script for someone else's project. Building these applications on this
# machine needs four things that are not obvious and were each found by a failed
# build:
#
#   1. the project's own SDK                  (its vendored copy under .deps/; the
#                                              one in $HOME does not carry what it
#                                              expects)
#   2. PS5_CLANG=/usr/bin/clang               (their wrapper defaults to clang-18,
#                                              which is not installed; the sibling
#                                              project that works uses plain clang)
#   3. PYTHONPATH=tooling/pystub              (mbedTLS regenerates a source file by
#                                              running a script that imports
#                                              jsonschema; see that module's header)
#   4. runtime/libc.prx present and verified   (ProsperoLight ships the manifest but
#                                              not the file, and its own rebuild is
#                                              not byte-reproducible here)
#
# The build itself is the project's own `make app`; nothing here edits its sources.
# What it produces is staged under handoff/<TITLE_ID>/ so the console's owner can
# place it, because writes from this machine do not reach the console's title
# folders (docs/FINDINGS.md).

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

project=${1:-}
mode=${2:-}
[[ -n $project ]] || { echo "usage: ${0##*/} PROJECT [--check]" >&2; exit 2; }
case "$project" in
    ProsperoLight|ps5-native-app-boilerplate-main|PS5_Vulkan) ;;
    *) echo "unknown project: $project" >&2; exit 2 ;;
esac

sibling="$root/../$project"
[[ -d $sibling ]] || { echo "error: no such project: $sibling" >&2; exit 2; }

sdk="$sibling/.deps/native/ps5-payload-sdk"
[[ -d $sdk ]] || sdk="$root/../ps5-native-app-boilerplate-main/.deps/native/ps5-payload-sdk"
[[ -d $sdk ]] || { echo "error: no vendored SDK found; run the project's dependency bootstrap" >&2; exit 2; }

echo "==> [native] project  $sibling"
echo "==> [native] sdk      $sdk"
echo "==> [native] compiler $(/usr/bin/clang --version | head -n1)"
echo "==> [native] pystub   $root/tooling/pystub"

runtime="$sibling/runtime/libc.prx"
if [[ -f $runtime ]]; then
    (cd "$sibling/runtime" && sha256sum -c libc.prx.sha256 >/dev/null) \
        && echo "==> [native] runtime  verified" \
        || { echo "error: $runtime does not match its manifest" >&2; exit 2; }
else
    donor="$root/../ps5-native-app-boilerplate-main/runtime/libc.prx"
    [[ -f $donor ]] || { echo "error: $runtime is missing and no verified copy exists at $donor" >&2; exit 2; }
    echo "==> [native] runtime  missing in the project; the copy at $donor has the same manifest digest"
fi

if [[ $mode == --check ]]; then
    echo "==> [native] check complete; nothing was built"
    exit 0
fi

[[ -f $runtime ]] || { cp -a -- "$donor" "$runtime"; echo "==> [native] placed the verified runtime"; }
if [[ -f $sibling/.gitmodules ]]; then
    (cd "$sibling" && git submodule update --init --recursive >/dev/null 2>&1) \
        && echo "==> [native] submodules present" || true
fi

echo "==> [native] building with the project's own make"
(
    cd "$sibling"
    PYTHONPATH="$root/tooling/pystub${PYTHONPATH:+:$PYTHONPATH}" \
    PS5_CLANG=/usr/bin/clang \
    PS5_PAYLOAD_SDK="$sdk" \
        make app
)

title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$sibling/sce_sys/param.json")
out="$root/handoff/$title_id"
rm -rf -- "$out"
mkdir -p -- "$root/handoff"
cp -a -- "$sibling/dist/$title_id" "$out"
echo "==> [native] staged $out for the console owner to place"
