"""Verify the padded source quad's UVs across width and transition changes."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class CoreFrame(unittest.TestCase):
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
