"""The SMB page's routes (src/webui_ps5.cpp, src/ps5_smb.c) against a Samba server of the
test's own (tests/smb_server.py): the shares are listed for the guest and for a user with a
password, a wrong password and a missing folder are refused with a sentence, the server is
saved to config/smb.cfg and answered back without its password, and forgotten; then the
page does the same in Chromium (tests/webui_smb_browser.cjs)."""
import http.client
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import smb_server  # noqa: E402
import webui_build  # noqa: E402
from test_webui_transfer_browser import PLAYWRIGHT  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


@unittest.skipUnless(smb_server.AVAILABLE, 'smbd and pdbedit (Samba) are not installed')
class SmbRoutes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.samba = smb_server.SambaServer()
        (cls.samba.share / 'Games' / 'Homebrew').mkdir(parents=True)
        (cls.samba.share / 'Games' / 'Homebrew' / 'demo.bin').write_bytes(b'demo')
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name) / 'PPSA99169'
        shutil.copytree(ROOT / 'webui', cls.root / 'webui')
        (cls.root / 'config').mkdir()
        cls.binary = Path(cls.temp.name) / 'server'
        webui_build.build(cls.binary, 'tests/webui_server_main.cpp')
        cls.process, cls.port, cls.token = webui_build.start(cls.binary, cls.root, dict(os.environ))

    @classmethod
    def tearDownClass(cls):
        cls.process.terminate()
        cls.process.wait(timeout=5)
        cls.temp.cleanup()
        cls.samba.stop()

    def request(self, method, path, body=None, token=True):
        conn = http.client.HTTPConnection('127.0.0.1', self.port, timeout=30)
        conn.request(method, path, json.dumps(body) if isinstance(body, dict) else body,
                     {'X-RetroArch-Token': self.token} if token else {})
        response = conn.getresponse()
        result = response.status, json.loads(response.read() or b'null')
        conn.close()
        return result

    def test_a_server_from_test_to_forget(self):
        server = self.samba.address
        guest = {'server': server, 'user': '', 'password': '', 'workgroup': ''}
        user = {'server': server, 'user': smb_server.USER, 'password': smb_server.PASSWORD, 'workgroup': ''}

        # nothing saved yet
        self.assertEqual(self.request('GET', '/api/smb'),
                         (200, {'server': '', 'share': '', 'folder': '', 'user': '', 'workgroup': '',
                                'password': False}))
        # a write needs the session's token
        self.assertEqual(self.request('POST', '/api/smb/shares', guest, token=False)[0], 403)

        # Test connection: the shares, for the guest and for the user
        self.assertEqual(self.request('POST', '/api/smb/shares', guest), (200, {'shares': ['games', 'locked']}))
        self.assertEqual(self.request('POST', '/api/smb/shares', user), (200, {'shares': ['games', 'locked']}))
        status, body = self.request('POST', '/api/smb/shares', {**user, 'password': 'wrong'})
        self.assertEqual(status, 400)
        self.assertIn('refused this user name and password', body['error'])
        status, body = self.request('POST', '/api/smb/shares', {**guest, 'server': '127.0.0.1:1'})
        self.assertEqual(status, 400)
        self.assertIn('did not answer', body['error'])
        for bad, words in [({**user, 'password': 'a"b'}, 'quote'), ({**guest, 'server': 'x\ny'}, 'line break'),
                           ({**guest, 'server': ''}, "server's name")]:
            status, body = self.request('POST', '/api/smb/shares', bad)
            self.assertEqual(status, 400, bad)
            self.assertIn(words, body['error'])
        self.assertEqual(self.request('POST', '/api/smb/shares', b'not json')[0], 400)

        # saving checks the share and the folder
        for share, folder, words in [('locked', '', 'refused'), ('games', 'Missing', 'no folder'),
                                     ('games', 'Games/../..', 'folder inside'), ('', '', 'Choose a share')]:
            status, body = self.request('POST', '/api/smb', {**guest, 'share': share, 'folder': folder})
            self.assertEqual(status, 400, (share, folder))
            self.assertIn(words, body['error'])
        self.assertFalse((self.root / 'config/smb.cfg').exists())

        # saved: the folder tidied, the password kept on the console and never answered back
        status, body = self.request('POST', '/api/smb', {**user, 'share': 'locked', 'folder': '\\Games\\Homebrew\\'})
        self.assertEqual((status, body), (200, {'server': server, 'share': 'locked', 'folder': 'Games/Homebrew',
                                                'user': smb_server.USER, 'workgroup': '', 'password': True}))
        saved = (self.root / 'config/smb.cfg').read_text()
        self.assertIn(f'smb_password = "{smb_server.PASSWORD}"', saved)
        self.assertEqual(self.request('GET', '/api/smb'), (200, body))
        self.assertNotIn(smb_server.PASSWORD, json.dumps(self.request('GET', '/api/smb')[1]))

        # an empty password with the saved server and user is the saved password
        self.assertEqual(self.request('POST', '/api/smb/shares', {**user, 'password': ''})[0], 200)
        status, body = self.request('POST', '/api/smb', {**user, 'password': '', 'share': 'locked', 'folder': 'Games'})
        self.assertEqual((status, body['folder'], body['password']), (200, 'Games', True))
        self.assertIn(f'smb_password = "{smb_server.PASSWORD}"', (self.root / 'config/smb.cfg').read_text())
        # the guest has none
        status, body = self.request('POST', '/api/smb', {**guest, 'password': 'ignored', 'share': 'games'})
        self.assertEqual((status, body['user'], body['password']), (200, '', False))

        # forgotten
        status, body = self.request('DELETE', '/api/smb')
        self.assertEqual((status, body['server'], body['password']), (200, '', False))
        self.assertFalse((self.root / 'config/smb.cfg').exists())

    @unittest.skipUnless(PLAYWRIGHT and shutil.which('node') and Path('/usr/bin/chromium').exists(),
                         'needs node, Playwright and Chromium')
    def test_save_without_a_config_folder(self):
        """Issue 84 (PR 103): on a title that has never written config/, saving still works."""
        guest = {'server': self.samba.address, 'user': '', 'password': '', 'workgroup': ''}
        shutil.rmtree(self.root / 'config')
        try:
            status, body = self.request('POST', '/api/smb', {**guest, 'share': 'games', 'folder': 'Games'})
            self.assertEqual(status, 200, body)
            self.assertTrue((self.root / 'config/smb.cfg').is_file())
        finally:
            (self.root / 'config').mkdir(exist_ok=True)
            (self.root / 'config/smb.cfg').unlink(missing_ok=True)

    def test_page_in_chromium(self):
        run = subprocess.run(['node', 'tests/webui_smb_browser.cjs'], cwd=ROOT, capture_output=True, text=True,
                             timeout=300, env={**os.environ, 'PLAYWRIGHT_PATH': PLAYWRIGHT,
                                               'WEBUI_TEST_URL': f'http://127.0.0.1:{self.port}',
                                               'SMB_SERVER': self.samba.address, 'SMB_USER': smb_server.USER,
                                               'SMB_PASSWORD': smb_server.PASSWORD})
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn('PASS:', run.stdout)


if __name__ == '__main__':
    unittest.main()
