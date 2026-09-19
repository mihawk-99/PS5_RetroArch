/*
 * ps5-native-app-boilerplate - Native application startup.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Initializes the platform runtime, runs static constructors and main, and
 * hands normal process termination back to the platform runtime.
 */

#include <cstddef>
#include <cstdint>

using Destructor = void (*)();
using Initializer = void (*)();

extern "C"
{
    void _init_env(void *process_parameters);
    int atexit(Destructor callback);
    [[noreturn]] void exit(int status);
    int application_main(int argc, char **argv, char **envp) __asm__("main");

    /* Provided by tooling/native/ps5-pie.ld: the range app_crt.cpp clears before
     * anything can read a zero-initialised static. */
    extern char __bss_start[];
    extern char __bss_end[];

    extern Initializer __preinit_array_start[] __attribute__((weak));
    extern Initializer __preinit_array_end[] __attribute__((weak));
    extern Initializer __init_array_start[] __attribute__((weak));
    extern Initializer __init_array_end[] __attribute__((weak));
    extern Initializer __fini_array_start[] __attribute__((weak));
    extern Initializer __fini_array_end[] __attribute__((weak));
}

namespace
{
/* The console's loader does not zero the BSS, and the tail of this image's
 * writable segment is BSS: the file image is 0x104b4 bytes where the segment is
 * 0xc7040, so about three quarters of a megabyte of zero-initialised objects -
 * RetroArch's, the Vulkan driver's and the shader compiler's - started with
 * whatever the previous owner of that memory left behind.
 *
 * Found through one of those objects: RetroArch's signal-handler counter, whose
 * value the display context reads to decide whether the user asked to quit, read
 * -285230512 before any signal had been delivered. The runloop ended on its first
 * iteration and rarch_main returned 0 without drawing a frame. Now the range the
 * linker script marks is cleared before anything can read it. */
void zero_bss() noexcept
{
    for (char *at = __bss_start; at != __bss_end; at++)
        *at = 0;
}

void run_forward(Initializer *first, Initializer *last) noexcept
{
    if (first == nullptr || last == nullptr)
        return;
    while (first != last)
        (*first++)();
}

void run_reverse(Initializer *first, Initializer *last) noexcept
{
    if (first == nullptr || last == nullptr)
        return;
    while (last != first)
        (*--last)();
}
} // namespace

extern "C" __attribute__((weak)) void catchReturnFromMain(int status)
{
    (void)status;
}

extern "C" void _init()
{
    run_forward(__preinit_array_start, __preinit_array_end);
    run_forward(__init_array_start, __init_array_end);
}

extern "C" void _fini()
{
    run_reverse(__fini_array_start, __fini_array_end);
}

extern "C" [[noreturn]] __attribute__((visibility("default"))) void
_start(void *process_parameters, Destructor loader_teardown)
{
    const int argc = *static_cast<const int *>(process_parameters);
    auto *parameters = static_cast<std::uint8_t *>(process_parameters);
    auto **argv = reinterpret_cast<char **>(parameters + sizeof(std::uint64_t));

    /* Before anything else: every static this program will read lives in the BSS
     * the loader left alone, and _init_env may well be the first code to use one. */
    zero_bss();

    _init_env(process_parameters);
    if (loader_teardown != nullptr)
        (void)atexit(loader_teardown);
    (void)atexit(_fini);
    _init();
    const int status = application_main(argc, argv, nullptr);
    catchReturnFromMain(status);
    exit(status);
}
