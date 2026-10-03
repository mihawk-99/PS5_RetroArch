# Cross-compile SwanStation's libretro core with this repository's SDK; never
# search host headers/libraries. Same contract as tooling/flycast's file: the
# pinned tree is configured directly as the top-level project.
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

# Read by the pinned tree's guards (patches/swanstation/swanstation-ps5.patch):
# the port routes the JIT code buffer through ps5platform/exec.h and avoids
# syscalls a PS5 title sandbox refuses.
set(PROSPERO ON CACHE BOOL "" FORCE)
set(PS5 ON CACHE BOOL "" FORCE)

# Link-only probes must actually resolve functions; static probes give false
# positives. Cross CMake never executes these binaries.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib -nostartfiles -nodefaultlibs -Wl,-e,0 -lkernel_web -lSceLibcInternal -lScePosixForWebKit")

# Threads live in the same libc as everything else on this target; the package
# probe's -lpthread is not a library that exists here.
set(THREADS_PREFER_PTHREAD_FLAG ON)
set(CMAKE_THREAD_LIBS_INIT "" CACHE STRING "" FORCE)
set(CMAKE_USE_PTHREADS_INIT ON CACHE BOOL "" FORCE)

# -DZSTD_TRACE=0 is the same switch the frontend and the other cores use: zstd
# emits weak tracing hooks the title's import table does not resolve.
# SwanStation uses exceptions and RTTI, off by default on the prospero target.
set(CMAKE_C_FLAGS_INIT "-O2 -fPIC -w -DZSTD_TRACE=0")
set(CMAKE_CXX_FLAGS_INIT "-O2 -fPIC -w -DZSTD_TRACE=0 -frtti -fexceptions")
set(CMAKE_ASM_FLAGS_INIT "-w")

# The core's link contract: no host CRT or libc, undefined symbols permitted
# at this stage and resolved later by the title's generated binding table,
# 16 KiB-page segments from the shared linker script. PS5_CORE_LINK_INPUTS
# carries this core's extra objects (core_cxx_runtime.o, the stubs).
set(CMAKE_SHARED_LINKER_FLAGS_INIT
    "-nostdlib -nodefaultlibs -Wl,-z,undefs -Wl,--build-id=sha1 -Wl,-T,${CMAKE_CURRENT_LIST_DIR}/../native/ps5-core.ld ${PS5_CORE_LINK_INPUTS} -lkernel_web -lSceLibcInternal -lScePosixForWebKit")
