"""Exercise the core gate with real ELFs, including incompatible artifacts."""
import importlib.util
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('core_abi', ROOT / 'tools/check-core.py')
core_abi = importlib.util.module_from_spec(spec)
spec.loader.exec_module(core_abi)


class CoreABI(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.folder = Path(self.tmp.name)
        self.cc = shutil.which('clang') or shutil.which('cc')
        if not self.cc:
            self.skipTest('host C compiler unavailable')

    def build(self, soname='libkernel_web.sprx', omit=None):
        stub = self.folder / 'stub.so'
        subprocess.run([self.cc, '-x', 'c', '-', '-shared', '-nostdlib',
                        '-Wl,-soname,' + soname, '-o', str(stub)],
                       input='void stub(void) {}', text=True, check=True,
                       capture_output=True)
        path = self.folder / 'core.so'
        source = '\n'.join('void ' + name + '(void) {}'
                           for name in sorted(core_abi.REQUIRED) if name != omit)
        subprocess.run([self.cc, '-x', 'c', '-', '-x', 'none', str(stub),
                        '-shared', '-nostdlib', '-Wl,-z,max-page-size=16384',
                        '-o', str(path)], input=source, text=True, check=True,
                       capture_output=True)
        data = bytearray(path.read_bytes())
        data[7] = 9  # Test fixture only: mark its ELF OSABI as FreeBSD.
        path.write_bytes(data)
        return path

    def test_valid_shape_does_not_claim_console_verification(self):
        report = core_abi.inspect(self.build())
        self.assertTrue(report['abi_passed'])
        self.assertFalse(report['console_loading_verified'])
        self.assertEqual(len(report['libretro_exports']), 25)

    def test_reject_host_abi(self):
        path = self.build()
        data = bytearray(path.read_bytes())
        data[7] = 0
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'not a host library'):
            core_abi.inspect(path)

    def test_reject_payload_kernel_import(self):
        with self.assertRaisesRegex(ValueError, 'unexpected PS5 core imports'):
            core_abi.inspect(self.build(soname='libkernel_sys.sprx'))

    def test_reject_missing_callback(self):
        with self.assertRaisesRegex(ValueError, 'retro_run'):
            core_abi.inspect(self.build(omit='retro_run'))

    def test_reject_four_kib_segment(self):
        path = self.build()
        data = bytearray(path.read_bytes())
        phoff = struct.unpack_from('<Q', data, 32)[0]
        size, count = struct.unpack_from('<HH', data, 54)
        for i in range(count):
            offset = phoff + i * size
            if struct.unpack_from('<I', data, offset)[0] == 1:
                struct.pack_into('<Q', data, offset + 48, 4096)
                break
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, '16 KiB'):
            core_abi.inspect(path)

    def test_reject_truncated_header(self):
        path = self.folder / 'truncated.so'
        path.write_bytes(b'\x7fELF')
        with self.assertRaisesRegex(ValueError, 'expected ELF64'):
            core_abi.inspect(path)
