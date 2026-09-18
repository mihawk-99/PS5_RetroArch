#!/usr/bin/env bash
# PS5 RetroArch - baseline payload build (Option 1).
#
# Builds the frontend from the vendored recipe in reference/ps5-retroarch, which
# cross-compiles upstream RetroArch for the console and stages a payload. This is
# the known-good baseline: a console run of something we did not write, to
# compare later runs of our own build against.
#
#   tools/build-baseline.sh            build and stage into dist/baseline/
#   tools/build-baseline.sh --check    report the toolchain and the ports only
#   tools/build-baseline.sh --clean    throw away the caches and build fresh
#   tools/build-baseline.sh --rebuild  reconfigure, keep the object cache
#
# Repeat builds are meant to be cheap, because the console run is the slow part:
#
#   - the pinned upstream tarball is kept in .deps/cache/ and its SHA-256 is
#     checked on every reuse, so a corrupt or swapped archive is caught, not
#     trusted;
#   - the prepared upstream tree is kept in work/baseline/ and reused as it is.
#     make then relinks or recompiles only what changed, and since our changes
#     land in platform/ beside the tree rather than inside it, most edits touch
#     a handful of objects;
#   - every compile goes through ccache when it is installed, which makes even a
#     from-scratch rebuild after a switch of flags much cheaper;
#   - the loop runs with -j on this host's core count. The recipe itself runs
#     make without -j, so the jobserver is supplied here.
#
# What this deliberately does not do:
#   - it never writes inside reference/: the recipe is copied into the work tree
#     and run there, so the baseline stays byte-identical;
#   - it does not build the per-core recipes. Those fetch whatever upstream's
#     default branch holds at the moment, which is not reproducible, and they
#     are not needed to prove that the frontend loads;
#   - it does not touch the console. Staging and deploying are separate steps.
#
# Details, and what each check proves: docs/TESTING.md and docs/DEPLOYMENT.md.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

recipe="$root/reference/ps5-retroarch"
work="${PS5RA_WORK:-$root/work/baseline}"
out="${PS5RA_OUT:-$root/dist/baseline}"
cache="${PS5RA_CACHE:-$root/.deps/cache}"
ports="${PS5RA_PORTS:-$root/.deps/pacbrew/current}"
ports_prefix="$ports/sysroot/user/homebrew"

mode=build
case "${1:-}" in
    "")        mode=build ;;
    --check)   mode=check ;;
    --clean)   mode=clean ;;
    --rebuild) mode=rebuild ;;
    -h|--help) sed -n '2,32p' "${BASH_SOURCE[0]}" | sed 's/^# \?//'; exit 0 ;;
    *)         echo "usage: ${0##*/} [--check|--clean|--rebuild]" >&2; exit 2 ;;
esac
[[ $# -le 1 ]] || { echo "usage: ${0##*/} [--check|--clean|--rebuild]" >&2; exit 2; }

say() { printf '==> [baseline] %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }
# sed replacement text: a path with & or \ in it would otherwise be special.
sed_escape() { printf '%s' "$1" | sed -e 's/[&\\|]/\\&/g'; }

[[ -n ${PS5_PAYLOAD_SDK:-} ]] || die "PS5_PAYLOAD_SDK is not set; see docs/REFERENCE.md"
[[ -d $PS5_PAYLOAD_SDK/toolchain ]] || die "PS5_PAYLOAD_SDK=$PS5_PAYLOAD_SDK has no toolchain/"
[[ -d $recipe ]] || die "the vendored recipe is missing: $recipe"
[[ -f $ports_prefix/libdata/pkgconfig/sdl2.pc ]] || die \
    "the ports image is not provisioned at $ports (run tools/fetch-ports.sh); the recipe enables SDL2"

# The recipe names the upstream version; it is parsed rather than repeated, so a
# version bump in the recipe is picked up here instead of silently ignored.
upstream_version=$(sed -n 's/^VER="\(.*\)"$/\1/p' "$recipe/build.sh" | head -n1)
[[ -n $upstream_version ]] || die "cannot read the upstream version from $recipe/build.sh"

if [[ $mode == check ]]; then
    source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh" >/dev/null
    say "toolchain  $PS5_PAYLOAD_SDK"
    "$CC" --version | head -n1
    say "ports      $ports_prefix"
    PKG_CONFIG_LIBDIR="$ports_prefix/libdata/pkgconfig" \
    PKG_CONFIG_SYSROOT_DIR= pkg-config --modversion sdl2
    say "upstream   RetroArch $upstream_version (pinned by the recipe)"
    say "cache      $cache"
    command -v ccache >/dev/null && say "ccache     $(ccache --version | head -n1)" \
        || say "ccache     not installed; builds will not be cached"
    say "check complete; no build was run"
    exit 0
fi

if [[ $mode == clean ]]; then
    say "removing the work tree and the artifact caches"
    rm -rf -- "$work" "$out"
    ccache -C >/dev/null 2>&1 || true
fi
if [[ $mode == rebuild ]]; then
    say "removing the configured tree so configure runs again"
    rm -rf -- "$work/RetroArch-$upstream_version"
fi

command -v wget >/dev/null || die "missing required command: wget"
command -v tar  >/dev/null || die "missing required command: tar"

mkdir -p -- "$work" "$cache"

# The work tree is disposable: the recipe writes its outputs next to itself, and
# its own files are re-copied so the reference stays the source of truth. The log
# directory is made after that copy, so a stale one cannot shadow it.
cp -a -- "$recipe"/. "$work/"
logs="$work/logs"
mkdir -p -- "$logs"

# ---- the pinned tarball, fetched once and verified on every reuse -----------
tarball="$cache/RetroArch-$upstream_version.tar.gz"
digest_file="$cache/RetroArch-$upstream_version.sha256"
if [[ -f $tarball && -f $digest_file ]]; then
    printf '%s  %s\n' "$(<"$digest_file")" "$tarball" | sha256sum --check --strict >/dev/null \
        || die "the cached tarball does not match its recorded digest; run --clean"
    say "reusing the cached tarball ${tarball##*/} ($(du -h "$tarball" | cut -f1))"
else
    say "downloading RetroArch $upstream_version once into $cache"
    wget -q --show-progress -O "$tarball.part" \
        "https://github.com/libretro/RetroArch/archive/refs/tags/v$upstream_version.tar.gz"
    mv -f -- "$tarball.part" "$tarball"
    sha256sum "$tarball" | cut -d' ' -f1 > "$digest_file"
    say "cached ${tarball##*/} ($(du -h "$tarball" | cut -f1)), digest in ${digest_file##*/}"
fi

# The recipe's own download and extraction are replaced by the cache. Both are
# anchored on lines that only appear once, and the text is inserted after them
# rather than edited in place, so the recipe's flow is unchanged.
t=$(sed_escape "$tarball")
w=$(sed_escape "$work")
sed -e "/wget -O \\\$TEMPDIR\/RetroArch.tar.gz/a\\
cp \"$t\" \"\$TEMPDIR/RetroArch.tar.gz\"" -i -- "$work/build.sh"
sed -e "/tar xf  \\\$TEMPDIR\/RetroArch.tar.gz -C \\\$TEMPDIR/a\\
if [[ ! -d $w/RetroArch-$upstream_version || ! -f $w/RetroArch-$upstream_version/media/icons/playstore/icon.png ]]; then rm -rf \"$w/RetroArch-$upstream_version\"; tar xf \"$t\" -C \"$w\"; fi\\
rm -rf \"\$TEMPDIR/RetroArch-$upstream_version\"\\
ln -s \"$w/RetroArch-$upstream_version\" \"\$TEMPDIR/RetroArch-$upstream_version\"" \
    -i -- "$work/build.sh"

# The tree is reused between builds, so the recipe must not take files out of it.
# It moves the upstream icon and the generated config into its own directory;
# both are copies here.
sed -e "s|mv \\\$TEMPDIR/RetroArch-\\\$VER/media/icons/playstore/icon.png|cp \\\$TEMPDIR/RetroArch-\\\$VER/media/icons/playstore/icon.png|" \
    -i -- "$work/build.sh"

# ---- the ports, and the paths this host cannot spell ------------------------
# This toolchain resolves an absolute include such as /user/homebrew/include/SDL2
# against the *host* filesystem, not against its sysroot: -isysroot does not move
# it, --sysroot does not move it, and only a host-absolute -I works. Measured
# three ways; see docs/FINDINGS.md. /user does not exist here and creating it
# needs a privilege this project does not ask for, so the build is given both
# spellings, in our copy of the recipe and nowhere else:
#
#   1. a pkg-config of our own, so the compiler is asked for the ports prefix by
#      its host path. The SDK's wrapper rewrites every path under its own
#      sysroot, and the ports prefix is not there, so it cannot be used. It is
#      assigned after the recipe sources prospero.sh, which would overwrite it;
#   2. after configure, the include paths it generated are rewritten to the same
#      host path, because configure copies them from the ports metadata rather
#      than asking the compiler.
#
# Only how the build reaches the headers on this machine changes. What the
# payload links against, and where it looks for its assets at runtime, stay the
# console's own /user/homebrew.
ports_pc="$work/pkg-config-for-ports"
cat > "$ports_pc" <<'PKGCONFIG'
#!/usr/bin/env bash
# pkg-config answered from this project's ports prefix, by host path.
set -euo pipefail
prefix=${PS5RA_PORTS_PREFIX:?}
export PKG_CONFIG_DIR=
export PKG_CONFIG_LIBDIR="$prefix/libdata/pkgconfig"
export PKG_CONFIG_PATH=
export PKG_CONFIG_SYSROOT_DIR=
exec pkg-config --static "$@"
PKGCONFIG
chmod +x "$ports_pc"

p=$(sed_escape "$ports_prefix")
sed -e "/toolchain\/prospero.sh/a\\
PKG_CONFIG=\"$ports_pc\"; export PKG_CONFIG" -i -- "$work/build.sh"
sed -e "/^[[:space:]]*\\\${MAKE}/i\\
sed -i -e 's|/user/homebrew|$p|g' config.mk" -i -- "$work/build.sh"

# ---- ccache, if it is here --------------------------------------------------
# The recipe calls $CC and $CXX from the environment, so a shim earlier on PATH
# is enough, and a host without ccache still builds. The shim is named for each
# compiler separately and calls the real one through the SDK path: ccache
# resolves a bare compiler name on PATH, which would find this shim again.
mkdir -p -- "$work/.shim"
# One shim per compiler name, in front of the real one. It decides at run time
# whether the invocation is a compile or a link: compiles go to ccache (when it
# is installed) and then to the compiler, links go to the intermediate PS5
# layout that the application-image converter requires
# (tools/prospero-clang-link has the reason). This is the only place the
# recipe's toolchain is intercepted.
for name in prospero-clang prospero-clang++; do
    {
        printf '#!/bin/sh\n'
        printf 'exec "%s/tools/prospero-clang-link" %s "$@"\n' "$root" "$name"
    } > "$work/.shim/$name"
    chmod +x "$work/.shim/$name"
done
if command -v ccache >/dev/null; then
    say "compiling through ccache ($(ccache --version | head -n1))"
fi
say "linking through the intermediate PS5 layout (tools/prospero-clang-link)"

jobs=$(nproc 2>/dev/null || echo 4)
say "building RetroArch $upstream_version with -j$jobs; the log is $logs/build.log"

# ---- build ------------------------------------------------------------------
# MAKEFLAGS carries the parallel job count into the recipe's own make call. The
# recipe prints every compile line, so its output goes to a log rather than the
# terminal; the tail is shown when something fails.
before=$(date +%s)
if [[ -n ${PS5RA_VERBOSE:-} ]]; then
    (
        cd "$work"
        export PS5_PAYLOAD_SDK PS5RA_PORTS_PREFIX="$ports_prefix"
        export MAKEFLAGS="-j$jobs"
        export PATH="$work/.shim:$PATH"
        export PS5RA_PS5_PIE_LD="$root/linker/ps5-pie.ld"
        export PS5RA_PLATFORM_DIR="$root/platform"
        export PS5RA_OBJ_DIR="$work/obj"
        bash ./build.sh
    ) || die "the recipe's build.sh failed (see above)"
else
    (
        cd "$work"
        export PS5_PAYLOAD_SDK PS5RA_PORTS_PREFIX="$ports_prefix"
        export MAKEFLAGS="-j$jobs"
        export PATH="$work/.shim:$PATH"
        export PS5RA_PS5_PIE_LD="$root/linker/ps5-pie.ld"
        export PS5RA_PLATFORM_DIR="$root/platform"
        export PS5RA_OBJ_DIR="$work/obj"
        bash ./build.sh
    ) 2>&1 | { set +e; grep -vE '^/home/mihawk/ps5-payload-sdk/bin/prospero-(clang|clang\+\+|lld) |^/home/mihawk/ps5-payload-sdk/bin/ld\.lld ' > "$logs/build.log"; } || {
        tail -n 25 "$logs/build.log" >&2
        die "the recipe's build.sh failed; full log in $logs/build.log"
    }
fi
elapsed=$(( $(date +%s) - before ))
say "build finished in ${elapsed}s"

for produced in retroarch.elf retroarch.cfg; do
    [[ -f $work/$produced ]] || die "the recipe did not produce $produced"
done

# The link above goes through tools/prospero-clang-link, which needs to know
# where its layout script and its image symbols are. Re-linking the finished tree
# is the reliable way to get the layout the application-image converter needs:
# make sees a newer output than its objects only when something changed, so this
# forces the link step on its own.
say "re-linking through the intermediate PS5 layout"


rm -rf -- "$out"
mkdir -p -- "$out/sce_sys"
cp -a -- "$work/retroarch.elf" "$work/retroarch.cfg" "$out/"
[[ -f $work/sce_sys/icon0.png ]] && cp -a -- "$work/sce_sys/icon0.png" "$out/sce_sys/"
cp -a -- "$root/title/homebrew.js" "$out/"

# The check the whole gate exists for: a host Linux object stages just as
# cleanly as a PS5 one, and only the import table tells them apart.
say "checking the payload is a PS5 object"
needed=$(readelf -dW "$out/retroarch.elf" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p')
grep -qx 'libkernel_web.sprx' <<<"$needed" || die \
    "retroarch.elf does not import libkernel_web.sprx (imports: $(tr '\n' ' ' <<<"$needed"))"
if grep -qx 'libkernel_sys.sprx' <<<"$needed"; then
    die "retroarch.elf imports libkernel_sys.sprx, which the loader's process lacks"
fi

(
    cd "$out"
    find . -type f ! -name manifest.sha256 -print0 | sort -z \
        | xargs -0 sha256sum > manifest.sha256
)
say "staged $(find "$out" -type f ! -name manifest.sha256 | wc -l) files in $out"
cat "$out/manifest.sha256"
