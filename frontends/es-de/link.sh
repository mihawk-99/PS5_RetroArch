# PS5 RetroArch - what EmulationStation's executable links beyond SDL2 and OpenGL.
# Sourced by tools/build-frontend.sh (es-de); tools/build-esde.sh builds every
# archive named here first: ES-DE's own libraries from its CMake build, the
# libraries tools/build-*.sh build, and the pinned stb headers its FreeImage
# functions (ps5/freeimage_stb.cpp) are built over.
# shellcheck shell=bash disable=SC2034
PS5_SDL2="$sdl" PS5_OPENGL_SDK="$gl_sdk" bash "$root/tools/build-esde.sh"
PS5_CLANG=${PS5_CLANG:-/usr/bin/clang} bash "$root/tools/build-libsmb2.sh" ps5
deps="$root/.deps/native"
esde="$root/build/es-de/src"
extra_includes=("$root/frontends/es-de/ps5/include" "$deps/pugixml-ps5/include"
    "$deps/libsmb2-ps5/include" "$deps/ffmpeg-esde-ps5/include"
    "${PS5_LIBRETRO_INCLUDE:-$root/vendor/retroarch/libretro-common/include}")
extra_system_includes=("$root/.deps/stb-2c980bb59875b0d32144a71867fbdebb2f77cd20" "$deps/libwebp-ps5/include")
extra_archives=(
    "$deps/libsmb2-ps5/lib/libsmb2.a"
    "$esde/libes-de.a" "$esde/libes-core.a" "$esde/liblunasvg.a" "$esde/libplutovg.a" "$esde/librlottie.a"
    "$deps/ffmpeg-esde-ps5/lib/libavformat.a" "$deps/ffmpeg-esde-ps5/lib/libavfilter.a"
    "$deps/ffmpeg-esde-ps5/lib/libavcodec.a" "$deps/ffmpeg-esde-ps5/lib/libswscale.a"
    "$deps/ffmpeg-esde-ps5/lib/libswresample.a" "$deps/ffmpeg-esde-ps5/lib/libavutil.a"
    "$deps/curl-ps5/lib/libcurl.a" "$deps/curl-ps5/lib/libmbedtls.a" "$deps/curl-ps5/lib/libmbedx509.a"
    "$deps/curl-ps5/lib/libmbedcrypto.a"
    "$deps/harfbuzz-ps5/lib/libharfbuzz.a" "$deps/freetype-ps5/lib/libfreetype.a"
    "$deps/icu-ps5/lib/libicuuc.a" "$deps/icu-ps5/lib/libicudata.a"
    "$deps/pugixml-ps5/lib/libpugixml.a" "$deps/libwebp-ps5/lib/libwebpdecoder.a"
)
# ES-DE presents its frames through SDL_GL_SwapWindow: ps5/capture_ps5.cpp sees each
# one first, for the armed captures (tools/run-title.sh --frontend-capture).
extra_link_flags=(--wrap=SDL_GL_SwapWindow --wrap=getprotobyname --wrap=getlogin_r)
