/*
 * PS5 RetroArch - libc functions a core imports that the console's SDK does not
 * resolve, or resolves to something that faults: gmtime_r, arc4random,
 * statvfs and utimensat.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PPSSPP's FFmpeg (libavutil's time formatting) calls the reentrant form; the
 * SDK has the plain gmtime, whose result lives in one static buffer. The copy is
 * taken under a lock, so concurrent callers each get their own conversion.
 */

#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <time.h>

uint64_t sceKernelReadTsc(void);

static pthread_mutex_t gmtime_lock = PTHREAD_MUTEX_INITIALIZER;

struct tm *gmtime_r(const time_t *time, struct tm *result)
{
    pthread_mutex_lock(&gmtime_lock);
    const struct tm *shared = gmtime(time);
    if (shared)
        memcpy(result, shared, sizeof(*result));
    pthread_mutex_unlock(&gmtime_lock);
    return shared ? result : NULL;
}

/* arc4random: FFmpeg's configure finds it on a FreeBSD target, and libavutil
 * seeds its random helper with it; the console resolves the import to nothing.
 * Nothing here needs cryptographic strength, so it is a splitmix64 generator
 * seeded once from the timestamp counter. Bound to cores by tools/core-imports.py
 * under its own name: a title-defined arc4random would be an export, which the
 * title converter refuses. */
static uint64_t random_state;

uint32_t ps5_arc4random(void)
{
    uint64_t state = __atomic_load_n(&random_state, __ATOMIC_RELAXED);
    if (state == 0)
        state = sceKernelReadTsc() | 1;
    state += UINT64_C(0x9e3779b97f4a7c15);
    __atomic_store_n(&random_state, state, __ATOMIC_RELAXED);
    uint64_t z = state;
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return (uint32_t)(z ^ (z >> 31));
}

/* statvfs: the console libc's own jumps to an unmapped address (0x10_002629b6)
 * from inside libc -- it is built on statfs, which only libkernel_sys carries.
 * PPSSPP asks for the free space of its save directory whenever a game checks
 * that its save data fits (sceUtilitySavedata GETSIZE), and Yu-Gi-Oh! GX Tag
 * Force crashed there at every start (2026-09-24). No query this title can make
 * reports the data partition's free space, so this answers a writable
 * filesystem with 16 GiB free, which is what those checks need: room.
 * Bound to cores by tools/core-imports.py under its own name. */
int ps5_statvfs(const char *path, struct statvfs *result)
{
    (void)path;
    if (result == NULL)
        return -1;
    memset(result, 0, sizeof(*result));
    result->f_bsize = 32768;
    result->f_frsize = 32768;
    result->f_blocks = (fsblkcnt_t)(UINT64_C(64) << 30) / 32768;
    result->f_bfree = (fsblkcnt_t)(UINT64_C(16) << 30) / 32768;
    result->f_bavail = result->f_bfree;
    result->f_namemax = 255;
    return 0;
}

/* utimensat: libc++'s std::filesystem::last_write_time(path, time) sets a file's
 * times with it, and Dolphin's core links that setter (its file utilities), so
 * the title's libc++ archive needs it; the SDK exports no such symbol. The
 * console's libkernel has utimes, which takes microseconds. Only what those
 * callers pass is supported: paths relative to the working directory
 * (AT_FDCWD), both times given or both left to "now". */
int utimensat(int directory, const char *path, const struct timespec times[2], int flags)
{
    (void)flags;
    if (directory != AT_FDCWD)
    {
        errno = ENOSYS;
        return -1;
    }
    if (times == NULL || (times[0].tv_nsec == UTIME_NOW && times[1].tv_nsec == UTIME_NOW))
        return utimes(path, NULL);
    if (times[0].tv_nsec == UTIME_OMIT || times[1].tv_nsec == UTIME_OMIT ||
        times[0].tv_nsec == UTIME_NOW || times[1].tv_nsec == UTIME_NOW)
    {
        errno = ENOSYS;
        return -1;
    }
    struct timeval values[2];
    for (int index = 0; index < 2; ++index)
    {
        values[index].tv_sec = times[index].tv_sec;
        values[index].tv_usec = times[index].tv_nsec / 1000;
    }
    return utimes(path, values);
}
