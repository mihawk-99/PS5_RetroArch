"""Check every RGB565 colour through the exact core and frontend upload helpers."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class FBNeoVideo(unittest.TestCase):
    def test_colours_pitch_bounds_and_cached_source(self):
        with tempfile.TemporaryDirectory() as td:
            source = Path(td) / 'video.cpp'
            source.write_text('#include "' + str(ROOT / 'tooling/fbneo/ps5-video.h') + '"\n'
                              '#include "' + str(ROOT / 'src/core_frame_ps5.cpp') + '"\n'
                              'extern "C" bool convert(uint32_t *d, size_t n, '
                              'const uint16_t *s, size_t p, unsigned w, unsigned h) '
                              '{ return ps5_fbneo_frame(d, n, s, p, w, h); }\n')
            lib = Path(td) / 'video.so'
            subprocess.run(['c++', '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror',
                            str(source), '-o', str(lib)], check=True)
            api = ctypes.CDLL(str(lib))
            convert = api.convert
            convert.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                                ctypes.c_size_t, ctypes.c_uint, ctypes.c_uint]
            convert.restype = ctypes.c_bool
            upload = api.ps5_core_frame_rgba
            upload.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                               ctypes.c_size_t, ctypes.c_uint, ctypes.c_uint]
            # All 65536 inputs, not only primaries, catch bit-order/green-width errors.
            src = (ctypes.c_uint16 * 65536)(*range(65536))
            core = (ctypes.c_uint32 * 65536)()
            gpu = (ctypes.c_uint8 * (65536 * 4))()
            self.assertTrue(convert(core, 65536, src, 512, 256, 256))
            upload(gpu, 1024, core, 1024, 256, 256)
            for pixel in range(65536):
                r, g, b = pixel >> 11, (pixel >> 5) & 63, pixel & 31
                self.assertEqual(list(gpu[pixel * 4:pixel * 4 + 4]),
                                 [r * 8 + r // 4, g * 4 + g // 16, b * 8 + b // 4, 255])
            self.assertEqual(list(src), list(range(65536)))
            # Horizontal, vertical and non-aligned arcade widths; non-tight input row pitch.
            for width, height in [(320, 224), (224, 384), (384, 224), (402, 256)]:
                pitch = (width + 8) * 2
                data = (ctypes.c_uint16 * ((width + 8) * height))()
                out = (ctypes.c_uint32 * (width * height + 1))()
                out[-1] = 0xdeadbeef
                for y in range(height):
                    data[y * (width + 8)] = 0xf800
                    data[y * (width + 8) + width - 1] = 0x001f
                    data[y * (width + 8) + width] = 0xffff
                original = bytes(data)
                for _ in range(2):
                    self.assertTrue(convert(out, width * height, data, pitch, width, height))
                    self.assertEqual(bytes(data), original)
                    for y in range(height):
                        self.assertEqual(out[y * width], 0xff0000)
                        self.assertEqual(out[y * width + width - 1], 0x0000ff)
                    self.assertEqual(out[-1], 0xdeadbeef)
                before = bytes(out)
                for capacity, stride, w, h in [(width * height - 1, pitch, width, height),
                                               (width * height, width * 2 - 2, width, height),
                                               (width * height, pitch + 1, width, height),
                                               (width * height, pitch, 0, height)]:
                    self.assertFalse(convert(out, capacity, data, stride, w, h))
                    self.assertEqual(bytes(out), before)
