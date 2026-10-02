/* PS5 networking fixes linked into the core itself.
 *
 * The SDK routes the getaddrinfo family to a module titles do not load, so
 * every call answers EAI_FAIL and DCNet/resolver lookups die with "a
 * non-recoverable error occurred during database lookup". A title also
 * cannot make a socket nonblocking: fcntl(F_SETFL, O_NONBLOCK) and
 * ioctl(FIONBIO) are both refused with EACCES.
 *
 * The replacements below are defined in the core's own link, so its internal
 * references bind here instead of the SDK import stubs:
 *
 *   getaddrinfo/freeaddrinfo - a DNS A-record query over a kernel UDP socket
 *     (sendto/recvfrom are kernel exports and work in a title). Numeric IPv4
 *     literals short-circuit. IPv6 and non-A records are out of scope: the
 *     DCNet, DreamPi and updater endpoints are all IPv4.
 *
 *   gethostbyname - same resolver, static result buffer.
 *
 * Non-blocking sockets are handled at the call sites instead: a PS5 title
 * cannot change socket flags (fcntl(F_SETFL, O_NONBLOCK) and FIONBIO are
 * refused with EACCES) and there is no way to forward other fcntl/ioctl
 * commands if these names are shadowed, so socket_ops.ipp and
 * net_platform.h substitute a ~0 socket timeout under __PROSPERO__.
 */
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>

#define PS5_DNS_TIMEOUT_SEC 3

static int ps5_dns_query_udp(const char *hostname, struct in_addr *out)
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
        tv.tv_sec = PS5_DNS_TIMEOUT_SEC; tv.tv_usec = 0;
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

        n = sendto(fd, packet, pos, 0, (struct sockaddr *)&server, sizeof(server));
        if (n < 0)
            continue;
        n = recvfrom(fd, reply, sizeof(reply), 0, NULL, NULL);
        if (n < 12)
            continue;

        {
            const int qd = (reply[4] << 8) | reply[5];
            const int an = (reply[6] << 8) | reply[7];
            const int rcode = reply[3] & 0x0f;
            size_t rp = 12;
            if (rcode != 0)
                continue;
            /* skip the question section */
            for (i = 0; i < qd; ++i)
            {
                while (rp < (size_t)n && reply[rp])
                    rp += ((reply[rp] & 0xc0) == 0xc0) ? 2 : reply[rp] + 1;
                rp += 5; /* terminal 0 + qtype + qclass */
            }
            for (i = 0; i < an; ++i)
            {
                /* answer name: usually a compression pointer, but a literal
                 * name is legal too */
                while (rp < (size_t)n && reply[rp])
                {
                    if ((reply[rp] & 0xc0) == 0xc0)
                    {
                        rp += 2;
                        goto name_done;
                    }
                    rp += reply[rp] + 1;
                }
name_done:
                if (rp + 10 > (size_t)n)
                    break;
                {
                    const int type  = (reply[rp+2] << 8) | reply[rp+3];
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

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **result)
{
    struct in_addr address;
    const int family = hints ? hints->ai_family : AF_UNSPEC;
    int socktype = SOCK_STREAM;
    int protocol = IPPROTO_TCP;

    if (!result)
        return EAI_FAIL;
    *result = NULL;

    if (family != AF_UNSPEC && family != AF_INET)
        return EAI_FAMILY;
    if (hints)
    {
        if (hints->ai_socktype == SOCK_DGRAM)
        {
            socktype = SOCK_DGRAM;
            protocol = IPPROTO_UDP;
        }
        else if (hints->ai_socktype)
        {
            socktype = hints->ai_socktype;
            protocol = hints->ai_protocol;
        }
    }

    if (!node)
        address.s_addr = (hints && (hints->ai_flags & AI_PASSIVE))
            ? INADDR_ANY : htonl(INADDR_LOOPBACK);
    /* inet_pton resolves to __inet_pton, a binding the title exports;
       inet_aton would import __inet_aton, which it does not. */
    else if (inet_pton(AF_INET, node, &address) != 1 && ps5_dns_query_udp(node, &address) < 0)
        return EAI_NONAME;

    {
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
        sockaddr->sin_addr = address;
        if (service)
        {
            char *end = NULL;
            const long port = strtol(service, &end, 10);
            sockaddr->sin_port = (end && *end == '\0')
                ? htons((unsigned short)port) : 0;
        }
        info->ai_family   = AF_INET;
        info->ai_socktype = socktype;
        info->ai_protocol = protocol;
        info->ai_addrlen  = sizeof(*sockaddr);
        info->ai_addr     = (struct sockaddr *)sockaddr;
        *result = info;
    }
    return 0;
}

void freeaddrinfo(struct addrinfo *info)
{
    while (info)
    {
        struct addrinfo *next = info->ai_next;
        free(info->ai_addr);
        free(info->ai_canonname);
        free(info);
        info = next;
    }
}

struct hostent *gethostbyname(const char *name)
{
    static struct hostent host;
    static struct in_addr addr;
    static struct in_addr *addr_list[2];
    static char *aliases[1] = { NULL };
    struct addrinfo *info = NULL;

    if (getaddrinfo(name, NULL, NULL, &info) || !info)
        return NULL;
    addr = ((struct sockaddr_in *)info->ai_addr)->sin_addr;
    freeaddrinfo(info);
    memset(&host, 0, sizeof(host));
    host.h_name      = (char *)name;
    host.h_aliases   = aliases;
    host.h_addrtype  = AF_INET;
    host.h_length    = sizeof(addr);
    addr_list[0]     = &addr;
    addr_list[1]     = NULL;
    host.h_addr_list = (char **)addr_list;
    return &host;
}
