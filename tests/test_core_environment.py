"""Cores ask whether hardcore is on and for the savestate folder (patch 0138): the environment
calls as the port inserts them into runloop.c."""
import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def runloop_edits(prefix):
    spec = importlib.util.spec_from_file_location('patches', ROOT / 'tools/apply-port-patches.py')
    patches = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(patches)
    return [e for e in patches.EDITS if e[0] == 'runloop.c' and e[3].startswith(prefix)]


class CoreEnvironment(unittest.TestCase):
    def test_hardcore_and_savestate_folder(self):
        """Each anchor is found once, and both cases end up in RetroArch's environment switch,
        before GET_SAVESTATE_CONTEXT."""
        edits = runloop_edits('patches/series, 0138')
        self.assertEqual(len(edits), 1)
        source = ROOT / 'vendor/retroarch/runloop.c'
        if not source.exists():
            self.skipTest('vendor/retroarch is not fetched (tools/fetch-retroarch.sh)')
        text = source.read_text()
        if 'patches/series, 0138' not in text:
            for _, anchor, replacement, marker in edits:
                self.assertEqual(text.count(anchor), 1, marker)
                text = text.replace(anchor, replacement, 1)
        for case in ('case 0x20000 | 1:', 'case 0x20000 | 2:'):
            self.assertEqual(text.count(case), 1, case)
            self.assertLess(text.index(case), text.index('case RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT:'))
        self.assertIn('rcheevos_hardcore_active()', text)
        self.assertIn('RARCH_DIR_CURRENT_SAVESTATE', text)


if __name__ == '__main__':
    unittest.main()
