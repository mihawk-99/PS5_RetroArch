"""The controllers' speakers (patch 0139): the real src/pad_speaker_ps5.cpp against a mocked
AudioOut, and the environment call and close-all the port inserts into runloop.c."""
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_core_environment import ROOT, runloop_edits


class PadSpeakerPs5(unittest.TestCase):
    def test_speaker_against_mocked_audio_out(self):
        with tempfile.TemporaryDirectory() as td:
            binary = str(Path(td) / 'pad-speaker-test')
            subprocess.run(['c++', '-std=c++17', '-O2', '-pthread', '-Wall', '-Wextra', '-Werror',
                            'tests/pad_speaker_ps5_test.cpp', '-o', binary], cwd=ROOT, check=True)
            subprocess.run([binary], cwd=ROOT, check=True, timeout=30)

    def test_environment_call_and_close_in_runloop(self):
        """0139 after 0138, as main() applies them: each anchor once, the case in the environment
        switch, and every speaker closed when the core goes."""
        edits = runloop_edits(('patches/series, 0138', 'patches/series, 0139'))
        self.assertEqual(len(edits), 3)
        source = ROOT / 'vendor/retroarch/runloop.c'
        if not source.exists():
            self.skipTest('vendor/retroarch is not fetched (tools/fetch-retroarch.sh)')
        text = source.read_text()
        if 'patches/series, 0139' not in text:
            for _, anchor, replacement, marker in edits:
                if marker in text:
                    continue
                self.assertEqual(text.count(anchor), 1, marker)
                text = text.replace(anchor, replacement, 1)
        self.assertEqual(text.count('case 0x20000 | 3:'), 1)
        self.assertIn('ps5_pad_speaker_interface()', text)
        deinit = text[text.index('void runloop_event_deinit_core(void)'):]
        self.assertLess(deinit.index('ps5_pad_speaker_close_all();'), deinit.index('\n}\n'))


if __name__ == '__main__':
    unittest.main()
