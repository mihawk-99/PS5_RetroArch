"""Exercise saved /app0 names through the wrappers linked into RetroArch."""
import pathlib
import socket
import subprocess
import tempfile
import threading
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class InstalledPaths(unittest.TestCase):
    def test_bundled_service_delivery_and_readiness(self):
        with tempfile.TemporaryDirectory() as directory, socket.socket() as server:
            work = pathlib.Path(directory)
            server.bind(('127.0.0.1', 0))
            server.listen()
            server.settimeout(10)
            payload = b'\x7fELF' + bytes(range(256)) * 512
            (work / 'lapy-root-daemon.elf').write_bytes(payload)
            (work / 'test.c').write_text(r'''
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#define PS5_PATHS_START_TEST
enum ps5_elevation_status { PS5_ELEVATION_OK };
#define PS5_ELEVATION_FILESYSTEM 1
static enum ps5_elevation_status ps5_elevation_request(int c) { (void)c; abort(); }
static const char *ps5_elevation_status_name(enum ps5_elevation_status s) {
    (void)s; return "unexpected";
}
#include "ps5_paths.c"
int main(int argc, char **argv) {
    (void)argc;
    strcpy(install_root, argv[1]);
    assert(!service_ready());
    assert(start_service(NULL));
    assert(service_ready());
    return 0;
}
''')
            flags = subprocess.check_output(
                ['bash', str(ROOT / 'tools/path-wrap-flags.sh')], text=True).split()
            subprocess.run(['cc', '-DPS5_PATHS_HOST_TEST',
                            '-DPS5_LAPY_LOADER_PORT=' + str(server.getsockname()[1]),
                            '-I' + str(ROOT / 'src'), str(work / 'test.c'),
                            str(ROOT / 'src/ps5_paths_io.c'),
                            *['-Wl,' + f for f in flags], '-o', str(work / 'test')], check=True)
            received = bytearray()

            def loader():
                with server.accept()[0] as connection:
                    connection.settimeout(10)
                    while block := connection.recv(4096):
                        received.extend(block)
                    # Existing host PID plus uptime zero is a live readiness marker.
                    import os
                    (work / 'lapy-ready').write_text(f'{os.getpid()} 0\n')

            thread = threading.Thread(target=loader)
            thread.start()
            try:
                subprocess.run([str(work / 'test'), str(work)], check=True, timeout=10)
            finally:
                thread.join(timeout=10)
            self.assertFalse(thread.is_alive())
            self.assertEqual(received, payload)

    def test_legacy_files_and_two_path_operations(self):
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            (work / 'test.c').write_text(r'''
#include <assert.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define PS5_PATHS_START_TEST
enum ps5_elevation_status { PS5_ELEVATION_OK };
#define PS5_ELEVATION_FILESYSTEM 1
static enum ps5_elevation_status ps5_elevation_request(int capability) {
    (void)capability;
    abort(); /* Missing/stale readiness must never publish an elevation request. */
}
static const char *ps5_elevation_status_name(enum ps5_elevation_status status) {
    (void)status; return "unexpected";
}
#include "ps5_paths.c"
int main(int argc, char **argv) {
    (void)argc;
    strcpy(install_root, argv[1]);
    char a[PATH_MAX], b[PATH_MAX];
    assert(!strcmp(ps5_installed_path("/app0", a, sizeof a), argv[1]));
    assert(!strcmp(ps5_installed_path("/app01/file", a, sizeof a), "/app01/file"));
    assert(!strcmp(ps5_installed_path("/mnt/usb0/a", a, sizeof a), "/mnt/usb0/a"));
    assert(!strcmp(ps5_installed_path("relative", a, sizeof a), "relative"));
    assert(!ps5_installed_path("/app0/long", a, 2) && errno == ENAMETOOLONG);
    assert(!ps5_installed_path(NULL, a, sizeof a));
    assert(mkdir("/app0/new", 0700) == 0);
    FILE *f = fopen("/app0/new/a", "w");
    assert(f && fputs("saved game", f) >= 0 && fclose(f) == 0);
    assert(rename("/app0/new/a", "/app0/new/b") == 0);
    struct stat st;
    assert(stat("/app0/new/b", &st) == 0 && st.st_size == 10);
    assert((st.st_mode & 0777) == 0777);
    assert(chmod("/app0/new/b", 0600) == 0);
    int fd = open("/app0/new/b", O_RDONLY);
    assert(fd >= 0 && read(fd, a, sizeof a) == 10 && close(fd) == 0);
    assert(!memcmp(a, "saved game", 10));
    f = fopen("/app0/new/b", "r");
    assert(f && freopen("/app0/new/b", "r", f));
    assert(fclose(f) == 0);
    assert(chdir("/app0/new") == 0);
    assert(stat("b", &st) == 0);
    assert(remove("/app0/new/b") == 0 && rmdir("/app0/new") == 0);
    /* Two simultaneous mapped names must not share scratch storage. */
    const char *one = ps5_installed_path("/app0/one", a, sizeof a);
    const char *two = ps5_installed_path("/app0/two", b, sizeof b);
    assert(strstr(one, "/one") && strstr(two, "/two"));
    assert(mkdir("/app0/sce_sys", 0700) == 0);
    f = fopen("/app0/sce_sys/param.json", "w");
    assert(f && fputs("{\"titleId\":\"PPSA99169\"}", f) >= 0 && fclose(f) == 0);
    f = fopen("/app0/eboot.bin", "w"); assert(f && fclose(f) == 0);
    assert(ps5_paths_start() == 0); /* no daemon */
    f = fopen("/app0/lapy-ready", "w");
    assert(f && fputs("bad marker", f) >= 0 && fclose(f) == 0);
    assert(ps5_paths_start() == 0);
    f = fopen("/app0/lapy-ready", "w");
    assert(f && fputs("2147483647 0", f) >= 0 && fclose(f) == 0);
    assert(ps5_paths_start() == 0); /* dead daemon PID */
    f = fopen("/app0/lapy-ready", "w");
    assert(f && fprintf(f, "%ld 9223372036854775807", (long)getpid()) > 0 && fclose(f) == 0);
    assert(ps5_paths_start() == 0); /* previous boot's uptime */
    return 0;
}
''')
            flags = subprocess.check_output(
                ['bash', str(ROOT / 'tools/path-wrap-flags.sh')], text=True).split()
            self.assertNotIn('--wrap=sceSystemServiceLoadExec', flags)
            subprocess.run(['cc', '-DPS5_PATHS_HOST_TEST', '-I' + str(ROOT / 'src'),
                            str(work / 'test.c'), str(ROOT / 'src/ps5_paths_io.c'),
                            *['-Wl,' + f for f in flags], '-o', str(work / 'test')], check=True)
            subprocess.run([str(work / 'test'), str(work)], check=True)
