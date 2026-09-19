"""Execute the real loader on small relocated ELFs; reject malformed input."""
import ctypes
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class CoreLoader(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.folder = Path(cls.tmp.name)
        c = cls.folder / 'core.c'
        c.write_text('''extern int native_add(int);
int counter;
int *pointer = &counter;
int (*call_import)(int) = native_add;
int run(void) { return call_import(++*pointer); }
''')
        cls.core = cls.folder / 'fixture.so'
        subprocess.run(['cc', '-shared', '-nostdlib', '-fPIC', str(c),
                        '-Wl,-z,max-page-size=16384', '-Wl,-T,' + str(ROOT / 'tooling/native/ps5-core.ld'),
                        '-o', str(cls.core)], check=True, capture_output=True)
        data = bytearray(cls.core.read_bytes())
        data[7] = 9
        cls.core.write_bytes(data)
        cls.original = bytes(data)
        wrapper = cls.folder / 'runtime.cpp'
        wrapper.write_text('''#include <cstring>
static bool allow = true;
extern "C" void allow_import(bool value) { allow = value; }
static int native_add(int value) { return value + 40; }
extern "C" void *ps5_core_import(const char *name) {
    return allow && !std::strcmp(name, "native_add") ? reinterpret_cast<void *>(&native_add) : nullptr;
}
''')
        harness = cls.folder / 'loader.so'
        subprocess.run(['c++', '-std=c++20', '-shared', '-fPIC', '-pthread', '-O1', '-g',
                        '-Wall', '-Wextra', '-Werror', str(ROOT / 'src/core_loader_ps5.cpp'),
                        str(wrapper), '-o', str(harness)], check=True, capture_output=True)
        cls.lib = ctypes.CDLL(str(harness))
        cls.lib.ps5_core_dlopen.argtypes = [ctypes.c_char_p, ctypes.c_int]
        cls.lib.ps5_core_dlopen.restype = ctypes.c_void_p
        cls.lib.ps5_core_dlsym.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        cls.lib.ps5_core_dlsym.restype = ctypes.c_void_p
        cls.lib.ps5_core_dlclose.argtypes = [ctypes.c_void_p]
        cls.lib.ps5_core_dlerror.restype = ctypes.c_char_p
        cls.lib.allow_import.argtypes = [ctypes.c_bool]

    def setUp(self):
        self.lib.allow_import(True)

    def open(self, path):
        return self.lib.ps5_core_dlopen(str(path).encode(), 0)

    def reject(self, data, reason):
        path = self.folder / 'invalid.so'
        path.write_bytes(data)
        handle = self.open(path)
        if handle:
            self.lib.ps5_core_dlclose(handle)
        self.assertFalse(handle)
        self.assertIn(reason, self.lib.ps5_core_dlerror().decode())

    def test_relocations_imports_reference_count_and_reload(self):
        for _ in range(8):
            a = self.open(self.core)
            self.assertTrue(a, self.lib.ps5_core_dlerror())
            b = self.open(self.core)
            self.assertEqual(a, b)
            run = ctypes.CFUNCTYPE(ctypes.c_int)(self.lib.ps5_core_dlsym(a, b'run'))
            self.assertEqual(run(), 41)
            self.assertEqual(self.lib.ps5_core_dlclose(a), 0)
            self.assertEqual(run(), 42)
            self.assertFalse(self.lib.ps5_core_dlsym(b, b'absent'))
            self.assertEqual(self.lib.ps5_core_dlclose(b), 0)
        self.assertEqual(self.lib.ps5_core_dlclose(1), -1)

    def test_missing_import_is_rejected_then_valid_load_recovers(self):
        self.lib.allow_import(False)
        self.assertFalse(self.open(self.core))
        self.assertIn(b'unresolved native runtime import: native_add', self.lib.ps5_core_dlerror())
        self.lib.allow_import(True)
        handle = self.open(self.core)
        self.assertTrue(handle)
        self.lib.ps5_core_dlclose(handle)

    def test_bad_header_and_header_table_bounds(self):
        self.reject(b'not an ELF', 'file size')
        data = bytearray(self.original)
        data[7] = 0
        self.reject(data, 'expected PS5 ELF64')
        data = bytearray(self.original)
        struct.pack_into('<Q', data, 32, 2**64 - 8)
        self.reject(data, 'header tables')

    def test_bad_segments_and_unsupported_tls(self):
        phoff = struct.unpack_from('<Q', self.original, 32)[0]
        data = bytearray(self.original)
        struct.pack_into('<I', data, phoff + 4, 7)
        self.reject(data, 'writable-executable')
        data = bytearray(self.original)
        struct.pack_into('<Q', data, phoff + 8, len(data) + 0x4000)
        self.reject(data, 'load segment')
        data = bytearray(self.original)
        struct.pack_into('<I', data, phoff, 7)  # PT_TLS
        self.reject(data, 'TLS')

    def test_bad_relocation_target_and_encoding(self):
        shoff = struct.unpack_from('<Q', self.original, 40)[0]
        count = struct.unpack_from('<H', self.original, 60)[0]
        offset = next(struct.unpack_from('<Q', self.original, shoff + i * 64 + 24)[0]
                      for i in range(count)
                      if struct.unpack_from('<I', self.original, shoff + i * 64 + 4)[0] == 4)
        data = bytearray(self.original)
        struct.pack_into('<Q', data, offset, 2**64 - 8)
        self.reject(data, 'relocation destination')
        data = bytearray(self.original)
        struct.pack_into('<I', data, offset + 8, 5)  # R_X86_64_COPY
        self.reject(data, 'unsupported x86-64 relocation')
