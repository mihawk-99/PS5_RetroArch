"""Large menu objects stay off libc; mappings are reclaimed and failures preserve ownership."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parent.parent

class MenuMemory(unittest.TestCase):
    def test_large_lists_failure_realloc_and_concurrent_release(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'menu-memory'
            subprocess.run(['c++', '-std=c++17', '-pthread', '-I.', '-g',
                            '-fsanitize=address,undefined', '-Wl,--wrap=mmap',
                            '-Wl,--wrap=munmap', 'tests/menu_memory_test.cpp',
                            'src/memory_ps5.cpp', '-o', str(binary)], cwd=ROOT, check=True)
            subprocess.run([str(binary)], check=True,
                           env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0',
                                'UBSAN_OPTIONS': 'halt_on_error=1'})
