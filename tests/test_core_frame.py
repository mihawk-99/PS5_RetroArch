"""Verify core upload channel order, alpha, row pitch and in-place writes."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class CoreFrame(unittest.TestCase):
    def test_xrgb_upload_with_padding_and_in_place(self):
        with tempfile.TemporaryDirectory() as td:
            lib = Path(td) / 'frame.so'
            subprocess.run(['c++', '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'src/core_frame_ps5.cpp'), '-o', str(lib)], check=True)
            convert = ctypes.CDLL(str(lib)).ps5_core_frame_rgba
            convert.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                                ctypes.c_size_t, ctypes.c_uint, ctypes.c_uint]
            source = bytes([0, 0, 255, 0, 0, 255, 0, 128, 9, 9, 9, 9,
                            255, 0, 0, 0, 0x56, 0x34, 0x12, 1, 9, 9, 9, 9])
            expected = bytes([255, 0, 0, 255, 0, 255, 0, 255,
                              0, 0, 255, 255, 0x12, 0x34, 0x56, 255])
            src = ctypes.create_string_buffer(source)
            dst = ctypes.create_string_buffer(bytes([0xAA]) * 32)
            convert(dst, 16, src, 12, 2, 2)
            self.assertEqual(dst.raw[:8] + dst.raw[16:24], expected)
            self.assertEqual(dst.raw[8:16] + dst.raw[24:32], bytes([0xAA]) * 16)
            # Quick Menu repeats the cached core frame into alternating images.
            # The source remains XRGB, so repeated uploads must be identical.
            self.assertEqual(src.raw[:len(source)], source)
            for _ in range(4):
                convert(dst, 16, src, 12, 2, 2)
                self.assertEqual(dst.raw[:8] + dst.raw[16:24], expected)
                self.assertEqual(src.raw[:len(source)], source)
            convert(src, 12, src, 12, 2, 2)
            self.assertEqual(src.raw[:8] + src.raw[12:20], expected)
            self.assertEqual(src.raw[8:12] + src.raw[20:24], bytes([9]) * 8)

    def test_padded_source_sampling_across_core_transitions(self):
        with tempfile.TemporaryDirectory() as td:
            wrapper = Path(td) / 'quad.cpp'
            wrapper.write_text('#include "' + str(ROOT / 'src/core_frame_ps5.cpp') + '"\n'
                               'extern "C" void quad(float *v, unsigned w, unsigned p) '
                               '{ ps5_core_source_quad(v, w, p); }\n')
            lib = Path(td) / 'quad.so'
            subprocess.run(['c++', '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror',
                            str(wrapper), '-o', str(lib)], check=True)
            quad = ctypes.CDLL(str(lib)).quad
            quad.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_uint, ctypes.c_uint]
            # FCEUmm -> no content -> GBA -> GB -> no content -> FCEUmm.
            for width, physical in [(256, 256), (4, 128), (240, 256),
                                    (160, 192), (4, 128), (256, 256)]:
                vertices = (ctypes.c_float * 48)()
                quad(vertices, width, physical)
                for base in (0, 24):
                    uv = [(vertices[i + 2], vertices[i + 3])
                          for i in range(base, base + 24, 4)]
                    right = ctypes.c_float(width / physical).value
                    self.assertEqual(uv, [(0, 0), (0, 1), (right, 0),
                                          (right, 0), (0, 1), (right, 1)])
                    # At every output pixel centre, nearest sampling stays in
                    # initialized content, never the poisoned row padding.
                    samples = [int(((x + 0.5) / 1920) * right * physical)
                               for x in range(1920)]
                    self.assertEqual(min(samples), 0)
                    self.assertEqual(max(samples), width - 1)
