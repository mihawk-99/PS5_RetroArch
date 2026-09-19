"""Diagnostic counts, failure-path durability and allocator semantics."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class MemoryDiagnostics(unittest.TestCase):
    def test_counts_failure_logging_and_ownership(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'diagnostics-test'
            capture = Path(directory) / 'memory.log'
            subprocess.run(['c++', '-std=c++20', '-pthread', '-I.',
                            '-DPS5_MEMORY_DIAGNOSTICS', '-Wl,--wrap=clock_gettime',
                            'tests/memory_diagnostics_test.cpp',
                            'src/memory_ps5.cpp', 'src/memory_diagnostics.cpp',
                            '-o', str(binary)], cwd=ROOT, check=True)
            subprocess.run([str(binary), str(capture)], check=True)
            text = capture.read_text()
            self.assertIn('session host-test', text)
            self.assertEqual(sum(line.startswith('sample ') for line in text.splitlines()), 1)
            self.assertIn('caller seq=', text)
            self.assertIn('ms=5000', text)
            self.assertEqual(text.count('failure op='), 5)
            self.assertIn('failure op=forced', text)
            final = next(line for line in text.splitlines() if line.startswith('final '))
            fields = dict(item.split('=') for item in final.split()[1:])
            for field in ('native_bytes', 'mapped_bytes', 'aligned_bytes',
                          'native_count', 'mapped_count', 'aligned_count'):
                self.assertEqual(fields[field], '0')
            self.assertEqual(fields['dropped'], '1')
            self.assertEqual(fields['image_create'], fields['image_destroy'])
            self.assertEqual(fields['idle_failed'], '1')

    def test_vulkan_image_and_idle_dispatch(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'dispatch-test'
            capture = Path(directory) / 'memory.log'
            subprocess.run(['c++', '-std=c++20', '-pthread', '-I.', '-Ivendor/retroarch',
                            '-DPS5_MEMORY_DIAGNOSTICS', 'tests/memory_vulkan_test.cpp',
                            'src/memory_diagnostics.cpp', '-o', str(binary)], cwd=ROOT, check=True)
            subprocess.run([str(binary), str(capture)], check=True)
            final = next(line for line in capture.read_text().splitlines() if line.startswith('final '))
            fields = dict(item.split('=') for item in final.split()[1:])
            for field in ('image_create', 'image_destroy', 'image_failed',
                          'idle_begin', 'idle_end', 'idle_failed'):
                self.assertEqual(fields[field], '1')
