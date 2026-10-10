/* PS5 RetroArch - resolve the installation once, after cooperative elevation.
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ps5_paths.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#ifndef PS5_PATHS_HOST_TEST
#include <ps5platform/elevation.h>
#endif

static char install_root[PATH_MAX] = "/app0";

const char *ps5_install_root(void)
{
    return install_root;
}

const char *ps5_installed_path(const char *path, char *buffer, size_t capacity)
{
    if (!path || strncmp(path, "/app0", 5) || (path[5] && path[5] != '/') ||
        !strcmp(install_root, "/app0"))
        return path;
    const int length = snprintf(buffer, capacity, "%s%s", install_root, path + 5);
    if (length < 0 || (size_t)length >= capacity)
    {
        errno = ENAMETOOLONG;
        return NULL;
    }
    return buffer;
}

#if !defined(PS5_PATHS_HOST_TEST) || defined(PS5_PATHS_START_TEST)
int ps5_paths_start(void)
{
    /* Keep the log descriptor across the root transition. Open before the
     * client's seteuid, so it does not retain the new credential. */
    FILE *log = fopen("/app0/startup-paths.log", "w");
    struct stat original;
    char metadata[4096] = {0}, id[10] = {0};
    FILE *file = fopen("/app0/sce_sys/param.json", "rb");
    if (file)
    {
        fread(metadata, 1, sizeof(metadata) - 1, file);
        fclose(file);
    }
    char *key = strstr(metadata, "\"titleId\"");
    char *colon = key ? strchr(key + 9, ':') : NULL;
    char *value = colon ? strchr(colon, '"') : NULL;
    if (!value || strlen(value) < 11 || value[10] != '"' || stat("/app0/eboot.bin", &original) != 0)
        goto failed;
    memcpy(id, value + 1, 9);
    for (unsigned i = 0; i < 9; i++)
        if (!((id[i] >= 'A' && id[i] <= 'Z') || (id[i] >= '0' && id[i] <= '9')))
            goto failed;

    /* A qualified daemon publishes readiness after its preflight and removes it
     * on exit. Do not publish a request at all on an ordinary offline launch.
     * A stale PID or a marker from a later uptime (before a reboot) is not ready. */
    long daemon_pid = -1, daemon_started = 0;
    struct timespec uptime;
    file = fopen("/app0/lapy-ready", "r");
    int ready = file && fscanf(file, "%ld %ld", &daemon_pid, &daemon_started) == 2;
    if (file)
        fclose(file);
    ready = ready && daemon_pid > 1 && clock_gettime(CLOCK_MONOTONIC, &uptime) == 0 &&
            uptime.tv_sec >= daemon_started;
    if (ready)
    {
        errno = 0;
        ready = kill((pid_t)daemon_pid, 0) == 0 || errno == EPERM;
    }
    if (!ready)
    {
        if (log)
        {
            fprintf(log, "elevation=unavailable offline=1 root=/app0\n");
            fclose(log);
        }
        return 0;
    }
    const enum ps5_elevation_status status = ps5_elevation_request(PS5_ELEVATION_FILESYSTEM);
    /* /data may already be mounted by the launcher. The SDK's filesystem proof
     * alone is not evidence of elevation; wait for the actual UID transition. */
    for (unsigned i = 0; status == PS5_ELEVATION_OK && getuid() != 0 && i < 200; i++)
        usleep(50000);
    if (log)
    {
        fprintf(log, "elevation=%s uid=%d\n", ps5_elevation_status_name(status), getuid());
        fflush(log);
    }
    if (status != PS5_ELEVATION_OK || getuid() != 0)
        goto failed;

    const char *formats[] = {"/data/homebrew/%s", "/mnt/sandbox/%s_000/app0", "/system_ex/app/%s"};
    for (unsigned i = 0; i < sizeof(formats) / sizeof(formats[0]); i++)
    {
        char root[PATH_MAX], image[PATH_MAX];
        struct stat candidate;
        snprintf(root, sizeof(root), formats[i], id);
        snprintf(image, sizeof(image), "%s/eboot.bin", root);
        /* Do not select a different installation just because its title ID
         * matches. These aliases must name the very same executable. */
        if (stat(image, &candidate) != 0 || candidate.st_ino != original.st_ino ||
            candidate.st_dev != original.st_dev)
            continue;
        strcpy(install_root, root);
        if (chdir(root) != 0)
            goto failed;
        if (log)
        {
            fprintf(log, "root=%s same_executable=1\n", root);
            fclose(log);
        }
        /* Drivers also consume paths without going through RetroArch's settings. */
        snprintf(image, sizeof(image), "%s/radv-shader-cache", root);
        setenv("MESA_SHADER_CACHE_DIR", image, 1);
        snprintf(image, sizeof(image), "%s/sce_sys/param.json", root);
        setenv("PS5VK_PARAM_JSON", image, 1);
        return 0;
    }
failed:
    if (log)
    {
        fprintf(log, "startup refused: elevation or installation could not be verified; errno=%d\n",
                errno);
        fclose(log);
    }
    return -1;
}
#endif
