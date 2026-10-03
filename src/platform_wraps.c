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

/* DNS. The SDK routes the getaddrinfo family to a module titles do not load -
 * every call answers EAI_FAIL, which is what killed the Online Updater's first
 * fetch. Resolution tries libSceNet's resolver (the title imports that module
 * for sceNetInit) and, when the sandbox denies it EADDRNOTAVAIL as it does in
 * an installed title, falls back to a plain DNS query over a kernel UDP
 * socket. IPv4 only, hostname or numeric: net_http is the caller that
 * matters. */

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int sceNetPoolCreate(const char *name, unsigned int size, int flags);
int sceNetResolverCreate(const char *name, int memid, int flags);
int sceNetResolverStartNtoa(int rid, const char *hostname, void *addr,
                          int timeout, int retry, int flags);
int sceNetResolverGetError(int rid, int *result);
int sceNetResolverDestroy(int rid);

static int dns_pool = -1;
static int dns_resolver = -1;

static int dns_resolver_id(void)
{
    if (dns_resolver < 0)
    {
        if (dns_pool < 0)
            dns_pool = sceNetPoolCreate("ra-dns", 32 * 1024, 0);
        if (dns_pool < 0)
            return -1;
        dns_resolver = sceNetResolverCreate("ra-dns", dns_pool, 0);
    }
    return dns_resolver;
}

int ps5_dns_getaddrinfo(const char *node, const char *service,
                        const struct addrinfo *hints, struct addrinfo **result);
void ps5_dns_freeaddrinfo(struct addrinfo *info);
__attribute__((visibility("hidden")))
int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **result)
{
    return ps5_dns_getaddrinfo(node, service, hints, result);
}

/* The title sandbox answers EADDRNOTAVAIL to sceNetResolverStartNtoa even
 * with the interface up (sceNetCtl state 3) - homebrew ELFs outside a title
 * resolve fine, an installed title does not. Kernel UDP sockets carry no such
 * restriction, so DNS is done on the wire directly: a type-A query to a public
 * resolver over sendto/recvfrom. */


static int dns_query_udp(const char *hostname, struct in_addr *out)
{
    static const unsigned char servers[][4] = {{1,1,1,1}, {8,8,8,8}};
    unsigned char packet[512];
    unsigned char reply[512];
    size_t pos = 0;
    int fd, i, s;

    /* header: id, flags RD, qd=1 */
    packet[pos++] = 0x5a; packet[pos++] = 0x5a;
    packet[pos++] = 0x01; packet[pos++] = 0x00;
    packet[pos++] = 0x00; packet[pos++] = 0x01;
    packet[pos++] = 0x00; packet[pos++] = 0x00;
    packet[pos++] = 0x00; packet[pos++] = 0x00;
    packet[pos++] = 0x00; packet[pos++] = 0x00;
    /* qname: labels */
    {
        const char *p = hostname;
        while (*p)
        {
            const char *dot = strchr(p, '.');
            size_t len = dot ? (size_t)(dot - p) : strlen(p);
            if (len == 0 || len > 63 || pos + len + 6 >= sizeof(packet))
                return -1;
            packet[pos++] = (unsigned char)len;
            memcpy(packet + pos, p, len);
            pos += len;
            if (!dot)
                break;
            p = dot + 1;
        }
    }
    packet[pos++] = 0x00;
    packet[pos++] = 0x00; packet[pos++] = 0x01; /* A */
    packet[pos++] = 0x00; packet[pos++] = 0x01; /* IN */

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;

    {
        struct timeval tv;
        tv.tv_sec = 3; tv.tv_usec = 0;
        (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    for (s = 0; s < 2; ++s)
    {
        struct sockaddr_in server;
        ssize_t n;
        memset(&server, 0, sizeof(server));
        server.sin_len = sizeof(server);
        server.sin_family = AF_INET;
        server.sin_port = htons(53);
        memcpy(&server.sin_addr, servers[s], 4);

        n = sendto(fd, packet, pos, 0, &server, sizeof(server));
        if (n < 0)
            continue;
        n = recvfrom(fd, reply, sizeof(reply), 0, NULL, NULL);
        if (n < 12)
            continue;

        /* parse answers: header 12, then the question section */
        {
            const int qd = (reply[4] << 8) | reply[5];
            const int an = (reply[6] << 8) | reply[7];
            int rcode = reply[3] & 0x0f;
            size_t rp = 12;
            if (rcode != 0)
                continue;
            for (i = 0; i < qd; ++i)
            {
                while (rp < (size_t)n && reply[rp])
                    rp += ((reply[rp] & 0xc0) == 0xc0) ? 2 : reply[rp] + 1;
                rp += 5; /* terminal 0 + qtype + qclass */
            }
            for (i = 0; i < an; ++i)
            {
                /* name: a 0xc0 compression pointer is the whole name; a label
                 * run ends at a zero byte. The pointer case needs no extra +1. */
                while (rp < (size_t)n && reply[rp])
                {
                    if ((reply[rp] & 0xc0) == 0xc0)
                    {
                        rp += 2;
                        goto name_done;
                    }
                    rp += reply[rp] + 1;
                }
                rp += 1;
name_done:
                if (rp + 10 > (size_t)n)
                    break;
                {
                    const int type = (reply[rp+2] << 8) | reply[rp+3];
                    const int rdlen = (reply[rp+8] << 8) | reply[rp+9];
                    rp += 10;
                    if (type == 1 && rdlen == 4 && rp + 4 <= (size_t)n)
                    {
                        memcpy(&out->s_addr, reply + rp, 4);
                        close(fd);
                        return 0;
                    }
                    rp += rdlen;
                }
            }
        }
    }
    close(fd);
    return -1;
}

int ps5_dns_getaddrinfo(const char *node, const char *service,
                        const struct addrinfo *hints, struct addrinfo **result)
{
    struct in_addr address;
    const int family = hints ? hints->ai_family : AF_UNSPEC;

    if (!result)
        return EAI_FAIL;
    *result = NULL;

    if (family != AF_UNSPEC && family != AF_INET)
        return EAI_FAMILY;

    if (!node)
    {
        address.s_addr = (hints && (hints->ai_flags & AI_PASSIVE))
            ? INADDR_ANY : htonl(INADDR_LOOPBACK);
    }
    else if (inet_aton(node, &address) == 0)
    {
        const int rid = dns_resolver_id();
        int resolver_error = 0;
        int ntoa;
        if (rid < 0)
            return EAI_FAIL;
        ntoa = sceNetResolverStartNtoa(rid, node, &address,
                                     10 * 1000 * 1000, 0, 0);
        if (ntoa < 0)
        {
            (void)sceNetResolverGetError(rid, &resolver_error);
            if (dns_query_udp(node, &address) < 0)
                return EAI_NONAME;
        }
    }

    struct addrinfo *info = calloc(1, sizeof(*info));
    struct sockaddr_in *sockaddr = calloc(1, sizeof(*sockaddr));
    if (!info || !sockaddr)
    {
        free(info);
        free(sockaddr);
        return EAI_MEMORY;
    }
    sockaddr->sin_len = sizeof(*sockaddr);
    sockaddr->sin_family = AF_INET;
    sockaddr->sin_port = htons(service
            ? (unsigned short)strtol(service, NULL, 10) : 0);
    sockaddr->sin_addr = address;

    info->ai_family = AF_INET;
    info->ai_socktype = (hints && hints->ai_socktype)
            ? hints->ai_socktype : SOCK_STREAM;
    info->ai_protocol = hints ? hints->ai_protocol : IPPROTO_TCP;
    info->ai_addrlen = sizeof(*sockaddr);
    info->ai_addr = (struct sockaddr *)sockaddr;
    *result = info;
    return 0;
}

__attribute__((visibility("hidden")))
void freeaddrinfo(struct addrinfo *info)
{
    ps5_dns_freeaddrinfo(info);
}

void ps5_dns_freeaddrinfo(struct addrinfo *info)
{
    while (info)
    {
        struct addrinfo *next = info->ai_next;
        free(info->ai_addr);
        free(info);
        info = next;
    }
}
