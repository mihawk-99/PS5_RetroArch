"""Generic startup strings must not accept an older executable."""

import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("deploy_title", ROOT / "tools/deploy-title.py")
deploy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deploy)


class DeployIdentity(unittest.TestCase):
    def test_generic_startup_strings_are_insufficient(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "eboot.bin"
            image.write_bytes(b"main() entered\0rarch_main returned\0ps5_init entered")
            with self.assertRaisesRegex(ValueError, "build identity"):
                deploy.program_markers(image)

    def test_different_build_requires_different_marker(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "eboot.bin"
            marker = b"build identity: " + b"a" * 64
            image.write_bytes(b"main() entered\0" + marker + b"\0")
            required = deploy.program_markers(image)
            self.assertIn(marker, required)
            old_image = b"main() entered\0build identity: " + b"b" * 64 + b"\0"
            self.assertTrue(any(value not in old_image for value in required))
