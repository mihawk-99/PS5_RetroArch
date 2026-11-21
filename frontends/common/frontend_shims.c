/* PS5 RetroArch - what a frontend executable's link needs beyond the payload SDK.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The compiler's builtins archive on this host (clang 23) is built with
 * _FORTIFY_SOURCE, so its emulated thread-locals (emutls.c, which SDL2 and the
 * OpenGL SDK pull in) call __memset_chk, which the console's libc does not have.
 * The check is the caller's buffer size; this keeps it.
 *
 * faccessat: libc++'s std::filesystem asks with it (ES-DE's MiximageGenerator), and
 * the console's libc has none. access() itself is the platform's here (bound by
 * tools/build-frontend.sh), the console's refusing every path a title asks about.
 *
 * pathconf: libc++'s std::filesystem::current_path sizes its buffer with it, and
 * only libkernel_sys exports it, which a title does not load: the import would be
 * null. The limits are the console's FreeBSD ones, for a path that exists.
 *
 * getcwd: the platform's ps5_getcwd finds the working directory by walking up
 * from "." and listing each parent, which failed in the title's sandbox; ES-DE,
 * given no working directory, recursed until its stack ran out
 * (klog/run-PPSA99169-160106.log). A frontend sets its working directory once,
 * with ps5_frontend_chdir and an absolute path, and getcwd answers with that path;
 * before it does, getcwd is the platform's. The console refuses chdir to a title
 * (EPERM, klog/frontend-20261004-161318/es-de-ps5.log), so until that changes
 * getcwd stays the platform's.
 *
 * lstat: refused to a title (EPERM) for every path; ES-DE asks it of every theme
 * file through std::filesystem::is_symlink and logged 13,686 refusals in one
 * start (klog/frontend-20261004-161318/es_log.txt). A title has no symbolic links
 * (the platform refuses symlink and finds none with readlink), so lstat answers
 * as stat. The platform's getcwd walks the tree with it too.
 *
 * ps5_smb_stat: game mode (src/ps5_game.c, linked into every frontend) asks it of
 * smb:// content through a weak reference, and in a program without the SMB client
 * that reference became an import of a name no module exports, which
 * tools/build-frontend.sh refuses. This weak default is the answer there: not on the
 * share. ES-DE's client (frontends/es-de/ps5/smb_client.c) replaces it.
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void *__memset_chk(void *destination, int value, size_t length, size_t destination_length)
{
    if (length > destination_length)
        abort();
    return memset(destination, value, length);
}

int faccessat(int directory, const char *path, int mode, int flags)
{
    (void)flags;
    if (!path || (directory != AT_FDCWD && path[0] != '/'))
    {
        errno = ENOSYS;
        return -1;
    }
    return access(path, mode);
}

long pathconf(const char *path, int name)
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

char *ps5_getcwd(char *buffer, size_t size);

static char working_directory[PATH_MAX];

int ps5_frontend_chdir(const char *path)
{
    if (!path || path[0] != '/' || strlen(path) >= sizeof(working_directory))
    {
        errno = EINVAL;
        return -1;
    }
    if (chdir(path) != 0)
        return -1;
    strcpy(working_directory, path);
    return 0;
}

char *getcwd(char *buffer, size_t size)
{
    if (!working_directory[0])
        return ps5_getcwd(buffer, size);
    const size_t needed = strlen(working_directory) + 1;
    if (buffer && size == 0)
    {
        errno = EINVAL;
        return NULL;
    }
    if (!buffer)
    {
        buffer = malloc(size > needed ? size : needed);
        if (!buffer)
            return NULL;
    }
    else if (size < needed)
    {
        errno = ERANGE;
        return NULL;
    }
    memcpy(buffer, working_directory, needed);
    return buffer;
}

int lstat(const char *path, struct stat *status)
{
    return stat(path, status);
}

__attribute__((weak)) int ps5_smb_stat(const char *url, int64_t *size)
{
    (void)url;
    (void)size;
    return 0;
}
