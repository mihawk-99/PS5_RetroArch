/*
 * PS5 RetroArch - libc entry points LRPS2's core calls and the console lacks.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Each function here is referenced by LRPS2 or by a library it vendors, and
 * cannot come from the title's import table: the payload SDK exports no symbol
 * for it, or its stub lists one the console does not provide at runtime. They
 * are defined in the core so the reference resolves where it is made. The
 * pattern is the Dolphin port's (tooling/dolphin/ps5-libc-shims.cpp).
 *
 * A real implementation, because the core depends on the answer:
 *
 *   atexit        glslang registers its finalizer. The process-wide list would
 *                 call into the core after RetroArch unmapped it, so it joins
 *                 this core's own destructor registry
 *                 (tooling/native/core_cxx_runtime.cpp), which the native loader
 *                 runs before unmapping.
 *
 *   clock_nanosleep
 *                 Granite's timer (paraLLEl-GS) sleeps to an absolute deadline.
 *                 The console has clock_gettime and nanosleep, so the deadline is
 *                 turned into the interval still to go.
 *
 * Refusals the callers already handle, for features a console core never uses:
 *
 *   getaddrinfo, freeaddrinfo           DEV9's network adapter (name lookups)
 *   if_nameindex, if_freenameindex      DEV9's adapter enumeration
 */
#include <cerrno>
#include <cstddef>
#include <ctime>

#include <net/if.h>
#include <netdb.h>

extern "C"
{
    int __cxa_atexit(void (*callback)(void *), void *argument, void *dso);
    extern void *__dso_handle;
}

namespace
{
void run_atexit(void *function)
{
    reinterpret_cast<void (*)()>(function)();
}
} // namespace

extern "C"
{
    int atexit(void (*function)())
    {
        return __cxa_atexit(run_atexit, reinterpret_cast<void *>(function), &__dso_handle);
    }

    int clock_nanosleep(clockid_t clock, int flags, const struct timespec *request,
                        struct timespec *remaining)
    {
        struct timespec wait = *request;
        if (flags & TIMER_ABSTIME)
        {
            struct timespec now;
            if (clock_gettime(clock, &now) != 0)
                return errno;
            const long long left = (static_cast<long long>(request->tv_sec) - now.tv_sec) * 1000000000LL +
                                   (request->tv_nsec - now.tv_nsec);
            if (left <= 0)
                return 0;
            wait.tv_sec = static_cast<time_t>(left / 1000000000LL);
            wait.tv_nsec = static_cast<long>(left % 1000000000LL);
            /* An absolute sleep reports no remainder: the deadline is unchanged. */
            remaining = nullptr;
        }
        return nanosleep(&wait, remaining) == 0 ? 0 : errno;
    }

    int getaddrinfo(const char *, const char *, const struct addrinfo *, struct addrinfo **result)
    {
        if (result)
            *result = nullptr;
        return EAI_FAIL;
    }

    void freeaddrinfo(struct addrinfo *)
    {
    }

    struct if_nameindex *if_nameindex()
    {
        errno = ENOSYS;
        return nullptr;
    }

    void if_freenameindex(struct if_nameindex *)
    {
    }
}
