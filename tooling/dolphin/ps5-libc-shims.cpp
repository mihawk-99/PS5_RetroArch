/*
 * PS5 RetroArch - libc entry points Dolphin's core calls and the console lacks.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Each function here is referenced by Dolphin or by a library it vendors, and
 * none can come from the title's import table: the payload SDK exports no
 * symbol for it, or (the resolver and isatty, as the PPSSPP port found) its
 * stub lists one the console does not provide at runtime. They are defined in
 * the core so the reference resolves where it is made.
 *
 * Real implementations, because Dolphin depends on the answer:
 *
 *   atexit        VideoCommon/Spirv.cpp registers glslang's finalizer. The
 *                 process-wide list would call into the core after RetroArch
 *                 unmapped it, so it joins this core's own destructor registry
 *                 (tooling/native/core_cxx_runtime.cpp), which the native loader
 *                 runs before unmapping.
 *   __cxa_thread_atexit
 *                 thread_local destructors (Common, glslang). A thread that
 *                 outlives the core (the frontend's main thread) would call them
 *                 after the unmap, so they are not registered: the objects of a
 *                 thread are leaked when it exits, never run from freed code.
 *   newlocale, uselocale
 *                 Common/StringUtil.cpp formats and parses numbers in the C
 *                 locale. The console's libc has no locale objects and is always
 *                 in the C locale, so a stand-in handle is returned and switching
 *                 to it changes nothing.
 *   ___mb_cur_max libiconv's MB_CUR_MAX: 1 byte, the C locale's value.
 *   timegm        implot's time axis; a proleptic Gregorian UTC conversion.
 *
 * Refusals the callers already handle, for features a console core never uses:
 *
 *   fork, execvp, waitpid              imgui's "open in shell"
 *   mkdtemp                            Common's temporary directories (NetPlay,
 *                                      movie export)
 *   gethostbyname, getaddrinfo, freeaddrinfo, gai_strerror, getnameinfo,
 *   __res_init, __res_state            name resolution (the Wii network stack,
 *                                      enet, curl, SFML), unused offline
 *   isatty                             terminal detection for log colour: the
 *                                      title's output is a log file
 */
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>

#include <netdb.h>
#include <sys/types.h>

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

/* Days from 1970-01-01 to the given civil date (Howard Hinnant's algorithm). */
long long days_from_civil(long long year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const long long era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + static_cast<long long>(day_of_era) - 719468;
}

int c_locale_tag;

/* The resolver's state; __res_state returns a zeroed block no caller reads after
 * __res_init has failed. */
alignas(16) unsigned char resolver_state[1024];
} // namespace

extern "C"
{
    int atexit(void (*function)())
    {
        return __cxa_atexit(run_atexit, reinterpret_cast<void *>(function), &__dso_handle);
    }

    int __cxa_thread_atexit(void (*)(void *), void *, void *)
    {
        return 0;
    }

    void *newlocale(int, const char *, void *)
    {
        return &c_locale_tag;
    }

    void *uselocale(void *)
    {
        return &c_locale_tag;
    }

    int ___mb_cur_max()
    {
        return 1;
    }

    time_t timegm(struct tm *time)
    {
        /* Normalise the month into 0-11 first, as mktime would. */
        long long year = 1900LL + time->tm_year + time->tm_mon / 12;
        int month = time->tm_mon % 12;
        if (month < 0)
        {
            month += 12;
            --year;
        }
        const long long days = days_from_civil(year, static_cast<unsigned>(month + 1), 1) + time->tm_mday - 1;
        return static_cast<time_t>(((days * 24 + time->tm_hour) * 60 + time->tm_min) * 60 + time->tm_sec);
    }

    pid_t fork()
    {
        errno = ENOSYS;
        return -1;
    }

    int execvp(const char *, char *const[])
    {
        errno = ENOSYS;
        return -1;
    }

    pid_t waitpid(pid_t, int *, int)
    {
        errno = ECHILD;
        return -1;
    }

    char *mkdtemp(char *)
    {
        errno = ENOSYS;
        return nullptr;
    }

    struct hostent *gethostbyname(const char *)
    {
        return nullptr;
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

    const char *gai_strerror(int)
    {
        return "name resolution is not available in this core";
    }

    int getnameinfo(const struct sockaddr *, socklen_t, char *, size_t, char *, size_t, int)
    {
        return EAI_FAIL;
    }

    void *__res_state()
    {
        return resolver_state;
    }

    int __res_init()
    {
        return -1;
    }

    int isatty(int)
    {
        return 0;
    }
}
