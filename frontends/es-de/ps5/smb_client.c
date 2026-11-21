/* Reuse RetroArch's corrected read-only SMB client, without linking its renderer. */
#include <stdarg.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>

void RARCH_LOG(const char* fmt, ...) {
    va_list args; va_start(args, fmt); vfprintf(stderr, fmt, args); va_end(args);
}
void RARCH_ERR(const char* fmt, ...) {
    va_list args; va_start(args, fmt); vfprintf(stderr, fmt, args); va_end(args);
}

#define ps5_smb_getaddrinfo ps5_esde_unused_getaddrinfo
#define ps5_smb_freeaddrinfo ps5_esde_unused_freeaddrinfo
#include "../../../src/ps5_smb.c"
#undef ps5_smb_getaddrinfo
#undef ps5_smb_freeaddrinfo
#include "../../../src/ps5_smb_vfs.c"
#define NET_SHIMS_RESOLVER_ONLY 1
#include "../../../src/net_shims.c"

int ps5_smb_getaddrinfo(const char* node, const char* service,
                       const struct addrinfo* hints, struct addrinfo** result) {
    struct addrinfo copy;
    if (hints) copy = *hints;
    return __wrap_getaddrinfo_retro(node, service, hints ? &copy : NULL, result);
}
void ps5_smb_freeaddrinfo(struct addrinfo* info) { __wrap_freeaddrinfo_retro(info); }
struct protoent* __wrap_getprotobyname(const char* name) {
    static char tcp_name[] = "tcp";
    static char* aliases[] = {NULL};
    static struct protoent tcp = {tcp_name, aliases, IPPROTO_TCP};
    return name && !strcmp(name, "tcp") ? &tcp : NULL;
}
int __wrap_getlogin_r(char* name, size_t size) { (void)name; (void)size; return ENXIO; }
