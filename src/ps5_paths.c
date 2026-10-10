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
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
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
#ifndef PS5_LAPY_LOADER_PORT
#define PS5_LAPY_LOADER_PORT 9021
#endif
static int service_ready(void)
{
    long pid = -1, started = 0;
    struct timespec uptime;
    FILE *file = fopen("/app0/lapy-ready", "r");
    int ready = file && fscanf(file, "%ld %ld", &pid, &started) == 2;
    if (file)
        fclose(file);
    if (!ready || pid <= 1 || started < 0 || clock_gettime(CLOCK_MONOTONIC, &uptime) ||
        uptime.tv_sec < started)
        return 0;
    errno = 0;
    return kill((pid_t)pid, 0) == 0 || errno == EPERM;
}

/* The same local ELF loader used by the WebUI, before any worker threads exist.
 * Send once only: an uncertain delivery must never trigger a second daemon. */
static int start_service(FILE *log)
{
    FILE *file = fopen("/app0/lapy-root-daemon.elf", "rb");
    struct stat info;
    if (!file)
    {
        if (log)
            fprintf(log, "lapy_autostart open_failed errno=%d\n", errno);
        return 0;
    }
    /* Do not use the SDK's inline fileno macro on the native libc's FILE. */
    if (stat("/app0/lapy-root-daemon.elf", &info) || info.st_size < 64 || info.st_size > (4 << 20))
    {
        if (log)
            fprintf(log, "lapy_autostart invalid_size errno=%d\n", errno);
        fclose(file);
        return 0;
    }
    const size_t size = (size_t)info.st_size;
    char *elf = malloc(size);
    int valid = elf && fread(elf, 1, size, file) == size && !memcmp(elf, "\177ELF", 4);
    fclose(file);
    if (!valid)
    {
        if (log)
            fprintf(log, "lapy_autostart invalid_elf errno=%d\n", errno);
        free(elf);
        return 0;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct timeval timeout = {5, 0};
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(PS5_LAPY_LOADER_PORT);
    size_t sent = 0;
    if (fd >= 0 && setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0 &&
        connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0)
    {
        while (sent < size)
        {
            ssize_t count = send(fd, elf + sent, size - sent, MSG_NOSIGNAL);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
                break;
            sent += (size_t)count;
        }
        shutdown(fd, SHUT_WR);
    }
    const int error = sent == size ? 0 : errno;
    if (fd >= 0)
        close(fd);
    free(elf);
    if (log)
    {
        fprintf(log, "lapy_autostart sent=%zu size=%zu errno=%d\n", sent, size, error);
        fflush(log);
    }
    if (!sent)
        return 0;
    for (unsigned i = 0; i < 100; i++)
    {
        if (service_ready())
            return 1;
        usleep(50000);
    }
    return 0;
}

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

    /* Reuse a ready service, otherwise start the bundled one. A service publishes
     * readiness only after preflight; no request is made before that proof. */
    int ready = service_ready() || start_service(log);
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
