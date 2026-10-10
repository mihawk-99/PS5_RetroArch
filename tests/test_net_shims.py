"""The title's getaddrinfo, freeaddrinfo and getnameinfo (src/net_shims.c), which
RetroArch's networking needs, on the host with a stand-in DNS resolver."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class NetShims(unittest.TestCase):
    def test_name_functions(self):
        with tempfile.TemporaryDirectory() as td:
            binary = str(Path(td) / 'net-shims-test')
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-D_DEFAULT_SOURCE', '-DNET_SHIMS_PREFIX(name)=shim_##name',
                            'tests/net_shims_test.c', 'src/net_shims.c', '-o', binary], cwd=ROOT, check=True)
            subprocess.run([binary], cwd=ROOT, check=True, timeout=15)


if __name__ == '__main__':
    unittest.main()
