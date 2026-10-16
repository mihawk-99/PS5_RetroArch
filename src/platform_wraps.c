/*
 * PS5 RetroArch - the title's links to the directory functions, through the
 * platform layer (my payload SDK fork, include/ps5platform/libc.h).
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * tools/build-title.sh links the title with --wrap for each of these. A
 * title's opendir is refused and the *at functions resolve to nothing (only
 * libkernel_sys exports them), so the frontend's and libc++'s calls land here.
 * A core's own imports of the same names are bound to the ps5_ functions by
 * tools/core-imports.py.
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ps5platform/libc.h>

/* Bind the IPv6 constant without importing a second socket runtime. */
const struct in6_addr ps5_in6addr_loopback = IN6ADDR_LOOPBACK_INIT;

int __wrap_openat(int directory, const char *name, int flags, ...)
{
    int mode = 0;
    if (flags & O_CREAT)
    {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, int);
        va_end(arguments);
    }
    return ps5_openat(directory, name, flags, mode);
}

int __wrap_unlinkat(int directory, const char *name, int flags)
{
    return ps5_unlinkat(directory, name, flags);
}

int __wrap_fchmodat(int directory, const char *name, mode_t mode, int flags)
{
    return ps5_fchmodat(directory, name, mode, flags);
}

DIR *__wrap_fdopendir(int fd)
{
    return ps5_fdopendir(fd);
}

/* libc's getcwd calls __getcwd, which only libkernel_sys has, so a title's
 * resolves to nothing: libc++'s std::filesystem::current_path, and with it
 * absolute and canonical on a relative path, jumped to address 0 (RPCS3
 * booting a game's folder). The cores' own are bound the same way
 * (tools/core-imports.py). */
char *__wrap_getcwd(char *buffer, size_t size)
{
    return ps5_getcwd(buffer, size);
}

/* A title's realpath is refused (EPERM); libc++'s std::filesystem canonical
 * paths are built on it. */
char *__wrap_realpath(const char *path, char *resolved)
{
    return ps5_realpath(path, resolved);
}

DIR *__wrap_opendir(const char *path)
{
    return ps5_opendir(path);
}

struct dirent *__wrap_readdir(DIR *directory)
{
    return ps5_readdir(directory);
}

int __wrap_closedir(DIR *directory)
{
    return ps5_closedir(directory);
}

/* No module a title loads exports these, so each import was null at run time:
 * strcasestr and mkstemp linked against libScePosixForWebKit's stub, link,
 * symlink, readlink and pathconf against libkernel_sys's. RetroArch's menu
 * finds the entry it should select with strcasestr: Import Content -> Manual
 * Scan -> Content Directory, opened a second time, starts on the parent of the
 * folder chosen before with that folder pending, and jumped to address 0
 * (2026-10-04). tools/build.sh refuses a title that imports one again. */
char *__wrap_strcasestr(const char *haystack, const char *needle)
{
    return ps5_strcasestr(haystack, needle);
}

int __wrap_mkstemp(char *path_template)
{
    return ps5_mkstemp(path_template);
}

int __wrap_link(const char *existing, const char *name)
{
    return ps5_link(existing, name);
}

int __wrap_symlink(const char *target, const char *name)
{
    return ps5_symlink(target, name);
}

ssize_t __wrap_readlink(const char *path, char *buffer, size_t size)
{
    return ps5_readlink(path, buffer, size);
}

/* libc++'s std::filesystem::current_path sizes its buffer with pathconf; the
 * platform layer has none, so the limits are the console's FreeBSD ones for a
 * path that exists, as the frontends answer (frontends/common/frontend_shims.c). */
long __wrap_pathconf(const char *path, int name)
{
    struct stat status;
    if (!path || stat(path, &status) != 0)
        return -1;
    switch (name)
    {
    case _PC_PATH_MAX:
        return PATH_MAX;
    case _PC_NAME_MAX:
        return NAME_MAX;
    default:
        errno = EINVAL;
        return -1;
    }
}
