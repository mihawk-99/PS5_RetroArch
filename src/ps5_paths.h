/* PS5 RetroArch - installed files and compatibility with saved /app0 paths.
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PS5_PATHS_H
#define PS5_PATHS_H
#include <limits.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    /* Once, before workers or frontend handover. A failed elevation must not be
     * followed by normal startup: the daemon may still be handling the request. */
    int ps5_paths_start(void);
    const char *ps5_install_root(void);
    /* Caller-owned storage: safe for concurrent cores and two-path operations.
     * Only the complete /app0 component is translated; NULL means overflow. */
    const char *ps5_installed_path(const char *path, char *buffer, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
