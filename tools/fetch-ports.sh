#!/usr/bin/env bash
# PS5 RetroArch - prebuilt PS5 ports (PacBrew) provisioning.
#
# The baseline recipe enables SDL2, and SDL2 for the PS5 is not part of the
# payload SDK: it ships in PacBrew's prebuilt ports image, one large archive
# that carries the whole sysroot. This fetches exactly one pinned release,
# verifies its digest, and unpacks it into the ignored cache. It installs
# nothing system-wide and never modifies the SDK.
#
#   tools/fetch-ports.sh          fetch and unpack (idempotent)
#   tools/fetch-ports.sh --check  report what is cached, download nothing
#
# Pin provenance: release v0.40.2 of ps5-payload-dev/pacbrew-repo, the same
# release and digest already verified by this machine's sibling project
# ../PS5_Vulkan (tools/setup-pacbrew-dependencies.sh). The pin is recorded in
# docs/REFERENCE.md; changing it is a step with its own line in
# docs/PHASE_LOG.md.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

version=v0.40.2
url="https://github.com/ps5-payload-dev/pacbrew-repo/releases/download/$version/ps5-payload-dev.tar.gz"
hash=a85f65de418a8e6a898c6c3e3c870d50fff7618a200e4dd59ea9692af6ecec4d

cache="$root/.deps/pacbrew"
archive="$cache/ps5-payload-dev-$version.tar.gz"
release="$cache/$version"
sysroot="$release/sysroot"
current="$cache/current"

say() { printf '==> [ports] %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

case "${1:-}" in
    "")        check_only=0 ;;
    --check)   check_only=1 ;;
    -h|--help) sed -n '2,20p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *)         echo "usage: ${0##*/} [--check]" >&2; exit 2 ;;
esac
[[ $# -le 1 ]] || { echo "usage: ${0##*/} [--check]" >&2; exit 2; }

if (( check_only )); then
    [[ -f $sysroot/user/homebrew/libdata/pkgconfig/sdl2.pc ]] \
        && say "cached: $sysroot" \
        || { say "not cached"; exit 1; }
    [[ -L $current ]] && say "current -> $(readlink "$current")"
    exit 0
fi

for command in wget sha256sum tar; do
    command -v "$command" >/dev/null || die "missing required command: $command"
done

marker="$release/.complete"
expected_marker="$version $hash"
if [[ -f $marker && $(<"$marker") == "$expected_marker" \
      && -f $sysroot/user/homebrew/libdata/pkgconfig/sdl2.pc ]]; then
    say "already cached and verified: $sysroot"
else
    mkdir -p -- "$cache"
    if [[ ! -f $archive ]] \
       || ! printf '%s  %s\n' "$hash" "$archive" | sha256sum --check --strict >/dev/null 2>&1; then
        say "downloading $version (about 346 MB) from the pinned release"
        wget -q --show-progress "$url" -O "$archive.part"
        mv -f -- "$archive.part" "$archive"
    fi
    say "verifying the archive digest"
    printf '%s  %s\n' "$hash" "$archive" | sha256sum --check --strict

    say "unpacking the ports prefix into $release"
    # The archive is a complete /opt/ps5-payload-sdk tree, including a second
    # copy of the compiler and the SDK samples. Only the ports prefix is wanted,
    # so the extraction is limited to it: --strip-components=3 turns
    # opt/ps5-payload-sdk/target/user/homebrew into <release>/sysroot/user/homebrew.
    rm -rf -- "$release"
    mkdir -p -- "$release/sysroot"
    tar -xzf "$archive" -C "$release/sysroot" --strip-components=3 \
        opt/ps5-payload-sdk/target/user/homebrew
    [[ -d $sysroot/user/homebrew/include && -d $sysroot/user/homebrew/lib ]] \
        || die "the archive did not contain the expected ports prefix"
    printf '%s\n' "$expected_marker" > "$marker"
fi

ln -sfn -- "$version" "$current"
say "ready: $current -> $version"
say "SDL2: $(PKG_CONFIG_LIBDIR="$sysroot/user/homebrew/lib/pkgconfig" \
    PKG_CONFIG_PATH="$sysroot/user/homebrew/libdata/pkgconfig" \
    pkg-config --modversion sdl2)"
