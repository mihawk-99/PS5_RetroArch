"""The WebUI's language: the catalogs (tools/webui-strings.py), the choice kept on the
console (/api/preferences) and the page in Chromium (tests/webui_language_browser.cjs)."""
from pathlib import Path
import http.client
import json
import os
import shutil
import socket
import subprocess
import tempfile
import time
import unittest

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import webui_build  # noqa: E402
from test_webui_transfer_browser import PLAYWRIGHT  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
LANGUAGES = ['en', 'es', 'fr', 'de', 'it', 'pt-BR', 'ru', 'ja', 'ko', 'zh-CN', 'zh-TW', 'vi', 'tr', 'pl', 'nl', 'id']


class Catalogs(unittest.TestCase):
    def test_every_language_has_every_string(self):
        run = subprocess.run([sys.executable, 'tools/webui-strings.py', '--check'], cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        catalogs = sorted(p.stem for p in (ROOT / 'webui/i18n').glob('*.json') if p.stem != 'strings')
        self.assertEqual(catalogs, sorted(LANGUAGES[1:]))
        script = (ROOT / 'webui/i18n.js').read_text()
        for code in LANGUAGES:
            self.assertIn(f"['{code}',", script)
        flags = {'en': 'us', 'es': 'es', 'fr': 'fr', 'de': 'de', 'it': 'it', 'pt-BR': 'br', 'ru': 'ru', 'ja': 'jp', 'ko': 'kr',
                 'zh-CN': 'cn', 'zh-TW': 'tw', 'vi': 'vn', 'tr': 'tr', 'pl': 'pl', 'nl': 'nl', 'id': 'id'}
        source = json.loads((ROOT / 'webui/assets/flags/SOURCE.json').read_text())
        for code, flag in flags.items():
            self.assertTrue((ROOT / f'webui/assets/flags/{flag}.svg').is_file(), flag)
            self.assertIn(flag, source['icons'])


class Server:
    def __init__(self, root):
        subprocess.check_output(['bash', 'tools/build-webui-http.sh', 'host'], cwd=ROOT, text=True)
        subprocess.check_output(['bash', 'tools/build-webui-update.sh', 'host'], cwd=ROOT, text=True)
        self.binary = root.parent / 'server'
        webui_build.build(self.binary, 'tests/webui_server_main.cpp', flags=('-O1',))
        with socket.socket() as s:
            s.bind(('127.0.0.1', 0)); self.port = s.getsockname()[1]
        self.process = subprocess.Popen([str(self.binary), str(root), str(self.port)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(100):
            try:
                with socket.create_connection(('127.0.0.1', self.port), timeout=.1): break
            except OSError:
                if self.process.poll() is not None: raise RuntimeError('HTTP server exited')
                time.sleep(.02)
        self.token = ''
        self.token = self.json('GET', '/api/status')[1]['token']

    def request(self, method, path, token=None):
        conn = http.client.HTTPConnection('127.0.0.1', self.port, timeout=5)
        conn.request(method, path, None, {'X-RetroArch-Token': self.token if token is None else token})
        response = conn.getresponse(); body = response.read(); conn.close()
        return response.status, body

    def json(self, method, path, token=None):
        status, body = self.request(method, path, token)
        return status, json.loads(body)

    def stop(self):
        self.process.terminate(); self.process.wait(timeout=5)


class Preference(unittest.TestCase):
    def test_language_is_kept_on_the_console(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / 'title'
            for name in ('config', 'content', 'webui'):
                (root / name).mkdir(parents=True)
            (root / 'webui/index.html').write_text('<!doctype html><title>RetroArch</title>')
            server = Server(root)
            try:
                saved = root / 'config/webui-preferences.cfg'
                self.assertEqual(server.json('GET', '/api/status')[1]['language'], '')
                self.assertEqual(server.request('POST', '/api/preferences?language=fr', token='wrong')[0], 403)
                for bad in ('evil', '', 'fr%22%0Ainjected%3D1', '../en', 'FR'):
                    self.assertEqual(server.request('POST', '/api/preferences?language=' + bad)[0], 400, bad)
                self.assertFalse(saved.exists())
                for code in ('fr', 'zh-TW', 'pt-BR'):
                    self.assertEqual(server.json('POST', '/api/preferences?language=' + code), (200, {'language': code}))
                    self.assertEqual(saved.read_text(), f'webui_language = "{code}"\n')
                    self.assertEqual(server.json('GET', '/api/status')[1]['language'], code)
                # A value edited by hand to something unknown reads as no choice.
                saved.write_text('webui_language = "xx"\n')
                self.assertEqual(server.json('GET', '/api/status')[1]['language'], '')
            finally:
                server.stop()


@unittest.skipUnless(PLAYWRIGHT and shutil.which('node') and Path('/usr/bin/chromium').exists(), 'needs node, Playwright and Chromium')
class LanguageBrowser(unittest.TestCase):
    def test_picker_translates_and_follows_the_console(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / 'title'
            for name in ('config', 'content', 'webui'):
                (root / name).mkdir(parents=True)
            shutil.copytree(ROOT / 'webui', root / 'webui', dirs_exist_ok=True)
            server = Server(root)
            try:
                shots = Path(os.environ.get('WEBUI_SCREENSHOTS', ROOT / 'build/tmp/webui-language'))
                run = subprocess.run(['node', 'tests/webui_language_browser.cjs'], cwd=ROOT, capture_output=True, text=True, timeout=300,
                                     env={**os.environ, 'PLAYWRIGHT_PATH': PLAYWRIGHT, 'WEBUI_TEST_URL': f'http://127.0.0.1:{server.port}',
                                          'WEBUI_SCREENSHOTS': str(shots)})
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                print(run.stdout.strip())
                self.assertEqual((root / 'config/webui-preferences.cfg').read_text(), 'webui_language = "en"\n')
            finally:
                server.stop()


if __name__ == '__main__':
    unittest.main()
