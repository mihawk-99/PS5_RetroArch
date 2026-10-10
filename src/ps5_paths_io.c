/* PS5 RetroArch - legacy /app0 paths at the shared filesystem boundary.
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 * The SDK's directory and *at functions use these same operations underneath;
 * cores bind them through core_imports.inc. No user file is rewritten.
 */
#include "ps5_paths.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <utime.h>
#include <unistd.h>

#define PATH(name, failure)                                                                        \
    char name##_storage[PATH_MAX];                                                                 \
    name = ps5_installed_path(name, name##_storage, sizeof(name##_storage));                       \
    if (!name)                                                                                     \
    return failure

extern int __real_open(const char *, int, ...);
extern int __real_mkdir(const char *, mode_t);
extern FILE *__real_fopen(const char *, const char *);
extern FILE *__real_freopen(const char *, const char *, FILE *);
extern int __real_stat(const char *, struct stat *);
extern int __real_lstat(const char *, struct stat *);
extern int __real_chmod(const char *, mode_t);
extern int __real_chdir(const char *);
extern int __real_unlink(const char *);
extern int __real_rmdir(const char *);
extern int __real_remove(const char *);
extern int __real_rename(const char *, const char *);

/* Preserve the existing FTP permissions policy (permissions_ps5.cpp). */
static void created(const char *path)
{
    const int saved = errno;
    struct stat status;
    if (__real_stat(path, &status) == 0 && (status.st_mode & 0777) != 0777)
        __real_chmod(path, (status.st_mode & 07777) | 0777);
    errno = saved;
}

int __wrap_open(const char *path, int flags, ...)
{
    int mode = 0;
    if (flags & O_CREAT)
    {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, int) | 0777;
        va_end(arguments);
    }
    PATH(path, -1);
    const int fd = __real_open(path, flags, mode);
    if (fd >= 0 && (flags & O_CREAT))
        created(path);
    return fd;
}

int __wrap_mkdir(const char *path, mode_t mode)
{
    PATH(path, -1);
    const int result = __real_mkdir(path, mode | 0777);
    if (!result)
        created(path);
    return result;
}

FILE *__wrap_fopen(const char *path, const char *mode)
{
    PATH(path, NULL);
    FILE *file = __real_fopen(path, mode);
    if (file && mode && strpbrk(mode, "wa+"))
        created(path);
    return file;
}

FILE *__wrap_freopen(const char *path, const char *mode, FILE *stream)
{
    /* A NULL filename changes the mode of an existing stream. */
    if (!path)
        return __real_freopen(path, mode, stream);
    PATH(path, NULL);
    return __real_freopen(path, mode, stream);
}

int __wrap_stat(const char *path, struct stat *status)
{
    PATH(path, -1);
    return __real_stat(path, status);
}

int __wrap_lstat(const char *path, struct stat *status)
{
    PATH(path, -1);
    return __real_lstat(path, status);
}

int __wrap_chmod(const char *path, mode_t mode)
{
    PATH(path, -1);
    return __real_chmod(path, mode);
}

#define ONE_PATH(name)                                                                             \
    int __wrap_##name(const char *path)                                                            \
    {                                                                                              \
        PATH(path, -1);                                                                            \
        return __real_##name(path);                                                                \
    }
ONE_PATH(chdir)
ONE_PATH(unlink)
ONE_PATH(rmdir)
ONE_PATH(remove)

int __wrap_rename(const char *from, const char *to)
{
    PATH(from, -1);
    PATH(to, -1);
    return __real_rename(from, to);
}

extern int __real_utime(const char *, const struct utimbuf *);
int __wrap_utime(const char *path, const struct utimbuf *times)
{
    PATH(path, -1);
    return __real_utime(path, times);
}

extern int __real_utimes(const char *, const struct timeval *);
int __wrap_utimes(const char *path, const struct timeval *times)
{
    PATH(path, -1);
    return __real_utimes(path, times);
}

/* LoadExec belongs to the shell's title namespace, not this process's filesystem.
 * Keep its /app0 names: the shell rejects the resolved sandbox path at spawn. */
