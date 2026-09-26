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

#include <fcntl.h>
#include <stdarg.h>

#include <ps5platform/libc.h>

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
