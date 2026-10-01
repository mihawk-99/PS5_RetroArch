# Cross-compile Flycast with this repository's SDK; never search host headers/libraries.
#
# The pinned tree is configured directly (not through add_subdirectory): upstream
# resolves module paths and source lists through ${CMAKE_SOURCE_DIR}, so it has to be
# the top-level project. Everything this port adds to the build lives in this file
# and in patches/flycast/.
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

# Read by the pinned tree's CMakeLists guards (patches/flycast/flycast-ps5.patch):
# PROSPERO skips --no-undefined (core imports resolve through the title's
# generated binding table, not at link time) and fixes PAGE_SIZE at 16 KiB, the
# only size the native core loader maps.
set(PROSPERO ON CACHE BOOL "" FORCE)
set(PS5 ON CACHE BOOL "" FORCE)

# Link-only probes must actually resolve functions; static probes give false positives.
# Cross CMake never executes these binaries, so no payload startup is required. The
# -nodefaultlibs is what stops the SDK wrapper from adding -lc -lSceNet of its own.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib -nostartfiles -nodefaultlibs -Wl,-e,0 -lkernel_web -lSceLibcInternal -lScePosixForWebKit")

# -DZSTD_TRACE=0 is the same switch tools/build-retroarch.sh uses for the frontend
# and tools/build-ppsspp.sh uses for PPSSPP: zstd emits weak tracing hooks whenever
# it sees GNUC+ELF+x86-64, and the title's import table does not resolve them.
#
# -frtti -fexceptions: Flycast uses both, and the prospero target's defaults keep
# them off.
set(CMAKE_C_FLAGS_INIT "-O2 -fPIC -w -DZSTD_TRACE=0")
set(CMAKE_CXX_FLAGS_INIT "-O2 -fPIC -w -DZSTD_TRACE=0 -frtti -fexceptions")
set(CMAKE_ASM_FLAGS_INIT "-w")

# The core's link contract, the same one every other native core uses: no host CRT or
# libc, undefined symbols permitted at this stage and resolved later by the title's
# generated binding table, 16 KiB-page segments from the shared linker script, and a
# reproducible build id. PS5_CORE_LINK_INPUTS carries this core's extra objects, which
# build-flycast.sh compiles:
#   core_cxx_runtime.o    the local destructor registry, whose fini-array callback
#                         has to run before the native loader unmaps the module
#   ps5-stubs.o           the entry points Flycast references on paths that never
#                         run here and the import table does not provide
#   ps5-libcxx-inst.o     std::stringbuf::str(str), inline-only in the headers and
#                         absent from the import table
#   future/memory/system_error/thread.o  the libc++ archive members std::future
#                         needs; the full libc++.a is NOT linked because its
#                         locale/io members reference a FreeBSD-14 surface
#                         (catgets, *_l locale functions, *at syscalls) the table
#                         does not provide
set(CMAKE_SHARED_LINKER_FLAGS_INIT
    "-nostdlib -nodefaultlibs -Wl,-z,undefs -Wl,--build-id=sha1 -Wl,-T,${CMAKE_CURRENT_LIST_DIR}/../native/ps5-core.ld ${PS5_CORE_LINK_INPUTS} -lkernel_web -lSceLibcInternal -lScePosixForWebKit")

# libzip's feature checks link cleanly against the SDK archives but resolve to
# Annex K and Win32 names the title's import table does not provide; libzip's own
# compat.h fallbacks cover every one of them, so the probes are pinned off.
foreach(_check
        HAVE_MEMCPY_S HAVE_STRNCPY_S HAVE_STRERROR_S HAVE_STRERRORLEN_S
        HAVE_SNPRINTF_S HAVE_LOCALTIME_S HAVE_CLONEFILE HAVE_EXPLICIT_BZERO
        HAVE_EXPLICIT_MEMSET HAVE_FTS_OPEN HAVE_SETMODE HAVE_STRICMP
        HAVE__CLOSE HAVE__DUP HAVE__FDOPEN HAVE__FILENO HAVE__SETMODE
        HAVE__SNPRINTF HAVE__SNPRINTF_S HAVE__SNWPRINTF_S HAVE__STRDUP
        HAVE__STRICMP HAVE__STRTOI64 HAVE__STRTOUI64 HAVE__UNLINK)
    set(${_check} FALSE CACHE BOOL "" FORCE)
endforeach()
