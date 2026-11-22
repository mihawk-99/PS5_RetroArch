"""Native platform setup with isolated filesystem and menu-entry capture."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class PlatformPaths(unittest.TestCase):
    def test_config_and_browser_roots(self):
        with tempfile.TemporaryDirectory() as td:
            binary = str(Path(td) / 'paths-test')
            functions = ['fopen', 'stat', 'mkdir', 'chmod', 'rename', 'remove']
            # The platform layer's headers, alone: the SDK's FreeBSD tree is
            # not a host compiler's.
            platform = Path(td) / 'platform-include'
            platform.mkdir()
            (platform / 'ps5platform').symlink_to(
                ROOT / '.deps/native/ps5-payload-sdk/target/include/ps5platform')
            # The SMB root's saved server (src/ps5_smb.c), read through the same fopen.
            smb2 = subprocess.check_output(['bash', 'tools/build-libsmb2.sh', 'host'], cwd=ROOT,
                                           text=True).strip()
            smb = str(Path(td) / 'ps5_smb.o')
            subprocess.run(['cc', '-std=c11', '-D_DEFAULT_SOURCE', '-O2', '-Wall', '-Werror', f'-I{smb2}/include',
                            '-c', 'src/ps5_smb.c', '-o', smb], cwd=ROOT, check=True)
            # The WebUI's saved settings, taken into retroarch.cfg at a start (issue 130).
            fold = str(Path(td) / 'ps5_webui_settings.o')
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-c', 'src/ps5_webui_settings.c',
                            '-o', fold], cwd=ROOT, check=True)
            subprocess.run(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-Ivendor/retroarch', '-Ivendor/retroarch/libretro-common/include',
                            f'-I{platform}', '-Isrc',
                            'tests/frontend_ps5_test.cpp', smb, fold, f'{smb2}/lib/libsmb2.a', '-lpthread', '-o', binary,
                            *[f'-Wl,--wrap={name}' for name in functions]], cwd=ROOT, check=True)
            subprocess.run([binary, str(Path(td) / 'filesystem')], cwd=ROOT,
                           check=True, timeout=10)

    def test_application_path_avoids_procfs(self):
        import importlib.util
        import re
        spec = importlib.util.spec_from_file_location('patches', ROOT / 'tools/apply-port-patches.py')
        patches = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patches)
        source = (ROOT / 'vendor/retroarch/libretro-common/file/file_path.c').read_text()
        for path, anchor, replacement, marker in patches.EDITS:
            if path == 'libretro-common/file/file_path.c' and marker not in source:
                self.assertIn(anchor, source)
                source = source.replace(anchor, replacement, 1)
        body = re.search(r'size_t fill_pathname_application_path\(char \*s, size_t len\)\n\{.*?\n\}',
                         source, re.S)[0]
        program = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
pid_t getpid(void) { abort(); }
ssize_t readlink(const char *p, char *b, size_t n) { abort(); }
size_t strlcpy(char *s, const char *p, size_t n) {
    size_t len = strlen(p);
    if (n) { size_t copied = len < n - 1 ? len : n - 1;
        memcpy(s, p, copied); s[copied] = 0; }
    return len;
}
''' + body + r'''
int main(void) {
    char buffer[64];
    assert(fill_pathname_application_path(buffer, sizeof(buffer)) == strlen("/app0/eboot.bin"));
    assert(strcmp(buffer, "/app0/eboot.bin") == 0);
    assert(fill_pathname_application_path(NULL, 0) == 0);
    char small[4];
    fill_pathname_application_path(small, sizeof(small));
    assert(strcmp(small, "/ap") == 0);
}
'''
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / 'executable.c'
            path.write_text(program)
            binary = str(Path(td) / 'executable-test')
            subprocess.run(['cc', '-std=gnu11', '-O2', str(path), '-o', binary], check=True)
            subprocess.run([binary], check=True)
