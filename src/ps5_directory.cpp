/* PS5 RetroArch - native directory enumeration without libc's denied opendir.
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ps5_directory.h"
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <new>
#include <sys/stat.h>
#include <unistd.h>

extern "C" int getdents(int fd, char *buffer, int bytes);

namespace
{
struct Directory
{
    int fd = -1;
    size_t offset = 0, bytes = 0;
    bool finished = false;
    // Mounted title directories require a larger read than the synthetic root.
    char buffer[64 * 1024];
    struct dirent entry{};
};
} // namespace

extern "C" DIR *ps5_fdopendir(int fd)
{
    struct stat status;
    if (fstat(fd, &status) != 0)
        return nullptr;
    if (!S_ISDIR(status.st_mode))
    {
        errno = ENOTDIR;
        return nullptr;
    }
    auto *directory = new (std::nothrow) Directory;
    if (!directory)
    {
        errno = ENOMEM;
        return nullptr;
    }
    directory->fd = fd;
    return reinterpret_cast<DIR *>(directory);
}

extern "C" DIR *ps5_opendir(const char *path)
{
    if (!path || !*path)
    {
        errno = ENOENT;
        return nullptr;
    }
    const int fd = open(path, O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        return nullptr;
    auto *directory = new (std::nothrow) Directory;
    if (!directory)
    {
        close(fd);
        errno = ENOMEM;
        return nullptr;
    }
    directory->fd = fd;
    return reinterpret_cast<DIR *>(directory);
}

extern "C" struct dirent *ps5_readdir(DIR *opaque)
{
    auto *directory = reinterpret_cast<Directory *>(opaque);
    if (!directory)
    {
        errno = EBADF;
        return nullptr;
    }
    while (!directory->finished)
    {
        if (directory->offset == directory->bytes)
        {
            const int count = getdents(directory->fd, directory->buffer, sizeof(directory->buffer));
            if (count <= 0)
            {
                directory->finished = true;
                return nullptr;
            }
            if (size_t(count) > sizeof(directory->buffer))
            {
                directory->finished = true;
                errno = EIO;
                return nullptr;
            }
            directory->bytes = size_t(count);
            directory->offset = 0;
        }
        // Public SDK FreeBSD dirent wire layout: inode32, reclen16, type8, namlen8.
        const size_t remaining = directory->bytes - directory->offset;
        const char *record = directory->buffer + directory->offset;
        uint32_t inode = 0;
        uint16_t length = 0;
        if (remaining >= 8)
        {
            std::memcpy(&inode, record, 4);
            std::memcpy(&length, record + 4, 2);
        }
        const size_t name_length = remaining >= 8 ? uint8_t(record[7]) : 0;
        if (remaining < 8 || length < 8 + name_length + 1 || length > remaining ||
            name_length >= sizeof(directory->entry.d_name) || record[8 + name_length] != 0)
        {
            directory->finished = true;
            errno = EIO;
            return nullptr;
        }
        directory->offset += length;
        if (!inode)
            continue;
        directory->entry = {};
        directory->entry.d_type = uint8_t(record[6]);
        std::memcpy(directory->entry.d_name, record + 8, name_length + 1);
        return &directory->entry;
    }
    return nullptr;
}

extern "C" void ps5_rewinddir(DIR *opaque)
{
    auto *directory = reinterpret_cast<Directory *>(opaque);
    if (!directory)
    {
        errno = EBADF;
        return;
    }
    if (lseek(directory->fd, 0, SEEK_SET) < 0)
        return;
    directory->offset = directory->bytes = 0;
    directory->finished = false;
}

extern "C" int ps5_closedir(DIR *opaque)
{
    auto *directory = reinterpret_cast<Directory *>(opaque);
    if (!directory)
    {
        errno = EBADF;
        return -1;
    }
    const int result = close(directory->fd);
    delete directory;
    return result;
}

/* The *at family for the title's own libc++.
 *
 * std::filesystem in the title's libc++ archive (the one every core's C++ uses)
 * walks directories with openat, fdopendir and unlinkat (remove_all) and sets
 * permissions with fchmodat. The console's libkernel exports none of them - the
 * SDK stub lists them, and the imports resolve to null, so Dolphin's first
 * remove_all called address 0 (2026-09-24) - and libc's opendir is refused
 * outright (see above). The title link wraps all of them (tools/build-title.sh),
 * so libc++ reaches these instead.
 *
 * A path relative to a directory descriptor is resolved against the path that
 * descriptor was opened with, which openat records. The record is checked
 * against the descriptor's device and inode before use, so a descriptor number
 * that was closed and reused elsewhere is never resolved to a stale path. */
namespace
{
struct DirectoryPath
{
    int fd = -1;
    dev_t device = 0;
    ino_t inode = 0;
    char path[1024];
};

constexpr int kTrackedDirectories = 64;
std::mutex directory_paths_lock;
DirectoryPath directory_paths[kTrackedDirectories];

void remember_directory(int fd, const char *path)
{
    struct stat status;
    if (fstat(fd, &status) != 0 || !S_ISDIR(status.st_mode) ||
        std::strlen(path) >= sizeof(DirectoryPath::path))
        return;
    std::lock_guard<std::mutex> hold(directory_paths_lock);
    DirectoryPath *slot = nullptr;
    for (auto &entry : directory_paths)
        if (entry.fd == fd || (!slot && entry.fd < 0))
            slot = &entry;
    if (!slot)
        slot = &directory_paths[fd % kTrackedDirectories];
    slot->fd = fd;
    slot->device = status.st_dev;
    slot->inode = status.st_ino;
    std::strcpy(slot->path, path);
}

void forget_directory(int fd)
{
    std::lock_guard<std::mutex> hold(directory_paths_lock);
    for (auto &entry : directory_paths)
        if (entry.fd == fd)
            entry.fd = -1;
}

/* The path of name relative to directory, in out; false (errno set) when the
 * directory descriptor is not one openat recorded. */
bool resolve_at(int directory, const char *name, char *out, size_t size)
{
    if (!name)
    {
        errno = EFAULT;
        return false;
    }
    if (directory == AT_FDCWD || name[0] == '/')
    {
        if (std::strlen(name) >= size)
        {
            errno = ENAMETOOLONG;
            return false;
        }
        std::strcpy(out, name);
        return true;
    }
    struct stat status;
    if (fstat(directory, &status) != 0)
        return false;
    std::lock_guard<std::mutex> hold(directory_paths_lock);
    for (const auto &entry : directory_paths)
    {
        if (entry.fd != directory || entry.device != status.st_dev || entry.inode != status.st_ino)
            continue;
        const size_t base = std::strlen(entry.path);
        const bool slash = base > 0 && entry.path[base - 1] == '/';
        if (base + (slash ? 0 : 1) + std::strlen(name) >= size)
        {
            errno = ENAMETOOLONG;
            return false;
        }
        std::strcpy(out, entry.path);
        if (!slash)
            std::strcat(out, "/");
        std::strcat(out, name);
        return true;
    }
    errno = ENOSYS;
    return false;
}
} // namespace

extern "C" int __wrap_openat(int directory, const char *name, int flags, ...)
{
    int mode = 0;
    if (flags & O_CREAT)
    {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, int);
        va_end(arguments);
    }
    char path[1024];
    if (!resolve_at(directory, name, path, sizeof(path)))
        return -1;
    const int fd = open(path, flags, mode);
    if (fd >= 0)
        remember_directory(fd, path);
    return fd;
}

extern "C" int __wrap_unlinkat(int directory, const char *name, int flags)
{
    char path[1024];
    if (!resolve_at(directory, name, path, sizeof(path)))
        return -1;
    return (flags & AT_REMOVEDIR) ? rmdir(path) : unlink(path);
}

extern "C" int __wrap_fchmodat(int directory, const char *name, mode_t mode, int flags)
{
    (void)flags;
    char path[1024];
    if (!resolve_at(directory, name, path, sizeof(path)))
        return -1;
    return chmod(path, mode);
}

extern "C" DIR *__wrap_fdopendir(int fd)
{
    return ps5_fdopendir(fd);
}

extern "C" DIR *__wrap_opendir(const char *path)
{
    return ps5_opendir(path);
}

extern "C" struct dirent *__wrap_readdir(DIR *directory)
{
    return ps5_readdir(directory);
}

extern "C" int __wrap_closedir(DIR *directory)
{
    auto *native = reinterpret_cast<Directory *>(directory);
    if (native)
        forget_directory(native->fd);
    return ps5_closedir(directory);
}
