"""Multiple cores share bindings without losing object imports or adapters."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('core_imports', ROOT / 'tools/core-imports.py')
imports = importlib.util.module_from_spec(spec)
spec.loader.exec_module(imports)


class CoreImports(unittest.TestCase):
    def test_union_deduplicates_and_preserves_runtime_adapters(self):
        first = '''1: 00000000 0 FUNC GLOBAL DEFAULT UND malloc
2: 00000000 0 OBJECT GLOBAL DEFAULT UND __isthreaded
3: 00000000 0 FUNC GLOBAL DEFAULT UND opendir'''
        second = '''1: 00000000 0 FUNC GLOBAL DEFAULT UND malloc
2: 00000000 0 NOTYPE GLOBAL DEFAULT UND localtime_r
3: 00000000 0 FUNC GLOBAL DEFAULT UND rewinddir'''
        with patch.object(imports.subprocess, 'check_output', side_effect=[first, second]):
            collected = imports.collect_imports(['one.so', 'two.so'])
        self.assertEqual(len(collected), 5)
        generated = imports.generate(collected)
        # A core's malloc is the title's overflow-first allocator (src/memory_ps5.cpp).
        self.assertEqual(generated.count('asm("ps5_core_malloc")'), 1)
        self.assertIn('[] asm("__isthreaded")', generated)
        self.assertIn('asm("rtime_localtime")', generated)
        self.assertIn('asm("ps5_rewinddir")', generated)
        self.assertIn('asm("ps5_opendir")', generated)

    def test_reject_conflicting_types_and_tls(self):
        with patch.object(imports.subprocess, 'check_output', side_effect=[
                '1: 0 0 FUNC GLOBAL DEFAULT UND symbol',
                '1: 0 0 OBJECT GLOBAL DEFAULT UND symbol']):
            with self.assertRaisesRegex(ValueError, 'Conflicting'):
                imports.collect_imports(['one.so', 'two.so'])
        with patch.object(imports.subprocess, 'check_output', return_value=
                          '1: 0 0 TLS GLOBAL DEFAULT UND variable'):
            with self.assertRaisesRegex(ValueError, 'Unsupported'):
                imports.collect_imports(['tls.so'])
