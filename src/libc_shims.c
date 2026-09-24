/*
 * PS5 RetroArch - libc functions a core imports that the console's SDK does not
 * resolve: gmtime_r and arc4random.
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
