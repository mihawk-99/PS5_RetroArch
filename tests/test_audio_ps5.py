"""Run the real audio driver against an explicitly clocked native output mock."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class AudioPs5(unittest.TestCase):
    def test_native_backend(self):
        with tempfile.TemporaryDirectory() as td:
            binary = str(Path(td) / 'audio-test')
            subprocess.run(['c++', '-std=c++17', '-O2', '-pthread', '-Wall', '-Wextra', '-Werror',
                            '-Ivendor/retroarch', '-Ivendor/retroarch/libretro-common/include',
                            'tests/audio_ps5_test.cpp', '-o', binary], cwd=ROOT, check=True)
            subprocess.run([binary], cwd=ROOT, check=True, timeout=15)
