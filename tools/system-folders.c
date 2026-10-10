/* PS5 RetroArch - the system folders of a staged title (host tool, tools/build-title.sh).
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   system-folders <title folder>
 *
 * Makes <title folder>/content/<system> for each system the staged cores run, with the
 * same code the title runs at each start (ps5_library_make_system_folders), so a fresh
 * install has them in its package. Prints how many it made. */
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

#include "ps5_library.h"

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fputs("usage: system-folders <title folder>\n", stderr);
        return 2;
    }
    char content[4096], info[4096], cores[4096];
    snprintf(content, sizeof(content), "%s/content", argv[1]);
    snprintf(info, sizeof(info), "%s/info", argv[1]);
    snprintf(cores, sizeof(cores), "%s/cores", argv[1]);
    if (mkdir(content, 0777) != 0 && errno != EEXIST)
    {
        perror(content);
        return 1;
    }
    const int made = ps5_library_make_system_folders(content, info, cores);
    if (made < 0)
        return 1;
    printf("%d\n", made);
    return 0;
}
