"""Run the pinned SDK C client against an already accessible /data directory."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get('PS5_PAYLOAD_SDK_FORK', ROOT.parent / 'PS5_PayloadSDK'))

HARNESS = r'''
#define _GNU_SOURCE
#include <ps5platform/elevation.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static atomic_int granted = 0, early_probe = 0;
static char request[512];
uid_t __wrap_getuid(void) { return atomic_load(&granted) ? 0 : 1; }
int __real_open(const char *, int, ...);
int __wrap_open(const char *path, int flags, ...) {
    if (strstr(path, "/.lapy_probe_") && !atomic_load(&granted))
        atomic_store(&early_probe, 1);
    if (flags & O_CREAT) {
        va_list args; va_start(args, flags); mode_t mode = va_arg(args, mode_t); va_end(args);
        return __real_open(path, flags, mode);
    }
    return __real_open(path, flags);
}
static void *daemon_thread(void *ignored) {
    (void)ignored;
    for (unsigned i = 0; access(request, F_OK) && i < 1000; ++i) usleep(1000);
    assert(access(request, F_OK) == 0);
    usleep(60000); /* Lapy owns preflight before granting root. */
    atomic_store(&granted, 1);
    return NULL;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    char directory[] = "/tmp/ps5-elevation-wait-XXXXXX", result[512];
    assert(mkdtemp(directory));
    snprintf(request, sizeof(request), "%s/request", directory);
    snprintf(result, sizeof(result), "%s/result", directory);
    /* The title can already write here, without having root credentials. */
    struct ps5_elevation_config config = {request, directory, result, directory, 30, 10000};
    pthread_t daemon;
    int with_daemon = !strcmp(argv[1], "grant");
    if (with_daemon) assert(!pthread_create(&daemon, NULL, daemon_thread, NULL));
    enum ps5_elevation_status status = ps5_elevation_request_with(&config, PS5_ELEVATION_FILESYSTEM);
    if (with_daemon) {
        assert(!pthread_join(daemon, NULL));
        assert(status == PS5_ELEVATION_OK);
    } else {
        assert(status == PS5_ELEVATION_TIMEOUT);
    }
    assert(!atomic_load(&early_probe));
    unlink(request); unlink(result); assert(!rmdir(directory));
    puts(with_daemon ? "wait then prove PASS" : "preexisting access is not elevation PASS");
}
'''

class ElevationWait(unittest.TestCase):
    def setUp(self):
        if not shutil.which('cc') or not (SDK / '.git').exists():
            self.skipTest('requires a host C compiler and the pinned SDK checkout')

    def run_case(self, mode):
        pin = re.search(r'^sdk_revision=([0-9a-f]{40})$',
                        (ROOT/'tools/setup-native-dependencies.sh').read_text(), re.M)[1]
        original = subprocess.check_output(['git', '-C', str(SDK), 'show',
                                            pin+':platform/src/elevation.c'], text=True)
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            source = work/'platform/src/elevation.c'
            source.parent.mkdir(parents=True)
            source.write_text(original)
            subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-d', str(work),
                            '-i', str(ROOT/'platform/sdk/elevation-root-proof.patch')],
                           check=True, capture_output=True)
            harness = work/'harness.c'; harness.write_text(HARNESS)
            binary = work/'test'
            subprocess.run(['cc', '-std=gnu11', '-O1', '-Wall', '-Wextra', '-Werror', '-pthread',
                            '-I'+str(SDK/'platform/include'), str(source), str(harness),
                            '-Wl,--wrap=getuid', '-Wl,--wrap=open', '-o', str(binary)],
                           check=True, capture_output=True)
            return subprocess.run([str(binary), mode], check=True, capture_output=True,
                                  text=True, timeout=5).stdout

    def test_no_filesystem_probe_during_credential_preflight(self):
        self.assertIn('wait then prove PASS', self.run_case('grant'))

    def test_existing_data_access_without_elevation_times_out(self):
        self.assertIn('preexisting access is not elevation PASS', self.run_case('missing'))

if __name__ == '__main__':
    unittest.main()
