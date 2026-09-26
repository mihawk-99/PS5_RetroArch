# Cross-compile LRPS2's libretro core with this repository's SDK; never search
# host headers or libraries.
#
# The pinned tree is configured directly, as the Dolphin and PPSSPP builds
# configure theirs. Everything the port changes in the core is in
# patches/lrps2/ps5-port.patch (made in ../PS5_LRPS2); what the build adds from
# outside the tree is here and in tools/build-lrps2.sh.
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
# pkg-config would find the host's libraries: every dependency is the bundled one.
set(PKG_CONFIG_EXECUTABLE "/bin/false" CACHE FILEPATH "" FORCE)

# The console's CPU is a Zen 2: one AVX2 build for it, rather than the three
# MultiISA copies chosen at run time (DISABLE_ADVANCE_SIMD). The core has no
# BMI2 intrinsics, the one extension Zen 2 runs slowly.
set(ARCH_FLAG "-march=znver2" CACHE STRING "" FORCE)

# Link-only probes must actually resolve functions; static probes give false
# positives. Cross CMake never executes these binaries.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib -nostartfiles -nodefaultlibs -Wl,-e,0 -lkernel_web -lSceLibcInternal -lScePosixForWebKit")

# -Dstatic_assert=_Static_assert and -DZSTD_TRACE=0: the same shims the Dolphin
# and PPSSPP builds use (see tooling/ppsspp/ps5-toolchain.cmake).
set(CMAKE_C_FLAGS_INIT "-O2 -fPIC -w -Dstatic_assert=_Static_assert -DZSTD_TRACE=0")
set(CMAKE_CXX_FLAGS_INIT "-O2 -fPIC -w -DZSTD_TRACE=0")

# The core's link contract, the same one every other native core uses: no host
# CRT or libc, undefined symbols resolved later by the title's binding table,
# 16 KiB-page segments from the shared linker script, a reproducible build id.
set(CMAKE_SHARED_LINKER_FLAGS_INIT
    "-nostdlib -nodefaultlibs -Wl,-z,undefs -Wl,--build-id=sha1 -Wl,-T,${CMAKE_CURRENT_LIST_DIR}/../native/ps5-core.ld ${PS5_CORE_LINK_INPUTS} -lkernel_web -lSceLibcInternal -lScePosixForWebKit")
