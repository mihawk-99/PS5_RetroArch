# Cross-compile PPSSPP with this repository's SDK; never search host headers/libraries.
#
# The pinned tree is configured directly (not through add_subdirectory): upstream
# resolves module paths and source lists through ${CMAKE_SOURCE_DIR}, so it has to be
# the top-level project. Everything this port adds to the build lives in this file
# and in patches/ppsspp/.
#
# The compiler is the payload SDK's own wrapper rather than tooling/prospero-clang18:
# it is the one that knows where the SDK's linker script (prx.script) lives, and it
# is what every other core build in this repository uses.
set(CMAKE_SYSTEM_NAME FreeBSD)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER "$ENV{PS5_PAYLOAD_SDK}/bin/prospero-clang")
set(CMAKE_CXX_COMPILER "$ENV{PS5_PAYLOAD_SDK}/bin/prospero-clang++")
set(CMAKE_AR "$ENV{PS5_PAYLOAD_SDK}/bin/prospero-ar")
set(CMAKE_RANLIB "$ENV{PS5_PAYLOAD_SDK}/bin/prospero-ranlib")
set(CMAKE_FIND_ROOT_PATH "$ENV{PS5_PAYLOAD_SDK}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Read by the pinned tree's CMakeLists guards (patch 0002) and by this file.
set(PPSSPP_PS5 ON CACHE BOOL "" FORCE)

# (The core does ask for a VK_KHR_display surface - build-ppsspp.sh sets
# USE_VULKAN_DISPLAY_KHR - which libretro_vulkan.cpp answers with the frontend's.)
# No X11 and no Wayland: a libretro core never creates a surface or a swapchain, the
# frontend owns presentation, and neither library exists in the payload SDK sysroot.
# Left on, USING_X11_VULKAN puts X11_Xlib_INCLUDE_PATH-NOTFOUND into the include list
# and fails generation.
set(USING_X11_VULKAN OFF CACHE BOOL "" FORCE)

# Link-only probes must actually resolve functions; static probes give false positives.
# Cross CMake never executes these binaries, so no payload startup is required. The
# -nodefaultlibs is what stops the SDK wrapper from adding -lc -lSceNet of its own.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib -nostartfiles -nodefaultlibs -Wl,-e,0 -lkernel_web -lSceLibcInternal -lScePosixForWebKit")

# -Dstatic_assert=_Static_assert is a shim for the SDK's headers, not a preference.
# PPSSPP's own C flags include -D_XOPEN_SOURCE=700; on this FreeBSD-derived libc that
# pins __ISO_C_VISIBLE to 1990, so <assert.h> never declares the C11 static_assert
# that vendored C libraries (ext/xxhash.h among them) assume. _Static_assert is a
# compiler keyword in C11 and later, so the rename is always available. It is set for
# C only: C++ gets static_assert as a keyword and needs no macro.
#
# -DZSTD_TRACE=0 is the same switch tools/build-retroarch.sh uses for the frontend:
# zstd emits weak tracing hooks whenever it sees GNUC+ELF+x86-64, and this title's
# converter refuses a symbol no public SDK stub exports, so the hooks must not exist.
set(CMAKE_C_FLAGS_INIT "-O2 -fPIC -w -Dstatic_assert=_Static_assert -DZSTD_TRACE=0")
set(CMAKE_CXX_FLAGS_INIT "-O2 -fPIC -w -DZSTD_TRACE=0")

# The core's link contract, the same one every other native core uses: no host CRT or
# libc, undefined symbols permitted at this stage and resolved later by the title's
# generated binding table, 16 KiB-page segments from the shared linker script, and a
# reproducible build id. PS5_CORE_LINK_INPUTS carries this core's destructor registry
# object, which build-ppsspp.sh compiles because upstream's version script hides it.
set(CMAKE_SHARED_LINKER_FLAGS_INIT
    "-nostdlib -nodefaultlibs -Wl,-z,undefs -Wl,--build-id=sha1 -Wl,-T,${CMAKE_CURRENT_LIST_DIR}/../native/ps5-core.ld ${PS5_CORE_LINK_INPUTS} -lkernel_web -lSceLibcInternal -lScePosixForWebKit")
