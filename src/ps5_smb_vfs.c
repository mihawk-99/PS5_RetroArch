/* PS5 RetroArch - smb:// paths in RetroArch's VFS.
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Read-only SMB VFS. Directory browsing keeps its directory connection alive;
 * stat requests reuse a separate connection for the same server/share while a
 * browser is active. All synchronous libsmb2 operations are logged.
 *
 * NOTE: libsmb2's synchronous API still runs on the calling thread. This file
 * adds diagnostics, reuse and bounded enumeration, but moving network work off
 * RetroArch's UI thread requires an asynchronous VFS/worker-thread integration.
 */
#include "ps5_smb_vfs.h"
#include "ps5_smb.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <smb2/smb2.h>
#include <smb2/libsmb2.h>

#include <libretro.h>

#ifdef __PROSPERO__
void RARCH_LOG(const char *fmt, ...);
#define SMB_LOG RARCH_LOG
#else
#define SMB_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

#ifndef PS5_SMB_CONFIG
#define PS5_SMB_CONFIG "/app0/config/smb.cfg"
#endif

#define SMB_URL_SIZE 256
#define SMB_SHARE_SIZE 256
#define SMB_PATH_SIZE 1024
#define SMB_MAX_DOT_ENTRIES 16

struct ps5_smb_file
{
    struct smb2_context *context;
    struct smb2fh *handle;
    ps5_smb_config config;
    char share[SMB_SHARE_SIZE];
    char path[SMB_PATH_SIZE];
    uint64_t position;
};

struct ps5_smb_dir
{
    struct smb2_context *context;
    struct smb2dir *dir;
    struct smb2dirent *entry;
    unsigned entries;
    char url[SMB_URL_SIZE];
    char share[SMB_SHARE_SIZE];
};

/*
 * libsmb2 contexts are not generally safe for concurrent use. Serialize the
 * shared stat connection. It is retained while at least one directory browser
 * is open and destroyed when the final browser closes.
 */
static pthread_mutex_t stat_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct smb2_context *stat_cache_context;
static char stat_cache_server[sizeof(((ps5_smb_config *)0)->server)];
static char stat_cache_share[SMB_SHARE_SIZE];
static unsigned open_directory_count;

static void log_smb_error(const char *operation, const char *url,
                          struct smb2_context *context)
{
    if (context)
        SMB_LOG("[SMB] %s failed: url=%s NT=0x%08X error=%s\n",
                operation, url ? url : "(null)",
                (unsigned)smb2_get_nterror(context),
                smb2_get_error(context) ? smb2_get_error(context) : "(no detail)");
    else
        SMB_LOG("[SMB] %s failed: url=%s context=NULL\n",
                operation, url ? url : "(null)");
}

static struct smb2_context *connect_to(const ps5_smb_config *config,
                                       const char *share, const char *operation,
                                       const char *url)
{
    char why[256] = {0};
    SMB_LOG("[SMB] %s: connecting to server=%s share=%s\n",
            operation, config->server, share);
    struct smb2_context *context =
        ps5_smb_connect(config, share, why, sizeof(why));
    if (!context)
        SMB_LOG("[SMB] %s: connection failed: %s [%s]\n",
                operation, why[0] ? why : "no explanation",
                ps5_smb_last_detail());
    else
        SMB_LOG("[SMB] %s: connected (url=%s)\n",
                operation, url ? url : "(null)");
    return context;
}

/* The saved server, and the share and path a URL names on it:
 * smb://server/share[/path]. Passwords are never logged or placed in URLs. */
static bool locate(const char *url, ps5_smb_config *config,
                   char *share, size_t share_size,
                   char *path, size_t path_size)
{
    if (!url || !config || !share || !path ||
        strncmp(url, "smb://", 6) != 0 ||
        !ps5_smb_config_load(PS5_SMB_CONFIG, config))
    {
        SMB_LOG("[SMB] locate: invalid URL or no saved server (%s)\n",
                PS5_SMB_CONFIG);
        return false;
    }

    const char *server = url + 6;
    const char *slash = strchr(server, '/');
    const size_t server_length = slash ? (size_t)(slash - server) : strlen(server);

    if (server_length != strlen(config->server) ||
        strncmp(server, config->server, server_length) != 0 || !slash)
    {
        SMB_LOG("[SMB] locate: URL is not a share of the saved server: %s\n", url);
        return false;
    }

    const char *name = slash + 1;
    const char *rest = strchr(name, '/');
    const size_t name_length = rest ? (size_t)(rest - name) : strlen(name);

    if (!name_length || name_length >= share_size)
    {
        SMB_LOG("[SMB] locate: invalid/oversized share in URL: %s\n", url);
        return false;
    }

    memcpy(share, name, name_length);
    share[name_length] = '\0';

    rest = rest ? rest + 1 : "";
    size_t length = strlen(rest);
    while (length && rest[length - 1] == '/')
        --length;

    if (length >= path_size)
    {
        SMB_LOG("[SMB] locate: path too long: %s\n", url);
        return false;
    }

    memcpy(path, rest, length);
    path[length] = '\0';
    return true;
}

static void stat_cache_drop_locked(void)
{
    if (stat_cache_context)
    {
        SMB_LOG("[SMB] stat-cache: disconnecting server=%s share=%s\n",
                stat_cache_server, stat_cache_share);
        ps5_smb_disconnect(stat_cache_context);
    }
    stat_cache_context = NULL;
    stat_cache_server[0] = '\0';
    stat_cache_share[0] = '\0';
}

static struct smb2_context *stat_cache_get_locked(const ps5_smb_config *config,
                                                   const char *share,
                                                   const char *url)
{
    if (stat_cache_context &&
        (strcmp(stat_cache_server, config->server) != 0 ||
         strcmp(stat_cache_share, share) != 0 ||
         !ps5_smb_alive(stat_cache_context)))
        stat_cache_drop_locked();

    if (!stat_cache_context)
    {
        stat_cache_context =
            connect_to(config, share, "stat-cache", url);
        if (stat_cache_context)
        {
            snprintf(stat_cache_server, sizeof(stat_cache_server), "%s",
                     config->server);
            snprintf(stat_cache_share, sizeof(stat_cache_share), "%s", share);
        }
    }
    return stat_cache_context;
}

static struct ps5_smb_file *open_file(const ps5_smb_config *config,
                                      const char *share, const char *path)
{
    struct ps5_smb_file *file = calloc(1, sizeof(*file));
    if (!file)
    {
        SMB_LOG("[SMB] open: allocation failed\n");
        return NULL;
    }

    file->config = *config;
    snprintf(file->share, sizeof(file->share), "%s", share);
    snprintf(file->path, sizeof(file->path), "%s", path);

    SMB_LOG("[SMB] open: opening %s/%s\n", share, path);
    file->context = connect_to(config, share, "open", path);
    if (file->context)
        file->handle = smb2_open(file->context, path, O_RDONLY);

    if (file->context && file->handle)
    {
        SMB_LOG("[SMB] open: opened %s/%s\n", share, path);
        return file;
    }

    log_smb_error("smb2_open", path, file->context);
    ps5_smb_disconnect(file->context);
    free(file);
    return NULL;
}

/* Reconnect and reopen the file at the position already consumed. */
static bool mend(struct ps5_smb_file *file)
{
    if (!file)
        return false;

    SMB_LOG("[SMB] reconnect: reopening %s/%s at offset %llu\n",
            file->share, file->path, (unsigned long long)file->position);

    ps5_smb_disconnect(file->context);
    file->context = NULL;
    file->handle = NULL;

    file->context = connect_to(&file->config, file->share,
                               "read-reconnect", file->path);
    if (!file->context)
        return false;

    file->handle = smb2_open(file->context, file->path, O_RDONLY);
    if (!file->handle)
    {
        log_smb_error("reconnect smb2_open", file->path, file->context);
        ps5_smb_disconnect(file->context);
        file->context = NULL;
        return false;
    }

    if (smb2_lseek(file->context, file->handle,
                   (int64_t)file->position, SEEK_SET, NULL) < 0)
    {
        log_smb_error("reconnect smb2_lseek", file->path, file->context);
        ps5_smb_disconnect(file->context);
        file->context = NULL;
        file->handle = NULL;
        return false;
    }

    SMB_LOG("[SMB] reconnect: restored %s/%s\n", file->share, file->path);
    return true;
}

void *ps5_smb_open(const char *url, unsigned mode, int64_t *size)
{
    ps5_smb_config config;
    char share[SMB_SHARE_SIZE], path[SMB_PATH_SIZE];

    SMB_LOG("[SMB] open: requested url=%s mode=0x%X\n",
            url ? url : "(null)", mode);

    if (!size || (mode & RETRO_VFS_FILE_ACCESS_WRITE) ||
        !locate(url, &config, share, sizeof(share), path, sizeof(path)) ||
        !path[0])
    {
        SMB_LOG("[SMB] open: rejected URL/mode/size\n");
        return NULL;
    }

    struct ps5_smb_file *file = open_file(&config, share, path);
    if (!file)
        return NULL;

    struct smb2_stat_64 status;
    SMB_LOG("[SMB] open: requesting fstat for %s\n", url);
    if (smb2_fstat(file->context, file->handle, &status) == 0)
    {
        *size = (int64_t)status.smb2_size;
        SMB_LOG("[SMB] open: success size=%lld url=%s\n",
                (long long)*size, url);
        return file;
    }

    log_smb_error("smb2_fstat", url, file->context);
    ps5_smb_close(file);
    return NULL;
}

int64_t ps5_smb_read(void *opened, void *buffer, uint64_t length)
{
    struct ps5_smb_file *file = opened;
    if (!file || (!buffer && length))
    {
        SMB_LOG("[SMB] read: invalid file/buffer\n");
        return -1;
    }
    if (!length)
        return 0;

    uint8_t *into = buffer;
    uint64_t done = 0;
    bool mended = false;

    /* libsmb2 caps an individual read; continue until the request is satisfied
     * or EOF/error is reached. */
    while (done < length)
    {
        const uint64_t want =
            length - done > (1u << 30) ? (1u << 30) : length - done;

        if (!file->context || !file->handle)
        {
            if (mended || !mend(file))
                return done ? (int64_t)done : -1;
            mended = true;
        }

        const int got = smb2_read(file->context, file->handle,
                                  into + done, (uint32_t)want);
        if (got < 0 && !mended)
        {
            log_smb_error("smb2_read", file->path, file->context);
            mended = true;
            if (mend(file))
                continue;
        }

        if (got <= 0)
        {
            if (got < 0)
                log_smb_error("smb2_read", file->path, file->context);
            return done || got == 0 ? (int64_t)done : -1;
        }

        done += (uint64_t)got;
        file->position += (uint64_t)got;
    }

    return (int64_t)done;
}

int64_t ps5_smb_seek(void *opened, int64_t offset, int whence)
{
    struct ps5_smb_file *file = opened;
    uint64_t now = 0;

    if (!file)
        return -1;
    if ((!file->context || !file->handle) && !mend(file))
        return -1;

    SMB_LOG("[SMB] seek: %s offset=%lld whence=%d\n",
            file->path, (long long)offset, whence);

    if (smb2_lseek(file->context, file->handle, offset, whence, &now) < 0)
    {
        log_smb_error("smb2_lseek", file->path, file->context);
        return -1;
    }

    file->position = now;
    return 0;
}

int64_t ps5_smb_tell(void *opened)
{
    struct ps5_smb_file *file = opened;
    return file ? (int64_t)file->position : -1;
}

void ps5_smb_close(void *opened)
{
    struct ps5_smb_file *file = opened;
    if (!file)
        return;

    SMB_LOG("[SMB] close: %s\n", file->path);
    if (file->handle && file->context && ps5_smb_alive(file->context))
        smb2_close(file->context, file->handle);

    ps5_smb_disconnect(file->context);
    file->context = NULL;
    file->handle = NULL;
    free(file);
}

int ps5_smb_stat(const char *url, int64_t *size)
{
    ps5_smb_config config;
    char share[SMB_SHARE_SIZE], path[SMB_PATH_SIZE];

    SMB_LOG("[SMB] stat: requested url=%s\n", url ? url : "(null)");
    if (!locate(url, &config, share, sizeof(share), path, sizeof(path)))
        return 0;

    pthread_mutex_lock(&stat_cache_mutex);
    struct smb2_context *context =
        stat_cache_get_locked(&config, share, url);

    struct smb2_stat_64 status;
    const bool found = context && smb2_stat(context, path, &status) == 0;

    if (!found)
    {
        if (context)
            log_smb_error("smb2_stat", url, context);
        else
            SMB_LOG("[SMB] stat: no usable connection for %s\n", url);
        /* A failed stat can simply mean the path does not exist. Keep a live
         * connection for the next entry; discard it only if libsmb2 says dead. */
        if (context && !ps5_smb_alive(context))
            stat_cache_drop_locked();
        pthread_mutex_unlock(&stat_cache_mutex);
        return 0;
    }

    if (size)
        *size = (int64_t)status.smb2_size;

    const int result =
        RETRO_VFS_STAT_IS_VALID |
        (status.smb2_type == SMB2_TYPE_DIRECTORY
             ? RETRO_VFS_STAT_IS_DIRECTORY : 0);

    SMB_LOG("[SMB] stat: found %s type=%u size=%lld\n",
            url, (unsigned)status.smb2_type,
            (long long)status.smb2_size);
    pthread_mutex_unlock(&stat_cache_mutex);
    return result;
}

void *ps5_smb_opendir(const char *url)
{
    ps5_smb_config config;
    char share[SMB_SHARE_SIZE], path[SMB_PATH_SIZE];

    SMB_LOG("[SMB] opendir: BEGIN url=%s\n", url ? url : "(null)");

    SMB_LOG("[SMB] opendir: loading saved SMB configuration\n");
    if (!locate(url, &config, share, sizeof(share), path, sizeof(path)))
    {
        SMB_LOG("[SMB] opendir: STOP - URL/configuration validation failed\n");
        return NULL;
    }

    SMB_LOG("[SMB] opendir: parsed server=%s share=%s path=%s\n",
            config.server, share, path[0] ? path : "(share root)");

    struct ps5_smb_dir *dir = calloc(1, sizeof(*dir));
    if (!dir)
    {
        SMB_LOG("[SMB] opendir: STOP - allocation failed\n");
        return NULL;
    }

    snprintf(dir->url, sizeof(dir->url), "%s", url);
    snprintf(dir->share, sizeof(dir->share), "%s", share);

    SMB_LOG("[SMB] opendir: connecting to SMB share\n");
    dir->context = connect_to(&config, share, "opendir", url);
    if (!dir->context)
    {
        SMB_LOG("[SMB] opendir: STOP - SMB connection failed\n");
        free(dir);
        return NULL;
    }

    SMB_LOG("[SMB] opendir: connected; issuing smb2_opendir(path=%s)\n",
            path[0] ? path : "(share root)");

    dir->dir = smb2_opendir(dir->context, path);
    if (!dir->dir)
    {
        log_smb_error("smb2_opendir", url, dir->context);
        SMB_LOG("[SMB] opendir: STOP - directory open failed\n");
        ps5_smb_disconnect(dir->context);
        dir->context = NULL;
        free(dir);
        return NULL;
    }

    pthread_mutex_lock(&stat_cache_mutex);
    ++open_directory_count;
    pthread_mutex_unlock(&stat_cache_mutex);

    SMB_LOG("[SMB] opendir: SUCCESS url=%s active_browsers=%u\n",
            url, open_directory_count);
    return dir;
}

bool ps5_smb_readdir(void *opened)
{
    struct ps5_smb_dir *dir = opened;
    if (!dir || !dir->context || !dir->dir)
    {
        SMB_LOG("[SMB] readdir: invalid directory/context/handle\n");
        if (dir)
            dir->entry = NULL;
        return false;
    }

    dir->entry = NULL;

    /*
     * One smb2_readdir() call is synchronous. Bound the dot-entry skip loop
     * defensively so malformed/unusual servers cannot trap us in this function.
     */
    for (unsigned skipped = 0; skipped < SMB_MAX_DOT_ENTRIES; ++skipped)
    {
        SMB_LOG("[SMB] readdir: requesting next entry (%u returned so far)\n",
                dir->entries);
        struct smb2dirent *entry = smb2_readdir(dir->context, dir->dir);

        if (!entry)
        {
            SMB_LOG("[SMB] readdir: end/error url=%s entries=%u NT=0x%08X error=%s\n",
                    dir->url, dir->entries,
                    (unsigned)smb2_get_nterror(dir->context),
                    smb2_get_error(dir->context)
                        ? smb2_get_error(dir->context) : "(no detail)");
            return false;
        }

        if (!entry->name)
        {
            SMB_LOG("[SMB] readdir: NULL entry name url=%s\n", dir->url);
            return false;
        }

        if (!strcmp(entry->name, ".") || !strcmp(entry->name, ".."))
            continue;

        dir->entry = entry;
        ++dir->entries;

        SMB_LOG("[SMB] readdir: entry %u name=%s type=%u\n",
                dir->entries, entry->name,
                (unsigned)entry->st.smb2_type);
        return true;
    }

    SMB_LOG("[SMB] readdir: too many consecutive dot entries; aborting url=%s\n",
            dir->url);
    return false;
}

const char *ps5_smb_dirent_name(void *opened)
{
    struct ps5_smb_dir *dir = opened;
    if (!dir || !dir->entry || !dir->entry->name)
        return NULL;
    return dir->entry->name;
}

bool ps5_smb_dirent_is_dir(void *opened)
{
    struct ps5_smb_dir *dir = opened;
    if (!dir || !dir->entry)
        return false;
    return dir->entry->st.smb2_type == SMB2_TYPE_DIRECTORY;
}

void ps5_smb_closedir(void *opened)
{
    struct ps5_smb_dir *dir = opened;
    if (!dir)
        return;

    SMB_LOG("[SMB] closedir: BEGIN url=%s entries=%u\n",
            dir->url, dir->entries);

    if (dir->context && dir->dir && ps5_smb_alive(dir->context))
        smb2_closedir(dir->context, dir->dir);

    dir->dir = NULL;
    ps5_smb_disconnect(dir->context);
    dir->context = NULL;
    free(dir);

    pthread_mutex_lock(&stat_cache_mutex);
    if (open_directory_count)
        --open_directory_count;
    SMB_LOG("[SMB] closedir: active_browsers=%u\n", open_directory_count);
    if (!open_directory_count)
        stat_cache_drop_locked();
    pthread_mutex_unlock(&stat_cache_mutex);

    SMB_LOG("[SMB] closedir: COMPLETE\n");
}

bool ps5_smb_stage(const char *url, const char *directory,
                   char *path, size_t size)
{
    if (!url || !directory || !*directory || !path || !size)
        return false;

    const char *name = strrchr(url, '/');
    if (!name || !name[1] ||
        (mkdir(directory, 0777) != 0 && errno != EEXIST))
        return false;

    const int length = snprintf(path, size, "%s/%s", directory, name + 1);
    if (length <= 0 || (size_t)length >= size)
        return false;

    int64_t total = 0;
    int64_t copied = 0;
    int64_t got = 0;
    void *from = ps5_smb_open(url, RETRO_VFS_FILE_ACCESS_READ, &total);
    if (!from)
        return false;

    const int to = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    const size_t block = 4u << 20;
    uint8_t *buffer = to >= 0 ? malloc(block) : NULL;

    while (buffer && (got = ps5_smb_read(from, buffer, block)) > 0)
    {
        int64_t written = 0;
        while (written < got)
        {
            const ssize_t n =
                write(to, buffer + written, (size_t)(got - written));
            if (n <= 0)
                break;
            written += n;
        }
        if (written != got)
            break;
        copied += got;
    }

    free(buffer);
    ps5_smb_close(from);

    const bool closed = to >= 0 && close(to) == 0;
    const bool whole = closed && copied == total && got == 0;

    SMB_LOG("[SMB] stage: copied %lld of %lld bytes for %s\n",
            (long long)copied, (long long)total, url);

    if (!whole && to >= 0)
        unlink(path);
    return whole;
}
