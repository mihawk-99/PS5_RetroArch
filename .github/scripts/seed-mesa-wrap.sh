#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Mesa's zlib subproject (its subprojects/zlib.wrap, which RADV's PS5 build takes with
# --force-fallback-for=zlib) for PS5_Vulkan's recipe: the wrap's source and patch put in the tree's
# subprojects/packagecache, where meson looks before it downloads. The wrap names zlib.net, and
# wrapdb.mesonbuild.com for the patch with no fallback; WrapDB's GitHub releases carry both files,
# checked here against the wrap's own hashes. The recipe keeps packagecache when it refreshes its tree.
#
#   .github/scripts/seed-mesa-wrap.sh <PS5_Vulkan checkout> <PS5_Mesa checkout>
set -euo pipefail
vulkan=${1:?usage: seed-mesa-wrap.sh <PS5_Vulkan> <PS5_Mesa>}
mesa=${2:?usage: seed-mesa-wrap.sh <PS5_Vulkan> <PS5_Mesa>}
revision=$(sed -n 's/^mesa_revision=\([0-9a-f]\{40\}\).*/\1/p' "$vulkan/tools/build-radv.sh")
[[ -n $revision ]] || { echo "seed-mesa-wrap: no mesa_revision in $vulkan/tools/build-radv.sh" >&2; exit 2; }
wrap=$(git -C "$mesa" show "$revision:subprojects/zlib.wrap")
field() { sed -n "s/^$1 *= *//p" <<<"$wrap" | head -n 1; }
release=$(field wrapdb_version)
[[ -n $release ]] || { echo "seed-mesa-wrap: the wrap at ${revision:0:12} has no wrapdb_version" >&2; exit 2; }
cache=$vulkan/.deps/work/radv-src/subprojects/packagecache
downloads=${CI_DOWNLOADS:-$HOME/.cache/ps5-retroarch-ci}/mesa-wrap
mkdir -p "$cache" "$downloads"
for kind in source patch; do
    name=$(field "${kind}_filename")
    hash=$(field "${kind}_hash")
    [[ -n $name && $hash =~ ^[0-9a-f]{64}$ ]] || { echo "seed-mesa-wrap: no ${kind}_filename or ${kind}_hash" >&2; exit 2; }
    if ! printf '%s  %s\n' "$hash" "$downloads/$name" | sha256sum --check --strict --status 2>/dev/null; then
        curl --fail --location --retry 3 --silent --show-error -o "$downloads/$name.download" \
            "https://github.com/mesonbuild/wrapdb/releases/download/zlib_$release/$name"
        mv -- "$downloads/$name.download" "$downloads/$name"
    fi
    printf '%s  %s\n' "$hash" "$downloads/$name" | sha256sum --check --strict
    cp -- "$downloads/$name" "$cache/$name"
done
echo "==> [mesa-wrap] zlib $release's source and patch in $cache"
