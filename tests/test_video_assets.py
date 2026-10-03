import importlib.util
import json
from pathlib import Path
import tempfile
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('video_assets', ROOT / 'tools/video-assets.py')
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)


class VideoAssetsTests(unittest.TestCase):
    def test_config_matches_upstream_empty_unterminated_and_first_value(self):
        self.assertEqual(assets.config('a =\nb = "foo\nb = "bar"\nc = baz # note'),
                         {'a': '', 'b': 'foo', 'c': 'baz'})

    def test_dependency_missing_case_and_escape(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'pass.slang').write_text('#include "common.inc"\n')
            (root / 'common.inc').write_text('')
            (root / 'test.slangp').write_text('shaders = 1\nshader0 = "pass.slang"\n')
            self.assertEqual(assets.dependency_errors(root), [])
            (root / 'common.inc').rename(root / 'Common.inc')
            self.assertIn('pass.slang -> common.inc', assets.dependency_errors(root))
            (root / 'test.slangp').write_text('#reference "../outside.slangp"\n')
            self.assertTrue(any('outside' in e for e in assets.dependency_errors(root)))

    def test_fixture_rejected_even_renamed_and_reference_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            payload = root / 'innocent.png'
            payload.write_bytes(b'fixture test payload')
            signatures = root / 'hashes.json'
            signatures.write_text(json.dumps([{'sha256': assets.sha(payload)}]))
            original = assets.FIXTURES
            try:
                assets.FIXTURES = signatures
                (root / 'preset.cfg').write_text('input_overlay = "/app0/TEST_OVERLAYS/a.cfg"')
                errors = assets.forbidden(root)
                self.assertIn('development fixture: innocent.png', errors)
                self.assertIn('development reference: preset.cfg', errors)
            finally:
                assets.FIXTURES = original

    def test_absent_inventory_rejects_package(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(ValueError, 'video-assets.json'):
                assets.package(Path(tmp), Path(tmp) / 'bad.zip')

    def test_real_cpu_filter_both_pixel_formats_and_output_size(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'filter-test'
            subprocess.run(['cc', '-std=c99',
                '-I'+str(ROOT / 'vendor/retroarch/gfx/video_filters'),
                '-I'+str(ROOT / 'vendor/retroarch/libretro-common/include'),
                str(ROOT / 'tests/video_filter_formats.c'),
                str(ROOT / 'vendor/retroarch/gfx/video_filters/normal2x.c'),
                '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_parameter_references_and_bare_paths(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'test.slangp').write_text('#reference "settings.params"\n')
            (root / 'settings.params').write_text('#reference missing.params\n')
            self.assertIn('settings.params -> missing.params', assets.dependency_errors(root))

    def test_missing_member_invalidates_declared_collection(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'collection').mkdir()
            (root / 'collection/member').write_text('asset')
            table = root / 'table.json'
            item = {'name': 'sample', 'destination': 'collection', 'revision': 'pin', 'sha256': 'digest'}
            table.write_text(json.dumps({'collections': [item]}))
            (root / 'video-assets.json').write_text(json.dumps([{**item, 'files': 2, 'bytes': 10}]))
            original = assets.TABLE
            try:
                assets.TABLE = table
                with self.assertRaisesRegex(ValueError, 'collection inventory mismatch'):
                    assets.check(root)
            finally:
                assets.TABLE = original
