"""Compile the real microphone backend; simulate AudioIn, not its queue algorithm."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class MicrophoneBackend(unittest.TestCase):
    def compile(self, args):
        result = subprocess.run(args, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_capture_lifecycle_and_mute(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        tree = ROOT / 'build/ra-conf'
        if not compiler or not (tree / 'audio/microphone_driver.h').exists():
            self.skipTest('requires a configured frontend tree and a host C++ compiler')
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / 'microphone-test'
            self.compile([compiler, '-std=c++17', '-pthread', '-Wall', '-Wextra', '-Werror',
                '-I' + str(tree), '-I' + str(tree / 'libretro-common/include'),
                str(ROOT / 'src/microphone_ps5.cpp'), str(ROOT / 'tests/microphone_ps5_test.cpp'),
                '-o', str(binary)])
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True, timeout=15)
            self.assertIn('input failure PASS', result.stdout)

if __name__ == '__main__':
    unittest.main()
