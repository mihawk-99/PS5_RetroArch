// Temporary, opt-in early-main diagnostic. No frontend or worker is started.
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ps5platform/elevation.h>
extern "C" int sceNetInit(void);
extern "C" int sceNetSocket(const char *, int, int, int);

static void connection_probe(FILE *log, const char *phase, const char *label,
                             const char *ip, unsigned port, bool sony)
{
    errno = 0;
    int fd = sony ? sceNetSocket("ra-probe", AF_INET, SOCK_STREAM, 0) : socket(AF_INET, SOCK_STREAM, 0);
    fprintf(log, "phase=%s target=%s api=%s socket=%d errno=%d\n", phase, label, sony ? "sceNetSocket" : "socket", fd, errno);
    if (fd < 0) return;
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0), saved = errno;
    fprintf(log, "phase=%s target=%s getfl=%d errno=%d\n", phase, label, flags, saved);
    errno = 0;
    int rc = flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    saved = errno;
    fprintf(log, "phase=%s target=%s nonblock=%d errno=%d\n", phase, label, rc, saved);
    if (rc < 0) { close(fd); return; }
    struct sockaddr_in addr = {};
    addr.sin_len = sizeof(addr); addr.sin_family = AF_INET; addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) { close(fd); return; }
    errno = 0;
    rc = connect(fd, (const struct sockaddr *)&addr, sizeof(addr)); saved = errno;
    fprintf(log, "phase=%s target=%s connect=%d errno=%d\n", phase, label, rc, saved);
    bool connected = rc == 0;
    if (rc < 0 && (saved == EINPROGRESS || saved == EWOULDBLOCK)) {
        struct pollfd p = {fd, POLLOUT, 0};
        errno = 0; rc = poll(&p, 1, 3000); saved = errno;
        fprintf(log, "phase=%s target=%s poll=%d revents=%d errno=%d\n", phase, label, rc, p.revents, saved);
        if (rc > 0) {
            int error = -1; socklen_t size = sizeof(error);
            errno = 0; rc = getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size); saved = errno;
            fprintf(log, "phase=%s target=%s getsockopt=%d so_error=%d errno=%d\n", phase, label, rc, error, saved);
            connected = rc == 0 && error == 0;
        }
    }
    fprintf(log, "phase=%s target=%s api=%s connected=%d\n", phase, label, sony ? "sceNetSocket" : "socket", connected);
    close(fd); fflush(log);
}

static bool network_elevation_probe()
{
    FILE *control = fopen("/app0/tests/network-elevation.control", "r");
    if (!control) return false;
    int elevate = 0; char lan[64] = {}, ra[64] = {}, media[64] = {}; unsigned port = 0;
    const int fields = fscanf(control, "%d %63s %u %63s %63s", &elevate, lan, &port, ra, media);
    fclose(control);
    FILE *log = fopen("/app0/tests/network-elevation.log", "w");
    if (!log) return true;
    setvbuf(log, nullptr, _IONBF, 0);
    if (fields != 5 || port == 0 || port > 65535) { fprintf(log, "invalid_control\n"); fclose(log); return true; }
    fprintf(log, "probe=1 pid=%d elevate=%d net_init=%d\n", getpid(), elevate, sceNetInit());
    for (int step = 0; step < 2; ++step) {
        const char *phase = step ? "after" : "before";
        fprintf(log, "phase=%s snapshot uid=%u euid=%u gid=%u egid=%u app0=%d data=%d\n", phase,
                getuid(), geteuid(), getgid(), getegid(), access("/app0", R_OK), access("/data", R_OK));
        sleep(8); // host observes credentials while this single-threaded title is alive
        for (int sony = 0; sony < 2; ++sony) {
            connection_probe(log, phase, "lan", lan, port, sony);
            connection_probe(log, phase, "achievements", ra, 443, sony);
            connection_probe(log, phase, "media", media, 443, sony);
        }
        if (step == 0) {
            if (!elevate) break;
            fprintf(log, "elevation_request\n");
            const auto status = ps5_elevation_request(PS5_ELEVATION_FILESYSTEM);
            fprintf(log, "elevation_status=%d name=%s\n", status, ps5_elevation_status_name(status));
            if (status != PS5_ELEVATION_OK) break;
        }
    }
    fprintf(log, "probe_complete\n"); fclose(log);
    return true;
}
