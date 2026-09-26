/*
 * PS5 RetroArch - the libc functions the title's own code and its libc++ link
 * by their standard names, where the console has none that works: gmtime_r
 * and utimensat. The implementations are the platform layer's (my payload SDK
 * fork, include/ps5platform/libc.h); these give them the standard names inside
 * the title. A core imports them through tools/core-imports.py instead.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <ps5platform/libc.h>

/* PPSSPP's FFmpeg (libavutil's time formatting) calls the reentrant form; the
 * console's libc has only gmtime. */
struct tm *gmtime_r(const time_t *time, struct tm *result)
{
    return ps5_gmtime_r(time, result);
}

/* libc++'s std::filesystem::last_write_time(path, time) sets a file's times
 * with it, and Dolphin's file utilities link that setter. */
int utimensat(int directory, const char *path, const struct timespec times[2], int flags)
{
    return ps5_utimensat(directory, path, times, flags);
}
