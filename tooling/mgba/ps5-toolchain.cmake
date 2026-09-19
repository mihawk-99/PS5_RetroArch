# Cross-compile with this repository's SDK; never search host headers/libraries.
set(CMAKE_SYSTEM_NAME FreeBSD)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER "$ENV{PS5_PAYLOAD_SDK}/bin/prospero-clang")
set(CMAKE_CXX_COMPILER "$ENV{PS5_PAYLOAD_SDK}/bin/prospero-clang++")
set(CMAKE_AR "$ENV{PS5_PAYLOAD_SDK}/bin/prospero-ar")
set(CMAKE_RANLIB "$ENV{PS5_PAYLOAD_SDK}/bin/prospero-ranlib")
# Link-only probes must actually resolve functions; static probes give false positives.
# Cross CMake never executes these binaries, so no payload startup is required.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib -nostartfiles -nodefaultlibs -Wl,-e,0 -lkernel_web -lSceLibcInternal -lScePosixForWebKit")
set(CMAKE_FIND_ROOT_PATH "$ENV{PS5_PAYLOAD_SDK}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
