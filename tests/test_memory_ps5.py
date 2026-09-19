"""Exercise mapped allocations together with genuinely libc-owned buffers."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class NativeMemory(unittest.TestCase):
    def test_large_buffers_and_mixed_ownership(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'memory-test'
            subprocess.run(['c++', '-std=c++17', '-pthread', '-I.',
                            'tests/memory_ps5_test.cpp', 'src/memory_ps5.cpp',
                            '-o', str(binary)], cwd=ROOT, check=True)
            subprocess.run([str(binary)], check=True)
