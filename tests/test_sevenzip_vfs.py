"""7zip file adapter regression, using the pinned SDK and a synthetic VFS URL.

Run tools/fetch-retroarch.sh first. No server, console, ROMs or credentials are
required. The C backend is an injectable test double, not live SMB integration.
"""
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
RA = ROOT / 'vendor/retroarch'


@unittest.skipUnless((RA / 'deps/7zip/7zFile.c').is_file(),
                     'run tools/fetch-retroarch.sh to fetch the pinned SDK')
class SevenzipVfs(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='7zip-vfs-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        cls.sdk = cls.directory / '7zip'
        shutil.copytree(RA / 'deps/7zip', cls.sdk)
        cls.edits = [edit for edit in runpy.run_path(
            str(ROOT / 'tools/apply-port-patches.py'))['EDITS']
            if edit[0].startswith('deps/7zip/')]
        for name, anchor, replacement, marker in cls.edits:
            path = cls.sdk / Path(name).name
            text = path.read_text()
            if marker not in text:
                if text.count(anchor) != 1:
                    raise AssertionError(f'non-unique 7zip anchor: {marker}')
                path.write_text(text.replace(anchor, replacement, 1))

    def test_patch_reapplication_is_idempotent(self):
        self.assertEqual(len(self.edits), 8)
        for name, _anchor, replacement, marker in self.edits:
            text = (self.sdk / Path(name).name).read_text()
            self.assertIn(replacement, text)
            self.assertEqual(text.count(marker), 1)

    def run_adapter(self, ps5):
        binary = self.directory / ('ps5' if ps5 else 'stdio')
        subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie',
                        *(['-D__PROSPERO__'] if ps5 else []),
                        f'-I{self.sdk}', f'-I{RA}/libretro-common/include',
                        str(ROOT / 'tests/sevenzip_vfs_test.c'), str(self.sdk / '7zFile.c'),
                        '-o', str(binary)], check=True)
        fixture = self.directory / 'fixture'
        output = self.directory / 'output'
        fixture.write_bytes(b'abcdefgh')
        output.write_bytes(b'old longer output')
        run = subprocess.run([str(binary), str(fixture), str(output)],
                             capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn('PASS: file I/O contract', run.stdout)
        self.assertNotIn('runtime error:', run.stderr)
        self.assertEqual(fixture.read_bytes(), b'abcdefgh')
        self.assertEqual(output.read_bytes(), b'ok')

    def test_ps5_vfs_url_and_error_contracts(self):
        self.run_adapter(ps5=True)

    def test_non_ps5_stdio_unchanged(self):
        self.run_adapter(ps5=False)


if __name__ == '__main__':
    unittest.main()
