"""Exercise the core's RGB565 packing, cropped frames and upload byte order."""
import ctypes
import hashlib
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
ARCHIVE = ROOT / '.deps/downloads/genesis-plus-gx-c2838c7dc4236fc2fe94e5dbd08b41486067918e.tar.gz'


class GenesisVideo(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        directory = Path(cls.tmp.name)
        source = directory / 'adapter.c'
        source.write_text('#include "' + str(ROOT / 'tooling/genesis-plus-gx/ps5-video.h') + '"\n'
                          'bool convert(uint32_t *d, size_t n, const uint16_t *s, '
                          'size_t pixels, size_t p, size_t offset, unsigned w, unsigned h) '
                          '{ return ps5_genesis_frame(d, n, s, pixels, p, offset, w, h); }\n')
        obj = directory / 'adapter.o'
        subprocess.run(['cc', '-std=c99', '-fPIC', '-Wall', '-Wextra', '-Werror',
                        '-c', str(source), '-o', str(obj)], check=True)
        lib = directory / 'video.so'
        subprocess.run(['c++', '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror',
                        str(obj), '-o', str(lib)], check=True)
        cls.api = ctypes.CDLL(str(lib))
        cls.convert = cls.api.convert
        cls.convert.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                                ctypes.c_size_t, ctypes.c_size_t, ctypes.c_size_t,
                                ctypes.c_uint, ctypes.c_uint]
        cls.convert.restype = ctypes.c_bool

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_all_colours_from_upstream_packing_to_gpu(self):
        if not ARCHIVE.exists():
            self.skipTest('build Genesis Plus GX first to fetch the pinned source')
        self.assertEqual(hashlib.sha256(ARCHIVE.read_bytes()).hexdigest(),
                         '7ba2eab9d6dae71bb42e8208573300ad3475a4263e860178c4d19c36d85fc92b')
        with tarfile.open(ARCHIVE) as tar:
            member = next(m for m in tar.getmembers() if m.name.endswith('/core/vdp_render.h'))
            renderer = tar.extractfile(member).read().decode().replace('\r\n', '\n')
        packing = renderer.split('#elif defined(USE_16BPP_RENDERING)\n')[1].split('\n')[0]
        directory = Path(self.tmp.name)
        source = directory / 'packing.c'
        source.write_text('#include <stdint.h>\n' + packing + '\n'
                          'void pack(uint16_t *p) { for (unsigned r=0; r<32; ++r) '
                          'for (unsigned g=0; g<64; ++g) for (unsigned b=0; b<32; ++b) '
                          '*p++ = PIXEL(r,g,b); }\n')
        lib = directory / 'packing.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-Wall', '-Werror', str(source),
                        '-o', str(lib)], check=True)
        api = ctypes.CDLL(str(lib))
        api.pack.argtypes = [ctypes.c_void_p]
        src = (ctypes.c_uint16 * 65536)()
        api.pack(src)
        original = bytes(src)
        core = (ctypes.c_uint32 * 65536)()
        self.assertTrue(self.convert(core, 65536, src, 65536, 512, 0, 256, 256))
        # These are the bytes an upload copies into a B8G8R8A8 image, which is
        # libretro's little-endian XRGB8888 layout: blue, green, red, unused.
        pixels = (ctypes.c_uint8 * (65536 * 4)).from_buffer(core)
        for pixel in range(65536):
            r, g, b = pixel >> 11, (pixel >> 5) & 63, pixel & 31
            self.assertEqual(list(pixels[pixel * 4:pixel * 4 + 4]),
                             [b * 8 + b // 4, g * 4 + g // 16, r * 8 + r // 4, 0])
        self.assertEqual(bytes(src), original)

    def test_cropping_pitch_cached_frames_and_geometry_transitions(self):
        pixels = 720 * 576
        data = (ctypes.c_uint16 * pixels)()
        out = (ctypes.c_uint32 * (pixels + 1))()
        # Reuse buffers over resolution changes, including cropped SMS/NTSC views.
        for width, height, offset in [(320, 224, 0), (160, 144, 0), (248, 192, 16),
                                      (256, 240, 0), (320, 480, 0), (600, 448, 40),
                                      (720, 576, 0), (256, 224, 0)]:
            ctypes.memset(data, 0, ctypes.sizeof(data))
            start = offset // 2
            for y in range(height):
                data[start + y * 720] = 0xf800
                data[start + y * 720 + width - 1] = 0x001f
            original = bytes(data)
            out[width * height] = 0xdeadbeef
            for _ in range(2):
                self.assertTrue(self.convert(out, width * height, data, pixels,
                                             1440, offset, width, height))
                self.assertEqual(bytes(data), original)
                for y in range(height):
                    self.assertEqual(out[y * width], 0xff0000)
                    self.assertEqual(out[y * width + width - 1], 0x0000ff)
                self.assertEqual(out[width * height], 0xdeadbeef)

    def test_invalid_viewports_do_not_write(self):
        src = (ctypes.c_uint16 * 1440)()
        out = (ctypes.c_uint32 * 1440)(*[0xdeadbeef] * 1440)
        before = bytes(out)
        for capacity, pixels, pitch, offset, width, height in [
                (1, 1440, 1440, 0, 2, 1), (1440, 1440, 1441, 0, 1, 1),
                (1440, 1440, 1440, 1, 1, 1), (1440, 1440, 1440, 2880, 1, 1),
                (1440, 1440, 1440, 16, 720, 1), (1440, 1440, 1440, 0, 1, 3),
                (1440, 1440, 0, 0, 1, 1), (1440, 1440, 1440, 0, 0, 1),
                (1440, 1440, 1440, 0, 1, 0), (1440, 0, 1440, 0, 1, 1)]:
            self.assertFalse(self.convert(out, capacity, src, pixels, pitch, offset, width, height))
            self.assertEqual(bytes(out), before)
