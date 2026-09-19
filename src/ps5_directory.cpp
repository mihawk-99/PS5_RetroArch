/* PS5 RetroArch - native directory enumeration without libc's denied opendir.
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ps5_directory.h"
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <new>
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
