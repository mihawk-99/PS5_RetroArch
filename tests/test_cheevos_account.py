"""RetroAchievements: the account survives a title the console closes (issue 110).

The WebUI saves a user name and password in webui.cfg, which the title appends to
RetroArch's config at each start. At sign-in RetroArch swaps the password for a token, in
its settings only, and patches/series 0130 then removed the password from both files.
RetroArch writes the token when it next saves its config, at quit; closed from the home
screen it never quits (2026-11-21: a run closed from outside left retroarch.cfg's time
where the last clean quit put it), so the next start had neither password nor token.
0139 writes the account into retroarch.cfg at sign-in (src/ps5_cheevos.c), and forgets the
password only once it is there.
"""
from pathlib import Path
import ast
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent

MAIN = r'''
#include <stdio.h>
#include <string.h>
int ps5_cheevos_keep_account(const char *path, const char *username, const char *token);
int ps5_cheevos_forget_password(const char *path, int token_too);
/* keep <retroarch.cfg> <user> <token> | signin <retroarch.cfg> <webui.cfg> <user> <token> */
int main(int c, char **v)
{
   if (c == 5 && !strcmp(v[1], "keep"))
      return ps5_cheevos_keep_account(v[2], v[3], v[4]) == 0 ? 0 : 1;
   if (c == 6 && !strcmp(v[1], "signin"))
   {
      /* What 0139's block does once RetroArch holds the token. */
      if (ps5_cheevos_keep_account(v[2], v[4], v[5]) == 0)
         ps5_cheevos_forget_password(v[3], 1);
      return 0;
   }
   return 2;
}
'''


def setting(text, key):
    """The value a config holds for key, as RetroArch reads it: the last line that sets it."""
    value = None
    for line in text.splitlines():
        name, _, rest = line.partition('=')
        if name.strip() == key and rest:
            value = rest.strip().strip('"')
    return value


class CheevosAccount(unittest.TestCase):
    def setUp(self):
        self.temp = Path(self.enterContext(tempfile.TemporaryDirectory(prefix='cheevos-')))
        (self.temp / 'main.c').write_text(MAIN)
        self.tool = self.temp / 'tool'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', self.temp / 'main.c',
                        ROOT / 'src/ps5_cheevos.c', '-o', self.tool], check=True)
        self.retroarch = self.temp / 'retroarch.cfg'
        self.webui = self.temp / 'webui.cfg'

    def run_tool(self, *args):
        return subprocess.run([self.tool, *map(str, args)]).returncode

    def test_the_next_start_still_has_the_account(self):
        # RetroArch's last save, before this account, and what the WebUI's page wrote.
        self.retroarch.write_text('video_smooth = "false"\ncheevos_username = ""\n'
                                  'cheevos_password = ""\ncheevos_token = ""\nmenu_driver = "xmb"')
        self.webui.write_text('cheevos_enable = "true"\ncheevos_username = "player"\n'
                              'cheevos_password = "correct horse"\ncheevos_token = ""\n')
        self.assertEqual(self.run_tool('signin', self.retroarch, self.webui, 'player', 'tok123'), 0)
        # The console closes the title: nothing more is written. The next start reads
        # retroarch.cfg, then webui.cfg over it.
        merged = self.retroarch.read_text() + self.webui.read_text()
        self.assertEqual((setting(merged, 'cheevos_username'), setting(merged, 'cheevos_token')),
                         ('player', 'tok123'))
        self.assertNotIn('correct horse', merged)
        self.assertEqual(setting(merged, 'cheevos_password'), None)
        # The rest of both files is as it was, and the unterminated last line is whole.
        self.assertIn('video_smooth = "false"\n', merged)
        self.assertIn('menu_driver = "xmb"\n', merged)
        self.assertIn('cheevos_enable = "true"\n', merged)

    def test_a_file_that_holds_the_account_is_left_alone(self):
        self.retroarch.write_text('cheevos_username = "player"\nvideo_smooth = "false"\n'
                                  'cheevos_token = "tok123"\n')
        before = (self.retroarch.read_text(), self.retroarch.stat().st_ino)
        self.assertEqual(self.run_tool('keep', self.retroarch, 'player', 'tok123'), 0)
        self.assertEqual((self.retroarch.read_text(), self.retroarch.stat().st_ino), before)
        self.assertFalse(Path(f'{self.retroarch}.tmp').exists())
        # A new token after the old one expired, and a line set twice: one of each stays.
        self.retroarch.write_text(before[0] + 'cheevos_token = "tok123"\n')
        self.assertEqual(self.run_tool('keep', self.retroarch, 'player', 'tok456'), 0)
        text = self.retroarch.read_text()
        self.assertEqual((text.count('cheevos_token'), setting(text, 'cheevos_token')), (1, 'tok456'))
        self.assertEqual(text.count('cheevos_username = "player"\n'), 1)

    def test_the_password_stays_when_the_account_cannot_be_written(self):
        saved = 'cheevos_username = "player"\ncheevos_password = "correct horse"\ncheevos_token = ""\n'
        self.webui.write_text(saved)
        # No retroarch.cfg to write into, then a value that cannot stand between quotes.
        self.assertEqual(self.run_tool('keep', self.retroarch, 'player', 'tok123'), 1)
        self.assertEqual(self.run_tool('signin', self.retroarch, self.webui, 'player', 'tok123'), 0)
        self.assertEqual(self.webui.read_text(), saved)
        self.retroarch.write_text('video_smooth = "false"\n')
        for user, token in (('pla"yer', 'tok123'), ('player', ''), ('player', 'tok\n123')):
            self.assertEqual(self.run_tool('signin', self.retroarch, self.webui, user, token), 0)
            self.assertEqual((self.webui.read_text(), self.retroarch.read_text()),
                             (saved, 'video_smooth = "false"\n'))

    def test_retroarch_s_sign_in_keeps_the_account_before_it_forgets_the_password(self):
        script = ROOT / 'tools/apply-port-patches.py'
        edits = next(ast.literal_eval(node.value) for node in ast.parse(script.read_text()).body
                     if isinstance(node, ast.Assign) and getattr(node.targets[0], 'id', '') == 'EDITS')
        # The pinned cheevos.c, fresh, and one that already carries 0130's edit as v4.3 built it.
        fresh = (ROOT / 'vendor/retroarch/cheevos/cheevos.c').read_text()
        earlier = fresh
        for name, anchor, replacement, marker in edits:
            if name == 'cheevos/cheevos.c' and '0139' not in marker:
                earlier = earlier.replace(anchor, replacement, 1)
        self.assertIn('ps5_cheevos_forget_password("/app0/config/retroarch.cfg", 0);', earlier)
        for start in (fresh, earlier):
            # A tree of this one file: the script reports the others missing and goes on.
            tree = Path(self.enterContext(tempfile.TemporaryDirectory(prefix='cheevos-tree-')))
            (tree / 'cheevos').mkdir()
            (tree / 'cheevos/cheevos.c').write_text(start)
            ran = subprocess.run(['python3', script, tree], capture_output=True, text=True)
            self.assertNotIn('cheevos/cheevos.c: ANCHOR NOT FOUND', ran.stdout)
            text = (tree / 'cheevos/cheevos.c').read_text()
            keep = text.index('if (ps5_cheevos_keep_account("/app0/config/retroarch.cfg",')
            forget = text.index('ps5_cheevos_forget_password("/app0/config/webui.cfg", 1);')
            self.assertLess(text.index('strlcpy(settings->arrays.cheevos_token, user->token,'), keep)
            self.assertLess(keep, forget)
            self.assertEqual(text.count('ps5_cheevos_forget_password("'), 1)


@unittest.skipUnless((ROOT / 'vendor/retroarch/cheevos/cheevos.c').exists(), 'the pinned RetroArch source is needed')
class Pinned(unittest.TestCase):
    def test_upstream_keeps_the_token_in_its_settings_only(self):
        # What 0139 rests on: sign-in stores the token in memory and saves nothing.
        text = (ROOT / 'vendor/retroarch/cheevos/cheevos.c').read_text()
        at = text.index('strlcpy(settings->arrays.cheevos_token, user->token,')
        self.assertNotIn('config_save', text[at:at + 1500])


if __name__ == '__main__':
    unittest.main()
