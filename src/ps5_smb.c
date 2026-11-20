/* PS5 RetroArch - the saved SMB server: its file, sign-in, share list and check.
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The sign-in and the share listing follow RetroArch's own SMB client
 * (libretro-common/vfs/vfs_implementation_smb.c, libretro/RetroArch@c68eea5598,
 * GPL-3.0-or-later), with libsmb2's synchronous share listing.
 */
#include "ps5_smb.h"
#include "ps5_smb_socket.h"

#include <ctype.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>

#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/libsmb2-raw.h>
#include <smb2/libsmb2-share-enum.h>

static pthread_mutex_t contexts = PTHREAD_MUTEX_INITIALIZER;

/* libsmb2 drains replies until EAGAIN, including after its completion callback.
 * A blocking scalar read there prevents the synchronous wait loop from checking
 * its five-second timeout. Enforce nonblocking I/O per socket call rather than
 * relying solely on the library's unchecked fcntl(O_NONBLOCK). These symbols
 * replace readv/writev only inside the PS5 libsmb2 build; regular files and other
 * title callers keep their existing semantics.
 */
ssize_t ps5_smb_socket_readv(int fd, const struct iovec *vectors, int count)
{
    ssize_t total = 0;
    for (int i = 0; i < count; ++i)
    {
        if (!vectors[i].iov_len)
            continue;
        const ssize_t got = recv(fd, vectors[i].iov_base, vectors[i].iov_len, MSG_DONTWAIT);
        if (got < 0)
            return total ? total : got;
        total += got;
        if ((size_t)got < vectors[i].iov_len)
            break;
    }
    return total;
}

ssize_t ps5_smb_socket_writev(int fd, const struct iovec *vectors, int count)
{
    ssize_t total = 0;
    for (int i = 0; i < count; ++i)
    {
        if (!vectors[i].iov_len)
            continue;
        const ssize_t sent = send(fd, vectors[i].iov_base, vectors[i].iov_len, MSG_DONTWAIT);
        if (sent < 0)
            return total ? total : sent;
        total += sent;
        if ((size_t)sent < vectors[i].iov_len)
            break;
    }
    return total;
}

/* libsmb2's resolver on the PS5 (tools/build-libsmb2.sh): getaddrinfo itself in the WebUI's
 * daemon; the title has its own (src/platform_wraps.c). */
__attribute__((weak)) int ps5_smb_getaddrinfo(const char *node, const char *service,
                                              const struct addrinfo *hints,
                                              struct addrinfo **result)
{
    return getaddrinfo(node, service, hints, result);
}

__attribute__((weak)) void ps5_smb_freeaddrinfo(struct addrinfo *info)
{
    freeaddrinfo(info);
}
static _Thread_local char last_detail[256];

const char *ps5_smb_last_detail(void)
{
    return last_detail;
}
static const unsigned timeout_seconds = 5;

static void say(char *why, size_t size, const char *text)
{
    if (why && size)
        snprintf(why, size, "%s", text);
}

static void copy(char *to, size_t size, const char *from)
{
    snprintf(to, size, "%s", from ? from : "");
}

bool ps5_smb_config_load(const char *path, ps5_smb_config *config)
{
    memset(config, 0, sizeof(*config));
    FILE *file = fopen(path, "r");
    if (!file)
        return false;
    char line[1024];
    while (fgets(line, sizeof(line), file))
    {
        char *equals = strchr(line, '=');
        if (!equals)
            continue;
        char *key = line, *value = equals + 1, *end = equals;
        while (end > key && isspace((unsigned char)end[-1]))
            --end;
        *end = '\0';
        while (isspace((unsigned char)*key))
            ++key;
        while (isspace((unsigned char)*value))
            ++value;
        size_t length = strcspn(value, "\r\n");
        value[length] = '\0';
        if (length >= 2 && value[0] == '"' && value[length - 1] == '"')
        {
            value[length - 1] = '\0';
            ++value;
        }
        if (!strcmp(key, "smb_server"))
            copy(config->server, sizeof(config->server), value);
        else if (!strcmp(key, "smb_share"))
            copy(config->share, sizeof(config->share), value);
        else if (!strcmp(key, "smb_folder"))
            copy(config->folder, sizeof(config->folder), value);
        else if (!strcmp(key, "smb_user"))
            copy(config->user, sizeof(config->user), value);
        else if (!strcmp(key, "smb_password"))
            copy(config->password, sizeof(config->password), value);
        else if (!strcmp(key, "smb_workgroup"))
            copy(config->workgroup, sizeof(config->workgroup), value);
    }
    fclose(file);
    return config->server[0] && config->share[0];
}

bool ps5_smb_alive(struct smb2_context *context)
{
    pthread_mutex_lock(&contexts);
    const bool alive = smb2_context_active(context) != 0;
    pthread_mutex_unlock(&contexts);
    return alive;
}

static void destroy(struct smb2_context *context)
{
    pthread_mutex_lock(&contexts);
    smb2_destroy_context(context);
    pthread_mutex_unlock(&contexts);
}

struct smb2_context *ps5_smb_connect(const ps5_smb_config *config, const char *share, char *why,
                                     size_t why_size)
{
    if (!config->server[0])
    {
        say(why, why_size, "Enter the server's name or address.");
        return NULL;
    }
    pthread_mutex_lock(&contexts);
    struct smb2_context *context = smb2_init_context();
    pthread_mutex_unlock(&contexts);
    if (!context)
    {
        say(why, why_size, "There is not enough memory to connect.");
        return NULL;
    }
    /* No user name is the guest account: Samba's "map to guest = Bad User" and Windows
     * both take an unknown name with no password as the guest. */
    const char *user = config->user[0] ? config->user : "guest";
    smb2_set_user(context, user);
    /* An empty password set is not no password: the guest sign-in then fails. */
    if (config->password[0])
        smb2_set_password(context, config->password);
    if (config->workgroup[0])
        smb2_set_domain(context, config->workgroup);
    smb2_set_timeout(context, (int)timeout_seconds);
    smb2_set_security_mode(context, SMB2_NEGOTIATE_SIGNING_ENABLED);
    smb2_set_authentication(context, SMB2_SEC_NTLMSSP);
    if (smb2_connect_share(context, config->server, share ? share : "IPC$", user) < 0)
    {
        /* libsmb2's synchronous connect reports the socket it closed, not why: the
         * server's NT status says that. */
        const unsigned status = (unsigned)smb2_get_nterror(context);
        const char *error = smb2_get_error(context);
        snprintf(last_detail, sizeof(last_detail), "%.*s", (int)strcspn(error ? error : "", "\r\n"),
                 error ? error : "");
        char text[160];
        if (status == 0xC000006Du || status == 0xC0000022u || status == 0xC000006Eu ||
            status == 0xC0000072u || status == 0xC0000071u)
            snprintf(text, sizeof(text),
                     "The server refused this user name and password (NT status 0x%08X).", status);
        else if (status == 0xC00000CCu)
            snprintf(text, sizeof(text), "The server has no share by that name.");
        else
            snprintf(text, sizeof(text),
                     "The server did not answer. Check its address and that it shares files.");
        say(why, why_size, text);
        destroy(context);
        return NULL;
    }
#ifdef SO_NOSIGPIPE
    /* A write to a connection the server dropped would raise SIGPIPE, which ends the
     * title: the write fails instead, and the reader reconnects (src/ps5_smb_vfs.c). */
    const int on = 1;
    setsockopt(smb2_get_fd(context), SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
    return context;
}

void ps5_smb_disconnect(struct smb2_context *context)
{
    if (!context)
        return;
    if (ps5_smb_alive(context))
        smb2_disconnect_share(context);
    destroy(context);
}

int ps5_smb_shares(const ps5_smb_config *config, void (*each)(const char *name, void *data),
                   void *data, char *why, size_t why_size)
{
    struct smb2_context *context = ps5_smb_connect(config, NULL, why, why_size);
    if (!context)
        return -1;
    struct srvsvc_NetrShareEnum_rep *reply = smb2_share_enum_sync(context, SHARE_INFO_1);
    if (!reply)
    {
        say(why, why_size, "The server would not list its shares.");
        ps5_smb_disconnect(context);
        return -1;
    }
    int count = 0;
    const struct srvsvc_SHARE_INFO_1 *info = reply->ses.ShareEnum.Level1.share_info_1;
    for (uint32_t i = 0; info && i < reply->ses.ShareEnum.Level1.EntriesRead; ++i)
    {
        /* Disk shares only, and none of the hidden or administrative ones (C$, IPC$). */
        const char *name = info[i].netname;
        if (!name || !*name || (info[i].type & 3) != SRVSVC_SHARE_TYPE_DISKTREE ||
            (info[i].type & (SRVSVC_SHARE_TYPE_HIDDEN | SRVSVC_SHARE_TYPE_TEMPORARY)) ||
            name[strlen(name) - 1] == '$')
            continue;
        each(name, data);
        ++count;
    }
    smb2_free_data(context, reply);
    ps5_smb_disconnect(context);
    return count;
}

bool ps5_smb_check(const ps5_smb_config *config, char *why, size_t why_size)
{
    if (!config->share[0])
    {
        say(why, why_size, "Choose a share.");
        return false;
    }
    struct smb2_context *context = ps5_smb_connect(config, config->share, why, why_size);
    if (!context)
        return false;
    struct smb2_stat_64 status;
    const bool folder = !config->folder[0] || (smb2_stat(context, config->folder, &status) == 0 &&
                                               status.smb2_type == SMB2_TYPE_DIRECTORY);
    if (!folder)
        say(why, why_size, "The share has no folder by that name.");
    ps5_smb_disconnect(context);
    return folder;
}

bool ps5_smb_root(const ps5_smb_config *config, char *path, size_t size)
{
    if (!config->server[0] || !config->share[0])
        return false;
    const int length =
        config->folder[0]
            ? snprintf(path, size, "smb://%s/%s/%s", config->server, config->share, config->folder)
            : snprintf(path, size, "smb://%s/%s", config->server, config->share);
    return length > 0 && (size_t)length < size;
}
