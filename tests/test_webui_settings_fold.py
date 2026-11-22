"""A setting saved from the WebUI is RetroArch's own from the next start on (issue 130).

webui.cfg is appended to RetroArch's configuration at every start, so a value saved there
once came back at each start whatever the menu or retroarch.cfg said afterwards: a volume
gain that returned to +12 dB. src/ps5_webui_settings.c moves such a setting into
retroarch.cfg at a start; what other code reads from webui.cfg stays in it.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
MAIN = r"""
#include <stdio.h>
int ps5_webui_settings_fold(const char *webui, const char *config);
int main(int c, char **v) { if (c != 3) return 2; printf("%d\n", ps5_webui_settings_fold(v[1], v[2])); return 0; }
"""


class Fold(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        work = Path(cls.temp.name)
        (work / 'main.c').write_text(MAIN)
        cls.binary = work / 'fold'
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                        str(work / 'main.c'), str(ROOT / 'src/ps5_webui_settings.c'), '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def fold(self, webui, config):
        work = Path(tempfile.mkdtemp(dir=self.temp.name))
        w, c = work / 'webui.cfg', work / 'retroarch.cfg'
        if webui is not None: w.write_bytes(webui)
        if config is not None: c.write_bytes(config)
        out = subprocess.run([str(self.binary), str(w), str(c)], capture_output=True, text=True, check=True).stdout.strip()
        return int(out), (w.read_bytes() if w.exists() else None), (c.read_bytes() if c.exists() else None), work

    def test_a_setting_moves_once_and_the_menu_s_later_value_stays(self):
        config = b'audio_mixer_volume = "0.000000"\naudio_volume = "0.000000"\nvideo_vsync = "true"\n'
        count, webui, merged, work = self.fold(b'audio_volume = "12"\nvideo_smooth = "true"\n', config)
        self.assertEqual(count, 2)
        self.assertEqual(webui, b'')
        self.assertEqual(merged, b'audio_mixer_volume = "0.000000"\nvideo_vsync = "true"\naudio_volume = "12"\nvideo_smooth = "true"\n')
        self.assertEqual(sorted(p.name for p in work.iterdir()), ['retroarch.cfg', 'webui.cfg'])
        # The user sets it in RetroArch's menu, which saves retroarch.cfg; the next start leaves it.
        (work / 'retroarch.cfg').write_bytes(merged.replace(b'audio_volume = "12"', b'audio_volume = "0.000000"'))
        out = subprocess.run([str(self.binary), str(work / 'webui.cfg'), str(work / 'retroarch.cfg')], capture_output=True, text=True, check=True).stdout.strip()
        self.assertEqual(out, '0')
        self.assertIn(b'audio_volume = "0.000000"', (work / 'retroarch.cfg').read_bytes())

    def test_what_other_code_reads_there_stays(self):
        saved = (b'cheevos_enable = "true"\ncheevos_username = "someone"\ncheevos_password = "secret"\ncheevos_token = ""\n'
                 b'esde_separate_systems = "true"\nmenu_driver = "ozone"\n')
        count, webui, merged, _ = self.fold(saved, b'menu_driver = "xmb"\ncheevos_username = ""\n')
        self.assertEqual(count, 1)
        self.assertEqual(webui, saved.replace(b'menu_driver = "ozone"\n', b''))
        self.assertEqual(merged, b'cheevos_username = ""\nmenu_driver = "ozone"\n')
        self.assertNotIn(b'secret', merged)

    def test_nothing_to_move_touches_nothing(self):
        for webui, config in [(b'cheevos_enable = "true"\n', b'a = "1"'), (b'', b'a = "1"\n'), (None, b'a = "1"\n'),
                              (b'audio_volume = "12"\n', None), (b'# a note\n\n', b'a = "1"\n')]:
            count, after, merged, _ = self.fold(webui, config)
            self.assertEqual((count, after, merged), (0, webui, config))

    def test_odd_lines(self):
        # No line end at the end of either file, spaces, a comment and a line that is not a setting.
        count, webui, merged, _ = self.fold(b'# from the page\n  video_smooth="true"\nnot a setting\naudio_volume = "3"',
                                            b'video_smooth = "false"\naudio_volume_extra = "1"\nlast = "x"')
        self.assertEqual(count, 2)
        self.assertEqual(webui, b'# from the page\nnot a setting\n')
        self.assertEqual(merged, b'audio_volume_extra = "1"\nlast = "x"\n  video_smooth="true"\naudio_volume = "3"\n')


if __name__ == '__main__':
    unittest.main()
