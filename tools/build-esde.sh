#!/usr/bin/env bash
# Build EmulationStation Desktop Edition (ES-DE) 3.5.0's libraries for the PS5
# (docs/FRONTENDS.md). tools/build-frontend.sh es-de runs this first, from
# frontends/es-de/link.sh, and links the executable from what it leaves.
#
#   1. its dependencies, each a tools/build-*.sh of this project with its own pins:
#      FreeType, HarfBuzz, ICU, pugixml, curl over mbedTLS, FFmpeg with the
#      filters ES-DE uses, and libwebp's decoder;
#   2. the stb headers its FreeImage functions are built over
#      (frontends/es-de/ps5/freeimage_stb.cpp), pinned by commit and digest;
#   3. ES-DE at v3.5.0, pinned by commit, exported into build/es-de/src as a git
#      repository whose one commit is the release, with frontends/es-de/patches
#      applied (the series file names them, in order) as its working-tree diff;
#   4. CMake and Ninja over it with the PS5 toolchain: libes-de.a, libes-core.a,
#      liblunasvg.a, libplutovg.a and librlottie.a, in build/es-de/src;
#   5. the Alekfull NX theme (anthonycaccese's ES-DE port of fagnerpc's theme, CC
#      BY-NC-SA 2.0), pinned by commit, in .deps/alekfull-nx-es-de: the theme the
#      picker's EmulationStation card shows (docs/FRONTENDS.md).
#
# The workspace is where the port is edited: change build/es-de/src, then export
# its diff with
#   git -C build/es-de/src diff > frontends/es-de/patches/0001-ps5-port.patch
# A workspace is re-exported only when the patches changed, and never while it
# holds edits the patches do not (build/es-de/.stamp records the diff it was given).
#
# Inputs: PS5_SDL2 and PS5_OPENGL_SDK, the SDL2 SDK and OpenGL SDK prefixes
# (tools/build-frontend.sh chooses and passes them).
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
sdl=${PS5_SDL2:?PS5_SDL2 names the SDL2 SDK (tools/build-frontend.sh es-de passes it)}
gl_sdk=${PS5_OPENGL_SDK:?PS5_OPENGL_SDK names the OpenGL SDK (tools/build-frontend.sh es-de passes it)}
esde_version=v3.5.0
esde_commit=50e4b600ae533d772bae3ff880d11a09b05dbe84
stb_commit=2c980bb59875b0d32144a71867fbdebb2f77cd20
alekfull_commit=645b2bcc2248e93af8f0f4461a6ee7367fb89365
sdk="$root/.deps/native/ps5-payload-sdk"
[[ -x $sdk/bin/prospero-clang ]] || { echo "error: bootstrap this project's SDK first" >&2; exit 2; }

for dependency in freetype harfbuzz icu pugixml curl ffmpeg-esde libwebp; do
    bash "$root/tools/build-$dependency.sh"
done

stb="$root/.deps/stb-$stb_commit"
mkdir -p "$stb"
while read -r digest file; do
    [[ -f $stb/$file ]] ||
        curl --fail --location --retry 3 "https://raw.githubusercontent.com/nothings/stb/$stb_commit/$file" \
            -o "$stb/$file"
    printf '%s  %s\n' "$digest" "$stb/$file" | sha256sum --check --status ||
        { echo "error: digest mismatch for $stb/$file" >&2; exit 1; }
done <<'EOF'
594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3 stb_image.h
cbd5f0ad7a9cf4468affb36354a1d2338034f2c12473cf1a8e32053cb6914a05 stb_image_write.h
173e654634f6ccaad98f603e686ea212eec1fe8ea6d2a5e5e8056efa10ae3880 stb_image_resize2.h
bebfe904b14301657e4e5d655c811d51fd31b97c455b9cc2d8600d6bac6cff63 LICENSE
EOF

clone="$root/.deps/es-de"
[[ -d $clone/.git ]] || git clone --quiet --depth 1 --branch "$esde_version" \
    https://gitlab.com/es-de/emulationstation-de.git "$clone"
[[ $(git -C "$clone" rev-parse HEAD) == "$esde_commit" ]] ||
    { echo "error: $clone is not ES-DE $esde_version ($esde_commit)" >&2; exit 1; }

alekfull="$root/.deps/alekfull-nx-es-de"
if [[ ! -d $alekfull/.git ]]; then
    git init --quiet "$alekfull"
    git -C "$alekfull" fetch --quiet --depth 1 https://github.com/anthonycaccese/alekfull-nx-es-de.git "$alekfull_commit"
    git -C "$alekfull" checkout --quiet FETCH_HEAD
fi
[[ $(git -C "$alekfull" rev-parse HEAD) == "$alekfull_commit" ]] ||
    { echo "error: $alekfull is not the Alekfull NX theme at $alekfull_commit" >&2; exit 1; }

# The other shipped themes (frontends/es-de/themes.list), each at its pinned commit.
while read -r pin repository; do
    [[ $pin == theme_* ]] || continue
    commit=${pin#*=}
    theme="$root/.deps/esde-themes/$(basename "$repository")"
    if [[ ! -d $theme/.git ]]; then
        git init --quiet "$theme"
        git -C "$theme" fetch --quiet --depth 1 "$repository.git" "$commit"
        git -C "$theme" checkout --quiet FETCH_HEAD
    fi
    [[ $(git -C "$theme" rev-parse HEAD) == "$commit" ]] ||
        { echo "error: $theme is not $repository at $commit" >&2; exit 1; }
done < "$root/frontends/es-de/themes.list"

port="$root/frontends/es-de"
patches=()
while read -r patch; do
    [[ -z $patch || $patch == \#* ]] || patches+=("$port/patches/$patch")
done < "$port/patches/series"
series_digest=$(cat "${patches[@]}" | sha256sum | cut -c1-64)
work="$root/build/es-de"
source_dir="$work/src"
stamp_file="$work/.stamp"
diff_digest() { git -C "$source_dir" diff | sha256sum | cut -c1-64; }

# The stamp is "<commit> <series digest> <digest of the diff the export left>".
read -r stamped_commit stamped_series stamped_diff < <(cat "$stamp_file" 2>/dev/null || echo "- - -")
if [[ -d $source_dir/.git && $stamped_commit == "$esde_commit" ]]; then
    current_diff=$(diff_digest)
    if [[ $stamped_series == "$series_digest" ]]; then
        [[ $current_diff == "$stamped_diff" ]] ||
            echo "==> [es-de] note: build/es-de/src has edits the patches do not hold yet"
        export_needed=0
    elif [[ $current_diff == "$series_digest" ]]; then
        # The patch was just exported from this workspace: it is what the series says.
        printf '%s %s %s\n' "$esde_commit" "$series_digest" "$current_diff" > "$stamp_file"
        export_needed=0
    elif [[ $current_diff == "$stamped_diff" ]]; then
        export_needed=1
    else
        echo "error: frontends/es-de/patches changed, and build/es-de/src has edits they do not hold;" \
            "export the workspace's diff or remove build/es-de/src" >&2
        exit 1
    fi
else
    export_needed=1
fi

if ((export_needed)); then
    echo "==> [es-de] exporting ES-DE $esde_version and applying ${#patches[@]} patch(es)"
    rm -rf -- "$source_dir"
    mkdir -p "$source_dir"
    git -C "$clone" archive "$esde_commit" | tar -x -C "$source_dir"
    git -C "$source_dir" init --quiet
    git -C "$source_dir" add -A
    git -C "$source_dir" -c user.name=ps5-retroarch -c user.email=ps5-retroarch@localhost \
        commit --quiet -m "ES-DE $esde_version (${esde_commit:0:8})"
    for patch in "${patches[@]}"; do
        git -C "$source_dir" apply "$patch"
    done
    printf '%s %s %s\n' "$esde_commit" "$series_digest" "$(diff_digest)" > "$stamp_file"
fi

echo "==> [es-de] CMake and Ninja (log in build/es-de/build.log)"
export PS5_PAYLOAD_SDK="$sdk" PS5_CLANG=${PS5_CLANG:-/usr/bin/clang}
cmake -S "$source_dir" -B "$work/cmake" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$sdk/toolchain/prospero.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DGL=ON -DGLES=OFF -DAPPLICATION_UPDATER=OFF -DCOMPILE_LOCALIZATIONS=OFF \
    -DBUNDLED_CERTS=ON -DDEINIT_ON_LAUNCH=ON -DPS5_PORT_DIR="$port/ps5" -DPS5_DEPS="$root/.deps/native" \
    -DPS5_SDL2="$sdl" -DPS5_GL_SDK="$gl_sdk" -DPS5_STB="$stb" \
    -DCMAKE_C_FLAGS=-march=znver2 -DCMAKE_CXX_FLAGS=-march=znver2 > "$work/configure.log" 2>&1 ||
    { tail -30 "$work/configure.log" >&2; exit 1; }
ninja -C "$work/cmake" lunasvg rlottie es-core es-de > "$work/build.log" 2>&1 ||
    { grep -E "error" "$work/build.log" | head -20 >&2; exit 1; }
printf '==> [es-de] %s: libes-de.a %s, libes-core.a %s\n' "$esde_version" \
    "$(du -h "$source_dir/libes-de.a" | cut -f1)" "$(du -h "$source_dir/libes-core.a" | cut -f1)"
