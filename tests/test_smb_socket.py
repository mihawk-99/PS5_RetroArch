"""Exercise libsmb2's PS5 socket adapters with deliberately blocking descriptors.

No server, credentials or console required. The baseline is extracted from the
actual generic title wrappers; the candidate links the real ps5_smb.c.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class SmbSocket(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='smb-socket-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        cls.flags = ['cc', '-std=c11', '-D_DEFAULT_SOURCE', '-O1', '-g',
                     '-Wall', '-Wextra', '-Werror', '-Isrc', 'tests/smb_socket_test.c']

    def test_title_wrapper_can_block_without_nonblocking_flag(self):
        source = (ROOT / 'src/platform_wraps.c').read_text()
        start = source.index('ssize_t __wrap_readv(')
        end = source.index('\nint __wrap_getlogin_r(', start)
        baseline = self.directory / 'baseline.c'
        baseline.write_text('#include <sys/uio.h>\n#include <unistd.h>\n' + source[start:end])
        binary = self.directory / 'baseline'
        subprocess.run(self.flags + ['-Dps5_smb_socket_readv=__wrap_readv',
                                     '-Dps5_smb_socket_writev=__wrap_writev',
                                     str(baseline), '-o', str(binary)], cwd=ROOT, check=True)
        # A missing O_NONBLOCK must reproduce a wait, not just a compile failure.
        with self.assertRaises(subprocess.TimeoutExpired):
            subprocess.run([str(binary)], capture_output=True, timeout=1, check=True)

    def test_scoped_socket_io_does_not_block(self):
        prefix = subprocess.check_output(['bash', 'tools/build-libsmb2.sh', 'host'],
                                         cwd=ROOT, text=True).strip()
        binary = self.directory / 'candidate'
        subprocess.run(self.flags + [f'-I{prefix}/include', 'src/ps5_smb.c',
                                     f'{prefix}/lib/libsmb2.a', '-lpthread',
                                     '-o', str(binary)], cwd=ROOT, check=True)
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=3, check=True)
        self.assertEqual(run.stdout.count('PASS '), 6, run.stdout)


if __name__ == '__main__':
    unittest.main()
