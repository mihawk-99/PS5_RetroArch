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
            convert(src, 12, src, 12, 2, 2)
            self.assertEqual(src.raw[:8] + src.raw[12:20], expected)
            self.assertEqual(src.raw[8:12] + src.raw[20:24], bytes([9]) * 8)
