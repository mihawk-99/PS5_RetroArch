cmake_minimum_required(VERSION 3.16)
project(PS5_mGBA C CXX)
if(NOT MGBA_SOURCE_DIR)
    message(FATAL_ERROR "MGBA_SOURCE_DIR must name the pinned extracted source")
endif()
set(BUILD_LIBRETRO ON CACHE BOOL "" FORCE)
set(SKIP_LIBRARY ON CACHE BOOL "" FORCE)
set(DISABLE_DEPS ON CACHE BOOL "" FORCE)
set(BUILD_LTO OFF CACHE BOOL "" FORCE)
add_subdirectory("${MGBA_SOURCE_DIR}" mgba EXCLUDE_FROM_ALL)
# Use libretro XRGB8888 and the existing RGBA upload. Do not alter upstream files.
get_target_property(core_defines mgba_libretro COMPILE_DEFINITIONS)
list(REMOVE_ITEM core_defines COLOR_16_BIT COLOR_5_6_5 HAVE_XLOCALE)
# localtime_r is supplied by the frontend's locked rtime_localtime adapter.
list(APPEND core_defines HAVE_LOCALTIME_R MGBA_PS5)
set_property(TARGET mgba_libretro PROPERTY COMPILE_DEFINITIONS "${core_defines}")
# No payload CRT/static libc, host -lm, or other core-specific runtime library.
set_property(TARGET mgba_libretro PROPERTY LINK_LIBRARIES
    kernel_web SceLibcInternal ScePosixForWebKit)
# Permit imports implemented by the frontend (localtime_r); the title's generated
# binding table still has to resolve every import at link time.
target_link_options(mgba_libretro PRIVATE -nostdlib -nodefaultlibs -Wl,-z,undefs
    "-Wl,--build-id=sha1" "-Wl,-T,${CMAKE_CURRENT_LIST_DIR}/../native/ps5-core.ld")

target_include_directories(mgba_libretro PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
