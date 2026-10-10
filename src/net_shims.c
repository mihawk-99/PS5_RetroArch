/*
 * PS5 RetroArch - name lookups for RetroArch's networking (netplay, RetroAchievements,
 * HTTP), which a title cannot do through libc: getaddrinfo is linked to a resolver that
 * refuses every lookup (the SDK routes the real one to a module titles do not load) and
 * no module exports getnameinfo. RetroArch calls both through libretro-common's
 * getaddrinfo_retro, freeaddrinfo_retro and getnameinfo_retro; these replace them
 * (--wrap, tools/build-title.sh: wrapping getaddrinfo itself still bound the refuser,
 * which the SDK's libc defines under that name), and getaddrinfo asks the console's own
 * DNS resolver (libSceNet's sceNetResolverStartNtoa). tests/net_shims_test.c checks them
 * on the host with a stand-in resolver. IPv4 only, as RetroArch is configured here.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#ifndef NET_SHIMS_PREFIX
#define NET_SHIMS_PREFIX(name) __wrap_##name
#endif

int NET_SHIMS_PREFIX(getaddrinfo_retro)(const char *node, const char *service,
                                        struct addrinfo *hints, struct addrinfo **result);
void NET_SHIMS_PREFIX(freeaddrinfo_retro)(struct addrinfo *info);
int NET_SHIMS_PREFIX(getnameinfo_retro)(const struct sockaddr *address, socklen_t length,
                                        char *host, socklen_t host_length, char *service,
                                        socklen_t service_length, int flags);

#ifdef __PROSPERO__
int sceNetInit(void);
int sceNetPoolCreate(const char *name, int size, int flags);
int sceNetResolverCreate(const char *name, int pool, int flags);
int sceNetResolverStartNtoa(int resolver, const char *host, uint32_t *address, int timeout_us,
                            int retries, int flags);
int sceNetResolverDestroy(int resolver);

void RARCH_LOG(const char *fmt, ...);
void RARCH_ERR(const char *fmt, ...);
#define NET_LOG RARCH_LOG
#define NET_ERR RARCH_ERR

static int net_pool = -1;
static void make_net_pool(void)
{
    (void)sceNetInit(); /* Already done by the scraper's HTTP client, or done here. */
    net_pool = sceNetPoolCreate("retroarch-dns", 16 * 1024, 0);
}

/* The console's DNS: 0 when the name resolved, the address in network order. */
static int resolve_name(const char *host, uint32_t *address)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, make_net_pool);
    if (net_pool < 0)
    {
        NET_ERR("[PS5 net] DNS for %s: no net pool (0x%08x)\n", host, (unsigned)net_pool);
        return -1;
    }
    const int resolver = sceNetResolverCreate("retroarch", net_pool, 0);
    if (resolver < 0)
    {
        NET_ERR("[PS5 net] DNS for %s: sceNetResolverCreate 0x%08x\n", host, (unsigned)resolver);
        return -1;
    }
    const int status = sceNetResolverStartNtoa(resolver, host, address, 10 * 1000 * 1000, 2, 0);
    sceNetResolverDestroy(resolver);
    if (status < 0)
    {
        NET_ERR("[PS5 net] DNS for %s: sceNetResolverStartNtoa 0x%08x\n", host, (unsigned)status);
        return -1;
    }
    const unsigned char *b = (const unsigned char *)address;
    NET_LOG("[PS5 net] DNS %s -> %u.%u.%u.%u\n", host, b[0], b[1], b[2], b[3]);
    return 0;
}
#else
/* The host test supplies the resolver. */
int net_shims_test_resolve(const char *host, uint32_t *address);
#define resolve_name net_shims_test_resolve
#endif

/* A dotted quad ("192.168.50.237") in network order; inet_pton is not relied on. */
static int dotted_quad(const char *text, uint32_t *address)
{
    unsigned part[4];
    char end;
    if (sscanf(text, "%u.%u.%u.%u%c", &part[0], &part[1], &part[2], &part[3], &end) != 4)
        return 0;
    for (int i = 0; i < 4; ++i)
        if (part[i] > 255)
            return 0;
    *address = htonl((part[0] << 24) | (part[1] << 16) | (part[2] << 8) | part[3]);
    return 1;
}

/* One IPv4 answer, as RetroArch's callers read it (ai_addr, ai_family, ai_socktype,
 * ai_protocol): a name through the console's resolver, a dotted quad as it is, no
 * node as the wildcard (AI_PASSIVE) or loopback address, a service as a port number. */
int NET_SHIMS_PREFIX(getaddrinfo_retro)(const char *node, const char *service,
                                        struct addrinfo *hints, struct addrinfo **result)
{
    const int flags = hints ? hints->ai_flags : 0, family = hints ? hints->ai_family : AF_UNSPEC;
    uint32_t address;
    unsigned long port = 0;
    if (!result || (!node && !service))
        return EAI_NONAME;
    *result = NULL;
    if (family != AF_UNSPEC && family != AF_INET)
        return EAI_FAMILY;
    if (service && *service)
    {
        char *end;
        port = strtoul(service, &end, 10);
        if (*end || port > 65535)
            return EAI_SERVICE; /* Only numeric services: RetroArch passes nothing else. */
    }
    if (!node)
        address = htonl((flags & AI_PASSIVE) ? INADDR_ANY : INADDR_LOOPBACK);
    else if (!dotted_quad(node, &address))
    {
        if ((flags & AI_NUMERICHOST) || resolve_name(node, &address) != 0)
            return EAI_NONAME;
    }
    struct
    {
        struct addrinfo info;
        struct sockaddr_in in;
    } *answer = calloc(1, sizeof *answer);
    if (!answer)
        return EAI_MEMORY;
    answer->in.sin_family = AF_INET;
    answer->in.sin_port = htons((uint16_t)port);
    answer->in.sin_addr.s_addr = address;
    answer->info.ai_family = AF_INET;
    answer->info.ai_socktype = hints ? hints->ai_socktype : 0;
    answer->info.ai_protocol = hints ? hints->ai_protocol : 0;
    answer->info.ai_addrlen = sizeof answer->in;
    answer->info.ai_addr = (struct sockaddr *)&answer->in;
    *result = &answer->info;
    return 0;
}

/* Each answer above is one allocation, its address inside it. */
void NET_SHIMS_PREFIX(freeaddrinfo_retro)(struct addrinfo *info)
{
    while (info)
    {
        struct addrinfo *next = info->ai_next;
        free(info);
        info = next;
    }
}

/* Numeric only, which is all RetroArch asks for: a host name lookup (NI_NAMEREQD)
 * fails as a resolver without a name would. */
int NET_SHIMS_PREFIX(getnameinfo_retro)(const struct sockaddr *address, socklen_t length,
                                        char *host, socklen_t host_length, char *service,
                                        socklen_t service_length, int flags)
{
    char text[64];
    unsigned port;
    if (!address || (flags & NI_NAMEREQD))
        return EAI_NONAME;
    if (address->sa_family == AF_INET && length >= (socklen_t)sizeof(struct sockaddr_in))
    {
        const struct sockaddr_in *in = (const struct sockaddr_in *)address;
        const unsigned char *b = (const unsigned char *)&in->sin_addr;
        snprintf(text, sizeof text, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
        port = ntohs(in->sin_port);
    }
    else if (address->sa_family == AF_INET6 && length >= (socklen_t)sizeof(struct sockaddr_in6))
    {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)address;
        const unsigned char *b = (const unsigned char *)&in6->sin6_addr;
        int at = 0;
        for (int i = 0; i < 16; i += 2)
            at += snprintf(text + at, sizeof text - (size_t)at, i ? ":%x" : "%x",
                           (b[i] << 8) | b[i + 1]);
        port = ntohs(in6->sin6_port);
    }
    else
        return EAI_FAMILY;
    if (host && host_length)
    {
        if (strlen(text) >= (size_t)host_length)
            return EAI_OVERFLOW;
        strcpy(host, text);
    }
    if (service && service_length &&
        snprintf(service, service_length, "%u", port) >= (int)service_length)
        return EAI_OVERFLOW;
    return 0;
}

#ifdef __PROSPERO__
/* RetroArch's own getnameinfo_retro, replaced above, still names getnameinfo, which no
 * module exports (--wrap=getnameinfo binds it here, and nothing calls it). */
int __wrap_getnameinfo(const struct sockaddr *address, socklen_t length, char *host,
                       size_t host_length, char *service, size_t service_length, int flags);
int __wrap_getnameinfo(const struct sockaddr *address, socklen_t length, char *host,
                       size_t host_length, char *service, size_t service_length, int flags)
{
    return NET_SHIMS_PREFIX(getnameinfo_retro)(address, length, host, (socklen_t)host_length,
                                               service, (socklen_t)service_length, flags);
}
#endif

#ifdef __PROSPERO__
/* HTTPS (RetroArch's mbedTLS 2.6) seeds its random generator from /dev/urandom, which a
 * title is not shown to be able to open, and one failing source fails every TLS
 * connection. The title's libc arc4random_buf is what the WebUI already draws its
 * session tokens from; --wrap=mbedtls_platform_entropy_poll (tools/build-title.sh)
 * makes it mbedTLS's platform source. */
int __wrap_mbedtls_platform_entropy_poll(void *data, unsigned char *output, size_t length,
                                         size_t *written);
int __wrap_mbedtls_platform_entropy_poll(void *data, unsigned char *output, size_t length,
                                         size_t *written)
{
    (void)data;
    arc4random_buf(output, length);
    *written = length;
    return 0;
}
#endif

#ifdef __PROSPERO__
/* What each step of an HTTPS request returned, in retroarch.log: RetroArch itself only
 * says "http_task returned -1". Bound by --wrap (tools/build-title.sh). */
#include <errno.h>
#include <stdbool.h>
void *__real_ssl_socket_init(int fd, const char *domain);
int __real_ssl_socket_connect(void *state, void *address, bool timeout, bool nonblock);
bool __real_socket_connect_with_timeout(int fd, void *address, int timeout);
int __real_mbedtls_ssl_handshake(void *ssl);
void *__wrap_ssl_socket_init(int fd, const char *domain);
int __wrap_ssl_socket_connect(void *state, void *address, bool timeout, bool nonblock);
bool __wrap_socket_connect_with_timeout(int fd, void *address, int timeout);
int __wrap_mbedtls_ssl_handshake(void *ssl);

void *__wrap_ssl_socket_init(int fd, const char *domain)
{
    void *state = __real_ssl_socket_init(fd, domain);
    if (!state)
        NET_ERR("[PS5 net] TLS setup for %s failed (random seed or certificates)\n",
                domain ? domain : "?");
    return state;
}
int __wrap_ssl_socket_connect(void *state, void *address, bool timeout, bool nonblock)
{
    errno = 0;
    const int result = __real_ssl_socket_connect(state, address, timeout, nonblock);
    if (result < 0)
        NET_ERR("[PS5 net] TLS connect returned %d (errno %d)\n", result, errno);
    else
        NET_LOG("[PS5 net] TLS connected\n");
    return result;
}
bool __wrap_socket_connect_with_timeout(int fd, void *address, int timeout)
{
    errno = 0;
    const bool connected = __real_socket_connect_with_timeout(fd, address, timeout);
    if (!connected)
        NET_ERR("[PS5 net] connect failed (errno %d)\n", errno);
    return connected;
}
int __wrap_mbedtls_ssl_handshake(void *ssl)
{
    const int result = __real_mbedtls_ssl_handshake(ssl);
    if (result < 0 && result != -0x6900 && result != -0x6880) /* WANT_READ, WANT_WRITE */
        NET_ERR("[PS5 net] TLS handshake -0x%04x\n", (unsigned)-result);
    return result;
}
#endif
