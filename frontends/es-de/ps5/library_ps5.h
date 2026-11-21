/* PS5 RetroArch - EmulationStation's systems and game lists, from RetroArch's playlists
 * (frontends/es-de/ps5/library_ps5.cpp says how).
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PS5_RETROARCH_ESDE_LIBRARY_PS5_H
#define PS5_RETROARCH_ESDE_LIBRARY_PS5_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    struct ps5_esde_library_paths
    {
        const char *playlists, *info, *cores; /* RetroArch's */
        const char *reference;                /* ES-DE's own systems file */
        const char *data;                     /* ES-DE's data folder (ES-DE/) */
        const char *const *content;           /* content roots, NULL-ended; or NULL */
        const char *media; /* the shared media library (src/ps5_library.h); or NULL */
        int separate_systems; /* ES-DE only: one system per source playlist; default off */
    };

    /* Writes ES-DE's systems file and game lists from the library; the number of
     * systems, or -1 (a summary of what was done either way). */
    int ps5_esde_write_library_to(const struct ps5_esde_library_paths *paths, char *summary,
                                  size_t summary_size);
    /* The same, with the title's folders. */
    int ps5_esde_write_library(char *summary, size_t summary_size);

#ifdef __cplusplus
}
#endif

#endif
