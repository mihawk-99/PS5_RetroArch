#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# zlib for a checkout whose tools/setup-native-dependencies.sh builds it (this one, PS5_Vulkan):
# the tarball put where that script looks for it, so it does not download it from
# zlib.net, which can answer a CI runner with something other than the tarball. zlib's GitHub release
# carries the same file; it is checked against the script's own pin (zlib_version, zlib_hash), and
# kept in CI_DOWNLOADS.
#
#   .github/scripts/seed-zlib.sh <checkout>
set -euo pipefail
checkout=${1:?usage: seed-zlib.sh <checkout>}
script=$checkout/tools/setup-native-dependencies.sh
[[ -f $script ]] || { echo "seed-zlib: no $script" >&2; exit 2; }
version=$(sed -n 's/^zlib_version="\([0-9.]*\)".*/\1/p' "$script")
hash=$(sed -n 's/^zlib_hash="\([0-9a-f]\{64\}\)".*/\1/p' "$script")
[[ -n $version && -n $hash ]] || { echo "seed-zlib: no zlib_version or zlib_hash in $script" >&2; exit 2; }
target=$checkout/.deps/native/zlib/zlib-$version.tar.gz
mkdir -p "$(dirname -- "$target")"
if printf '%s  %s\n' "$hash" "$target" | sha256sum --check --strict --status 2>/dev/null; then
    echo "==> [zlib] $target is zlib $version"
    exit 0
fi
cache=${CI_DOWNLOADS:-$HOME/.cache/ps5-retroarch-ci}/zlib-$version.tar.gz
mkdir -p "$(dirname -- "$cache")"
if ! printf '%s  %s\n' "$hash" "$cache" | sha256sum --check --strict --status 2>/dev/null; then
    curl --fail --location --retry 3 --silent --show-error -o "$cache.download" \
        "https://github.com/madler/zlib/releases/download/v$version/zlib-$version.tar.gz"
    mv -- "$cache.download" "$cache"
fi
printf '%s  %s\n' "$hash" "$cache" | sha256sum --check --strict
cp -- "$cache" "$target"
echo "==> [zlib] $target: zlib $version from its GitHub release"
