"""Diagnostic counts, failure-path durability and allocator semantics."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class MemoryDiagnostics(unittest.TestCase):
    def test_normal_build_xmb_hooks_are_inert_c(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'inert.c'
            source.write_text('''#include "src/memory_xmb.h"
int main(void) {
    unsigned calls = 0;
    ps5_memory_xmb_context(++calls, ++calls, ++calls, ++calls, ++calls);
    ps5_memory_xmb_stage(++calls, ++calls, ++calls);
    ps5_memory_xmb_node(++calls);
    return calls != 0;
}
''')
            binary = Path(directory) / 'inert'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I.',
                            str(source), '-o', str(binary)], cwd=ROOT, check=True)
            subprocess.run([str(binary)], check=True)

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
            self.assertEqual(fields['failures'], '10006')
            self.assertEqual(fields['failure_records'], '5')
            self.assertEqual(fields['failure_suppressed'], '10001')
            rows = text.splitlines()
            context = [line for line in rows if line.startswith('first-failure-xmb ')]
            self.assertEqual(len(context), 1)
            for field in ('tab=3', 'kind=2', 'phase=7', 'current=500', 'old=8',
                          'horizontal=10', 'list_size=500', 'index=499', 'nodes=1',
                          'created=1', 'copied=1', 'freed=1', 'unmatched_frees=0'):
                self.assertIn(' ' + field + ' ', context[0] + ' ')
            history = [line for line in rows if line.startswith('first-failure-xmb-history ')]
            self.assertEqual(len(history), 8)
            for i, line in enumerate(history):
                self.assertIn(f' event={i + 13} ', line)
                self.assertIn(f' list_size={i + 12} ', line)
            owners = [dict(field.split('=') for field in line.split()[1:])
                      for line in rows if line.startswith('first-failure-caller ')]
            self.assertEqual(len(owners), 48)
            for route in range(3):
                group = [o for o in owners if o['route'] == str(route)]
                self.assertEqual(len(group), 16)
                self.assertEqual([int(o['bytes']) for o in group], list(range(1019, 1003, -1)))
                self.assertTrue(all(o['count'] == '1' for o in group))
            self.assertLess(capture.stat().st_size, 20000)
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
