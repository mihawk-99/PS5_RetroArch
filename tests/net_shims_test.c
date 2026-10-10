/* src/net_shims.c on the host, under test names (NET_SHIMS_PREFIX), with a stand-in
 * for the console's DNS resolver. */
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include <stdint.h>

int shim_getnameinfo_retro(const struct sockaddr *, socklen_t, char *, socklen_t, char *, socklen_t,
                           int);
int shim_getaddrinfo_retro(const char *, const char *, struct addrinfo *, struct addrinfo **);
void shim_freeaddrinfo_retro(struct addrinfo *);
#define shim_getnameinfo shim_getnameinfo_retro
#define shim_getaddrinfo shim_getaddrinfo_retro
#define shim_freeaddrinfo shim_freeaddrinfo_retro

static int lookups;
/* The console's resolver, as the test sees it: one known name. */
int net_shims_test_resolve(const char *host, uint32_t *address);
int net_shims_test_resolve(const char *host, uint32_t *address)
{
    lookups++;
    if (strcmp(host, "retroachievements.org") != 0)
        return -1;
    *address = htonl(0x68151502); /* 104.21.21.2 */
    return 0;
}

static int failures;
#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

int main(void)
{
    char host[64], service[16];
    struct sockaddr_in in = {0};
    in.sin_family = AF_INET;
    in.sin_port = htons(55435);
    inet_pton(AF_INET, "192.168.50.237", &in.sin_addr);
    CHECK(shim_getnameinfo((struct sockaddr *)&in, sizeof in, host, sizeof host, service,
                           sizeof service, NI_NUMERICHOST | NI_NUMERICSERV) == 0);
    CHECK(strcmp(host, "192.168.50.237") == 0);
    CHECK(strcmp(service, "55435") == 0);
    /* Host or service alone, as RetroArch asks for them. */
    CHECK(shim_getnameinfo((struct sockaddr *)&in, sizeof in, host, sizeof host, NULL, 0,
                           NI_NUMERICHOST) == 0);
    CHECK(shim_getnameinfo((struct sockaddr *)&in, sizeof in, NULL, 0, service, sizeof service,
                           NI_NUMERICSERV) == 0);
    /* No reverse lookup, short buffers and unknown families fail as getnameinfo does. */
    CHECK(shim_getnameinfo((struct sockaddr *)&in, sizeof in, host, sizeof host, NULL, 0,
                           NI_NAMEREQD) == EAI_NONAME);
    CHECK(shim_getnameinfo((struct sockaddr *)&in, sizeof in, host, 8, NULL, 0, 0) == EAI_OVERFLOW);
    CHECK(shim_getnameinfo((struct sockaddr *)&in, sizeof in, NULL, 0, service, 3, 0) ==
          EAI_OVERFLOW);
    CHECK(shim_getnameinfo((struct sockaddr *)&in, 4, host, sizeof host, NULL, 0, 0) == EAI_FAMILY);
    struct sockaddr_in6 in6 = {0};
    in6.sin6_family = AF_INET6;
    in6.sin6_port = htons(80);
    inet_pton(AF_INET6, "2001:db8::1", &in6.sin6_addr);
    CHECK(shim_getnameinfo((struct sockaddr *)&in6, sizeof in6, host, sizeof host, service,
                           sizeof service, 0) == 0);
    struct in6_addr back;
    CHECK(inet_pton(AF_INET6, host, &back) == 1 && memcmp(&back, &in6.sin6_addr, sizeof back) == 0);
    CHECK(strcmp(service, "80") == 0);
    /* getaddrinfo: a name through the resolver, a dotted quad without it, no node. */
    struct addrinfo hints = {0}, *found = NULL;
    hints.ai_socktype = SOCK_STREAM;
    CHECK(shim_getaddrinfo("retroachievements.org", "443", &hints, &found) == 0 && lookups == 1);
    CHECK(found && found->ai_family == AF_INET && found->ai_socktype == SOCK_STREAM &&
          !found->ai_next);
    CHECK(shim_getnameinfo(found->ai_addr, found->ai_addrlen, host, sizeof host, service,
                           sizeof service, 0) == 0);
    CHECK(strcmp(host, "104.21.21.2") == 0 && strcmp(service, "443") == 0);
    shim_freeaddrinfo(found);
    CHECK(shim_getaddrinfo("239.255.255.250", "1900", &hints, &found) == 0 && lookups == 1);
    shim_getnameinfo(found->ai_addr, found->ai_addrlen, host, sizeof host, service, sizeof service,
                     0);
    CHECK(strcmp(host, "239.255.255.250") == 0 && strcmp(service, "1900") == 0);
    shim_freeaddrinfo(found);
    hints.ai_flags = AI_PASSIVE;
    CHECK(shim_getaddrinfo(NULL, "55435", &hints, &found) == 0);
    shim_getnameinfo(found->ai_addr, found->ai_addrlen, host, sizeof host, NULL, 0, 0);
    CHECK(strcmp(host, "0.0.0.0") == 0);
    shim_freeaddrinfo(found);
    hints.ai_flags = 0;
    CHECK(shim_getaddrinfo(NULL, "80", &hints, &found) == 0);
    shim_getnameinfo(found->ai_addr, found->ai_addrlen, host, sizeof host, NULL, 0, 0);
    CHECK(strcmp(host, "127.0.0.1") == 0);
    shim_freeaddrinfo(found);
    CHECK(shim_getaddrinfo("lobby.libretro.com", NULL, &hints, &found) == EAI_NONAME && !found &&
          lookups == 2);
    hints.ai_flags = AI_NUMERICHOST;
    CHECK(shim_getaddrinfo("retroachievements.org", "443", &hints, &found) == EAI_NONAME &&
          lookups == 2);
    hints.ai_flags = 0;
    hints.ai_family = AF_INET6;
    CHECK(shim_getaddrinfo("1.2.3.4", "1", &hints, &found) == EAI_FAMILY);
    hints.ai_family = AF_INET;
    CHECK(shim_getaddrinfo("1.2.3.4", "http", &hints, &found) == EAI_SERVICE);
    CHECK(shim_getaddrinfo("1.2.3.256", "1", &hints, &found) == EAI_NONAME && lookups == 3);
    shim_freeaddrinfo(NULL);
    if (!failures)
        puts("net_shims: getaddrinfo (resolver, numeric, passive), getnameinfo and refusals OK");
    return failures != 0;
}
