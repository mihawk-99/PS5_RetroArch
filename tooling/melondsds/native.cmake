# Match the title's native loader and the other C++ cores' destructor ownership.
get_filename_component(PS5_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
target_sources(melondsds_libretro PRIVATE
    "${PS5_PROJECT_ROOT}/tooling/native/core_cxx_runtime.cpp")
target_include_directories(melondsds_libretro PRIVATE "${melonDS_SOURCE_DIR}/src/net")
target_link_options(melondsds_libretro PRIVATE -Wl,-m,elf_x86_64_fbsd)
target_link_libraries(melondsds_libretro PRIVATE
    -Wl,--no-as-needed -lkernel_web -lSceLibcInternal -lScePosixForWebKit)
