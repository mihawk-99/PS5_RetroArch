"""ScreenScraper as a scraping source (src/scraper.cpp) on the host, against a fake
ScreenScraper API: signing in (checked with the source, kept on the console 0600, the
password never in an answer), a job matched by checksum, by name and by search, with
details in the language asked for, the account's thread limit kept, an ambiguous game
resolved, a quota that pauses the job until it is resumed, PC mode refused (the media
links carry the developer account), and the sign-in form in a browser. The developer account is a fake one
(PS5_SCRAPER_TEST_DEVID, read by host builds only)."""
from pathlib import Path
import http.client
import http.server
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.parse

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_scraper as base  # noqa: E402
import webui_build  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DB = 'Nintendo - Super Nintendo Entertainment System'
DEV_ID, DEV_PASSWORD = 'fake-dev', 'fake-dev-secret'
USER, PASSWORD = 'tester', 'user-secret-Pw9'
SNES = '4'
# The fake API's games: id -> (names by region, the checksum and file name it knows them by).
GAMES = {
    '1001': ({'us': 'Space Hunter', 'jp': 'Space Hunter (JP)'}, 'D63ED5F8', ''),
    '1002': ({'us': 'Time Voyager', 'jp': 'Time Voyager (JP)'}, '', ''),
    '1003': ({'wor': 'Time Voyager: Hover Race Special'}, '', ''),
    '1004': ({'us': 'Jungle Run 2'}, '', "Jungle Run 2 - Partner's Island Quest (USA).zip"),
}
TYPES = {'box-2D': 'png', 'ss': 'png', 'video-normalized': 'mp4', 'wheel-hd': 'png'}


def media_bytes(game, kind, region):
    return f'{kind}/{region}/{game}'.encode() * 40


def game_json(game_id, base_url):
    names, _, _ = GAMES[game_id]
    medias = []
    for kind, fmt in TYPES.items():
        for region in ('jp', 'us'):
            query = urllib.parse.urlencode({'id': game_id, 'media': kind, 'region': region, 'devid': DEV_ID,
                                            'devpassword': DEV_PASSWORD, 'ssid': USER, 'sspassword': PASSWORD})
            medias.append({'type': kind, 'region': region, 'format': fmt, 'url': f'{base_url}/medias.php?{query}'})
    return {'id': game_id, 'noms': [{'region': r, 'text': n} for r, n in names.items()],
            'synopsis': [{'langue': 'en', 'text': f'English story {game_id}'}, {'langue': 'fr', 'text': f'Histoire {game_id}'}],
            'developpeur': {'text': 'Studio'}, 'editeur': {'text': 'Publisher'}, 'joueurs': {'text': '1-2'},
            'note': {'text': '17'}, 'dates': [{'region': 'jp', 'text': '1994-03-19'}, {'region': 'us', 'text': '1994-04-18'}],
            'genres': [{'noms': [{'langue': 'en', 'text': 'Action'}, {'langue': 'fr', 'text': 'Action FR'}]}],
            'medias': medias}


class FakeScreenScraper(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self):
        super().__init__(('127.0.0.1', 0), Handler)
        self.lock = threading.Lock()
        self.reset()

    def reset(self):
        self.quota_hit = False
        self.calls, self.urls, self.busy, self.most_busy, self.max_threads = [], [], 0, 0, '2'
        self.times = []


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def reply(self, status, body, kind='application/json'):
        # Answered: not busy from here, since the client sends its next request as soon as it
        # has read this one, maybe before this thread gets back from writing it.
        self.uncount()
        data = body if isinstance(body, bytes) else (json.dumps(body) if isinstance(body, dict) else body).encode()
        self.send_response(status); self.send_header('Content-Type', kind); self.send_header('Content-Length', str(len(data)))
        self.end_headers(); self.wfile.write(data)

    def uncount(self):
        with self.server.lock:
            self.server.busy -= self.counted
            self.counted = 0

    def do_GET(self):
        url = urllib.parse.urlsplit(self.path)
        q = dict(urllib.parse.parse_qsl(url.query))
        endpoint = url.path.rsplit('/', 1)[-1]
        server = self.server
        with server.lock:
            server.calls.append((endpoint, q)); server.urls.append(self.path); server.times.append(time.time())
            server.busy += 1; server.most_busy = max(server.most_busy, server.busy); self.counted = 1
        try:
            time.sleep(0.05)
            if (q.get('devid'), q.get('devpassword')) != (DEV_ID, DEV_PASSWORD):
                return self.reply(403, 'Erreur de login : Vérifier vos identifiants développeur !', 'text/plain')
            if endpoint == 'medias.php':
                return self.reply(200, media_bytes(q['id'], q['media'], q['region']), 'image/png')
            if q.get('ssid') != USER or q.get('sspassword') != PASSWORD:
                return self.reply(403, 'Erreur de login : Vérifier vos identifiants utilisateur !', 'text/plain')
            if server.quota_hit and endpoint != 'ssuserInfos.php':
                return self.reply(430, 'Votre quota de scrape est dépassé pour aujourd\'hui !', 'text/plain')
            user = {'id': USER, 'niveau': '1', 'maxthreads': server.max_threads, 'requeststoday': '12', 'maxrequestsperday': '20000'}
            if endpoint == 'ssuserInfos.php':
                return self.reply(200, {'response': {'ssuser': user}})
            base_url = f'http://127.0.0.1:{server.server_port}'
            if q.get('systemeid') != SNES:
                return self.reply(400, 'Erreur : systeme inconnu', 'text/plain')
            if endpoint == 'jeuInfos.php':
                found = q.get('gameid') if q.get('gameid') in GAMES else next(
                    (i for i, (_, crc, rom) in GAMES.items() if (crc and q.get('crc', '').upper() == crc) or (rom and q.get('romnom') == rom)), None)
                if not found:
                    return self.reply(404, 'Erreur : Rom/Iso/Dossier non trouvée !', 'text/plain')
                return self.reply(200, {'response': {'ssuser': user, 'jeu': game_json(found, base_url)}})
            if endpoint == 'jeuRecherche.php':
                words = q.get('recherche', '').lower().split()
                hits = [game_json(i, base_url) for i, (names, _, _) in GAMES.items()
                        if any(all(w in n.lower() for w in words) for n in names.values())]
                return self.reply(200, {'response': {'ssuser': user, 'jeux': hits}})
            return self.reply(404, 'Erreur', 'text/plain')
        finally:
            self.uncount()


class ScreenScraper(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = webui_build.build(Path(cls.temp.name) / 'server', 'tests/webui_server_main.cpp')
        cls.source = FakeScreenScraper()
        threading.Thread(target=cls.source.serve_forever, daemon=True).start()
        cls.libretro = base.FakeLibretro()  # the free source, first in a chain
        threading.Thread(target=cls.libretro.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.source.shutdown()
        cls.libretro.shutdown()
        cls.temp.cleanup()

    def setUp(self):
        self.root = Path(tempfile.mkdtemp(dir=self.temp.name))
        for folder in ('config', 'content/SNES', 'webui', 'cores', 'info', 'playlists'):
            (self.root / folder).mkdir(parents=True)
        (self.root / 'webui/index.html').write_text('<!doctype html><title>RetroArch</title>')
        (self.root / 'cores/snes9x_libretro.so').write_bytes(b'')
        (self.root / 'info/snes9x_libretro.info').write_text(
            'display_name = "Snes9x"\ncorename = "Snes9x"\ndatabase = "' + DB + '"\nsupported_extensions = "sfc|smc|zip"\n')
        self.games = {  # label -> (path, crc)
            'Space Hunter (Japan, USA) (En,Ja)': ('/app0/content/SNES/Space Hunter (Japan, USA) (En,Ja).sfc', 'D63ED5F8|crc'),  # by checksum
            "Jungle Run 2 - Partner's Island Quest (USA)": ("/app0/content/SNES/Jungle Run 2 - Partner's Island Quest (USA).zip", ''),  # by file name
            'Time Voyager (1995)': ('/app0/content/SNES/Time Voyager (1995).sfc', ''),  # by search, the same title
            'Voyager': ('/app0/content/SNES/Voyager.sfc', ''),  # search: two titles, ambiguous
            'Totally Unknown Homebrew': ('/app0/content/SNES/Totally Unknown Homebrew.sfc', ''),  # nowhere
        }
        for path, _ in self.games.values():  # the files, so their sizes are sent
            (self.root / path.replace('/app0/', '')).write_bytes(b'rom' * 100)
        items = [{'path': p, 'label': l, 'core_path': 'DETECT', 'core_name': 'DETECT', 'crc32': c, 'db_name': DB + '.lpl'}
                 for l, (p, c) in self.games.items()]
        (self.root / f'playlists/{DB}.lpl').write_text(json.dumps({'version': '1.5', 'items': items}))
        with self.source.lock:
            self.source.reset()
        with self.libretro.lock:
            self.libretro.log.clear()
            self.libretro.times.clear()
        self.start_server()

    def start_server(self, developer=True):
        env = {**os.environ, 'PS5_SCRAPER_SCREENSCRAPER_BASE': f'http://127.0.0.1:{self.source.server_port}',
               'PS5_SCRAPER_LIBRETRO_BASE': f'http://127.0.0.1:{self.libretro.server_port}'}
        if developer:
            env.update(PS5_SCRAPER_TEST_DEVID=DEV_ID, PS5_SCRAPER_TEST_DEVPASSWORD=DEV_PASSWORD)
        self.server, self.port, self.token = webui_build.start(self.binary, self.root, env)

    def tearDown(self):
        if self.server.poll() is None:
            self.server.terminate(); self.server.wait(timeout=10)

    def request(self, method, path, body=None):
        conn = http.client.HTTPConnection('127.0.0.1', self.port, timeout=60)
        conn.request(method, path, body, {'X-RetroArch-Token': getattr(self, 'token', '')})
        response = conn.getresponse()
        data = response.read()
        conn.close()
        # No answer of the console ever carries the user's password or the developer's.
        for secret in (PASSWORD, DEV_PASSWORD, DEV_ID):
            self.assertNotIn(secret.encode(), data, f'{method} {path}')
        return response.status, data

    def sign_in(self, user=USER, password=PASSWORD):
        return self.request('POST', '/api/scraper/account?source=screenscraper', f'{user}\n{password}'.encode())

    def source_entry(self):
        settings = json.loads(self.request('GET', '/api/scraper/settings')[1])
        return next(s for s in settings['sources'] if s['id'] == 'screenscraper')

    def start(self, mode='ps5', kinds='cover,screenshot,video', language='fr', details=True, overwrite=False):
        query = f'mode={mode}&source=screenscraper&kinds={kinds}&region=us&language={language}&overwrite={int(overwrite)}&details={int(details)}'
        status, body = self.request('POST', f'/api/scraper/start?{query}', b'snes\t')
        self.assertEqual(status, 201, body)
        return json.loads(body)['job']['id']

    def job(self, job_id):
        return json.loads(self.request('GET', f'/api/scraper/job?id={job_id}')[1])['job']

    def wait(self, job_id, until=lambda j: j['state'] != 'running', timeout=60):
        end = time.time() + timeout
        while time.time() < end:
            job = self.job(job_id)
            if until(job):
                return job
            time.sleep(0.1)
        self.fail(f'the job did not settle: {self.job(job_id)}')

    def key(self, label):
        return Path(self.games[label][0]).stem

    def test_sign_in_is_checked_kept_private_and_forgotten(self):
        self.assertEqual(self.source_entry()['available'], True)
        self.assertEqual(self.source_entry()['signed_in'], False)
        # No job before signing in.
        status, body = self.request('POST', '/api/scraper/start?mode=ps5&source=screenscraper&kinds=cover', b'snes\t')
        self.assertEqual((status, json.loads(body)['error']), (409, 'Sign in to ScreenScraper first.'))
        # A wrong password is refused, and nothing is kept.
        status, body = self.sign_in(password='wrong')
        self.assertEqual(status, 403)
        self.assertIn('did not accept', json.loads(body)['error'])
        self.assertFalse((self.root / 'config/private/screenscraper.cfg').exists())
        # The right one: the name and quotas come back, never the password.
        status, body = self.sign_in()
        self.assertEqual(status, 200, body)
        account = json.loads(body)
        self.assertEqual((account['signed_in'], account['user'], account['max_threads'], account['max_requests']),
                         (True, USER, '2', '20000'))
        self.assertNotIn('password', account)
        self.assertEqual(self.source_entry()['signed_in'], True)
        stored = self.root / 'config/private/screenscraper.cfg'
        self.assertEqual(stat.S_IMODE(stored.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(stored.parent.stat().st_mode), 0o700)
        # The sign-in went to the source in the request, not in the console's address.
        self.assertTrue(any(e == 'ssuserInfos.php' and q.get('ssid') == USER for e, q in self.source.calls))
        # Kept across a restart; forgotten on request.
        self.server.terminate(); self.server.wait(timeout=10)
        self.start_server()
        self.assertEqual(json.loads(self.request('GET', '/api/scraper/account?source=screenscraper')[1])['user'], USER)
        status, body = self.request('DELETE', '/api/scraper/account?source=screenscraper')
        self.assertEqual((status, json.loads(body)['signed_in']), (200, False))
        self.assertFalse(stored.exists())
        # Signing in and out needs the page's token.
        self.token = ''
        self.assertEqual(self.sign_in()[0], 403)
        self.assertFalse(stored.exists())

    def test_a_build_without_the_developer_account_offers_no_screenscraper(self):
        self.server.terminate(); self.server.wait(timeout=10)
        self.start_server(developer=False)
        self.assertEqual(self.source_entry()['available'], False)
        status, body = self.sign_in()
        self.assertEqual((status, json.loads(body)['error']), (403, 'This build has no ScreenScraper developer account.'))
        self.assertFalse(self.source.calls)

    def test_ps5_mode_checksum_name_search_details_and_thread_limit(self):
        self.assertEqual(self.sign_in()[0], 200)
        # Database IDs belong to their source; a LaunchBox choice is not a ScreenScraper ID.
        key = self.key('Space Hunter (Japan, USA) (En,Ja)')
        query = urllib.parse.urlencode({'system': 'snes', 'game': key})
        self.assertEqual(self.request('POST', '/api/library/game?' + query,
                         json.dumps({'partial': 'true', 'launchbox_id': '2002'}).encode())[0], 200)
        job_id = self.start()
        job = self.wait(job_id)
        self.assertEqual(job['state'], 'done', job)
        self.assertEqual(job['counts'], {'ambiguous': 1, 'done': 3, 'unmatched': 1})
        self.assertLessEqual(self.source.most_busy, 2)  # two games at once, their media downloads in their turn
        infos = [q for e, q in self.source.calls if e == 'jeuInfos.php']
        self.assertFalse(any(q.get('gameid') == '2002' for q in infos))
        self.assertTrue(any(q.get('crc') == 'D63ED5F8' and q.get('romtaille') == '300' for q in infos))
        library = self.root / 'library/snes'
        hunter = self.key('Space Hunter (Japan, USA) (En,Ja)')
        self.assertEqual((library / 'covers' / f'{hunter}.png').read_bytes(), media_bytes('1001', 'box-2D', 'us'))  # the region asked for
        self.assertEqual((library / 'videos' / f'{hunter}.mp4').read_bytes(), media_bytes('1001', 'video-normalized', 'us'))
        meta = (library / 'metadata' / f'{hunter}.meta').read_text()
        for line in ('name = "Space Hunter"', 'description = "Histoire 1001"', 'genre = "Action FR"', 'rating = "0.85"',
                     'released = "1994-04-18"', 'developer = "Studio"', 'players = "1-2"', 'source = "screenscraper"'):
            self.assertIn(line, meta)
        dk = self.key("Jungle Run 2 - Partner's Island Quest (USA)")
        self.assertTrue((library / 'covers' / f'{dk}.png').exists())
        self.assertEqual((library / 'covers' / f'{self.key("Time Voyager (1995)")}.png').read_bytes(),
                         media_bytes('1002', 'box-2D', 'us'))
        # Nothing on the console's disk holds the user's password but its own account file.
        for path in self.root.rglob('*'):
            if path.is_file() and path.name != 'screenscraper.cfg':
                self.assertNotIn(PASSWORD.encode(), path.read_bytes(), str(path))
        # The recap: what each game got, per kind, and what is missing.
        recap = json.loads(self.request('GET', f'/api/scraper/recap?id={job_id}')[1])['recap']
        self.assertEqual(recap['totals'], {k: {'got': 3, 'had': 0, 'missed': 2} for k in ('cover', 'screenshot', 'video', 'details')})
        self.assertEqual(recap['kinds'], ['cover', 'screenshot', 'video', 'details'])
        games = {g['label']: g for g in recap['systems'][0]['games']}
        self.assertEqual(games['Space Hunter (Japan, USA) (En,Ja)']['got'], ['cover', 'screenshot', 'video', 'details'])
        self.assertEqual(games['Space Hunter (Japan, USA) (En,Ja)']['matched'], 'Space Hunter')
        self.assertEqual((games['Totally Unknown Homebrew']['state'], games['Totally Unknown Homebrew']['missed']),
                         ('unmatched', ['cover', 'screenshot', 'video', 'details']))
        # The ambiguous game: two titles offered as "Name [id]", one chosen.
        ambiguous = next(p for p in job['problems'] if p['state'] == 'ambiguous')
        self.assertEqual(sorted(ambiguous['candidates']), ['Time Voyager [1002]', 'Time Voyager: Hover Race Special [1003]'])
        status, body = self.request('POST', f'/api/scraper/resolve?id={job_id}&item={ambiguous["item"]}&action=choose&value='
                                    + urllib.parse.quote('Time Voyager: Hover Race Special [1003]'))
        self.assertEqual(status, 200, body)
        self.wait(job_id, lambda j: j['state'] == 'done' and not j['counts'].get('pending') and not j['counts'].get('working'))
        self.assertEqual((library / 'covers' / f'{self.key("Voyager")}.png').read_bytes(), media_bytes('1003', 'box-2D', 'us'))
        # A search from the page, by the user's words.
        unmatched = next(p for p in job['problems'] if p['state'] == 'unmatched')
        status, body = self.request('POST', f'/api/scraper/resolve?id={job_id}&item={unmatched["item"]}&action=search&value=hunter')
        found = next(p for p in json.loads(body)['job']['problems'] if p['item'] == unmatched['item'])
        self.assertEqual(found['candidates'], ['Space Hunter [1001]'])
        # One game at a time when that is all the account may do.
        with self.source.lock:
            self.source.reset(); self.source.max_threads = '1'
        self.sign_in()
        shutil.rmtree(library / 'covers')
        second = self.wait(self.start(kinds='cover,screenshot'))
        self.assertEqual(self.source.most_busy, 1)
        # Covers fetched again, screenshots kept: the recap tells them apart (the ambiguous
        # game is asked again in a new job: its screenshot kept, its cover missing).
        totals = json.loads(self.request('GET', f'/api/scraper/recap?id={second["id"]}')[1])['recap']['totals']
        self.assertEqual(totals['cover'], {'got': 3, 'had': 0, 'missed': 2})
        self.assertEqual(totals['screenshot'], {'got': 0, 'had': 4, 'missed': 1})
        # A search from the page waits its turn too, while a chosen game is fetched.
        item = {p['state']: p['item'] for p in second['problems']}
        self.request('POST', f'/api/scraper/resolve?id={second["id"]}&item={item["ambiguous"]}&action=choose&value='
                     + urllib.parse.quote('Time Voyager: Hover Race Special [1003]'))
        status, body = self.request('POST', f'/api/scraper/resolve?id={second["id"]}&item={item["unmatched"]}&action=search&value=hunter')
        self.assertEqual(status, 200, body)
        self.wait(second['id'], lambda j: j['state'] == 'done' and not j['counts'].get('pending') and not j['counts'].get('working'))
        self.assertEqual(self.source.most_busy, 1)

    def test_details_alone_or_media_alone(self):
        self.assertEqual(self.sign_in()[0], 200)
        library = self.root / 'library/snes'
        hunter = self.key('Space Hunter (Japan, USA) (En,Ja)')
        # Media without details: the pictures, a name, no description.
        self.wait(self.start(kinds='cover', details=False))
        self.assertTrue((library / 'covers' / f'{hunter}.png').exists())
        meta = (library / 'metadata' / f'{hunter}.meta').read_text()
        self.assertIn('name = "Space Hunter"', meta)
        self.assertNotIn('description', meta)
        # Details alone: no kind of media at all, the details filled, no media fetched.
        with self.source.lock:
            self.source.calls.clear()
        job = self.wait(self.start(kinds='', details=True))
        self.assertEqual(job['counts'], {'ambiguous': 1, 'done': 3, 'unmatched': 1})
        self.assertIn('description = "Histoire 1001"', (library / 'metadata' / f'{hunter}.meta').read_text())
        self.assertFalse(any(e == 'medias.php' for e, _ in self.source.calls))
        recap = json.loads(self.request('GET', f'/api/scraper/recap?id={job["id"]}')[1])['recap']
        self.assertEqual((recap['kinds'], recap['totals']['details']), (['details'], {'got': 3, 'had': 0, 'missed': 2}))
        # Again: details already there are kept (nothing to ask), unless replacing.
        again = self.wait(self.start(kinds='', details=True))
        self.assertEqual(again['counts'].get('skipped'), 3)
        # A hand-edited description survives replacing everything.
        path = library / 'metadata' / f'{hunter}.meta'
        path.write_text(path.read_text().replace('Histoire 1001', 'Mine') + 'edited = "description"\n')
        self.wait(self.start(kinds='', details=True, overwrite=True))
        self.assertIn('description = "Mine"', path.read_text())
        # Neither: refused.
        status, body = self.request('POST', '/api/scraper/start?mode=ps5&source=screenscraper&kinds=&details=0', b'snes\t')
        self.assertEqual((status, json.loads(body)['error']), (409, 'Choose at least one kind of media, or the games\' details.'))

    def chain(self, kinds='cover,screenshot,video', details=True):
        query = f'mode=ps5&sources=libretro,screenscraper&kinds={kinds}&region=us&language=en&overwrite=0&details={int(details)}'
        status, body = self.request('POST', f'/api/scraper/start?{query}', b'snes\t')
        self.assertEqual(status, 201, body)
        return json.loads(body)['job']['id']

    def first_lookup(self):
        """When ScreenScraper was first asked about a game (not the sign-in)."""
        with self.source.lock:
            return min(at for at, (endpoint, _) in zip(self.source.times, self.source.calls) if endpoint != 'ssuserInfos.php')

    def ss_media(self):
        with self.source.lock:
            return [q['media'] for e, q in self.source.calls if e == 'medias.php']

    def test_a_chain_asks_the_next_source_only_for_what_is_missing(self):
        self.assertEqual(self.sign_in()[0], 200)
        with self.libretro.lock:
            self.libretro.times.clear()
        with self.source.lock:
            self.source.times.clear()
        job_id = self.chain()
        job = self.wait(job_id)
        # A funnel: libretro's pass over every game ends before ScreenScraper's begins.
        self.assertLess(max(self.libretro.times), self.first_lookup())
        # Each pass counted: libretro checked all five and gave box art and screenshots to
        # three; all five went on to ScreenScraper (videos and details are not libretro's).
        self.assertEqual([(p['source'], p['checked'], p['games'], p['files']) for p in job['passes']],
                         [('libretro', 5, 3, 6), ('screenscraper', 5, 3, 6)])
        self.assertEqual(job['sources'], 'libretro,screenscraper')
        self.assertEqual(job['counts'], {'ambiguous': 1, 'done': 3, 'unmatched': 1}, job)
        # libretro gave the box art and screenshots; ScreenScraper only the videos (and
        # the details): not one picture asked of it.
        self.assertEqual(sorted(self.ss_media()), ['video-normalized'] * 3)
        library = self.root / 'library/snes'
        hunter = self.key('Space Hunter (Japan, USA) (En,Ja)')
        self.assertEqual((library / 'covers' / f'{hunter}.png').read_bytes(), base.image('Space Hunter (Japan, USA) (En,Ja)', 'Named_Boxarts'))
        self.assertEqual((library / 'videos' / f'{hunter}.mp4').read_bytes(), media_bytes('1001', 'video-normalized', 'us'))
        self.assertIn('description = "English story 1001"', (library / 'metadata' / f'{hunter}.meta').read_text())
        # The game no source could tell: libretro's choices first; once chosen, what is
        # left goes to ScreenScraper, which offers its own choices for it.
        voyager = next(p for p in job['problems'] if p['label'] == 'Voyager')
        self.assertIn('Time Voyager (USA)', voyager['candidates'])
        self.assertIn('libretro offers these', voyager['message'])
        self.request('POST', f'/api/scraper/resolve?id={job_id}&item={voyager["item"]}&action=choose&value=' + urllib.parse.quote('Time Voyager (USA)'))
        job = self.wait(job_id, lambda j: j['state'] == 'done' and not j['counts'].get('pending') and not j['counts'].get('working'))
        voyager = next(p for p in job['problems'] if p['label'] == 'Voyager')
        self.assertIn('Some found; for the rest, ScreenScraper offers these', voyager['message'])
        self.assertIn('Time Voyager: Hover Race Special [1003]', voyager['candidates'])
        self.assertTrue((library / 'covers' / 'Voyager.png').exists())
        self.request('POST', f'/api/scraper/resolve?id={job_id}&item={voyager["item"]}&action=choose&value=' + urllib.parse.quote('Time Voyager: Hover Race Special [1003]'))
        job = self.wait(job_id, lambda j: j['state'] == 'done' and not j['counts'].get('pending') and not j['counts'].get('working'))
        self.assertEqual(job['counts'], {'done': 4, 'unmatched': 1})
        self.assertEqual((library / 'videos' / 'Voyager.mp4').read_bytes(), media_bytes('1003', 'video-normalized', 'us'))
        self.assertEqual((library / 'covers' / 'Voyager.png').read_bytes(), base.image('Time Voyager (USA)', 'Named_Boxarts'))  # libretro's kept
        unknown = next(p for p in job['problems'] if p['label'] == 'Totally Unknown Homebrew')
        self.assertEqual(unknown['message'], 'Not found at libretro or ScreenScraper. Search by name or skip.')
        recap = json.loads(self.request('GET', f'/api/scraper/recap?id={job_id}')[1])['recap']
        self.assertEqual(recap['totals']['video'], {'got': 4, 'had': 0, 'missed': 1})

    def test_a_one_game_account_holds_back_only_screenscraper(self):
        # A free account asks about one game at a time; libretro, first, is not held back.
        with self.source.lock:
            self.source.max_threads = '1'
        self.assertEqual(self.sign_in()[0], 200)
        with self.libretro.lock:
            self.libretro.delay, self.libretro.most_busy = 0.3, 0
        try:
            self.wait(self.chain(kinds='cover,video', details=False))
        finally:
            with self.libretro.lock:
                self.libretro.delay = 0.0
        self.assertGreater(self.libretro.most_busy, 1)
        self.assertEqual(self.source.most_busy, 1)
        self.assertLess(max(self.libretro.times), self.first_lookup())  # its pass first

    def test_a_chain_paused_by_a_quota_does_not_ask_again_what_it_found(self):
        self.assertEqual(self.sign_in()[0], 200)
        with self.source.lock:
            self.source.quota_hit = True
        job_id = self.chain(kinds='cover,video', details=False)
        job = self.wait(job_id, lambda j: j['state'] == 'paused' and not j['counts'].get('working'))
        self.assertIn('quota for today is used up', job['message'])
        served = lambda: [p for m, a, p in self.libretro.log if m == 'GET' and p.endswith('.png') and p.split('/')[-1][:-4] in base.SOURCE]
        with self.libretro.lock:
            self.assertGreater(len(served()), 0)  # libretro served some before ScreenScraper stopped
        with self.source.lock:
            self.source.quota_hit = False
        self.assertEqual(self.request('POST', f'/api/scraper/resume?id={job_id}')[0], 200)
        job = self.wait(job_id)
        self.assertEqual(job['counts'], {'ambiguous': 1, 'done': 3, 'unmatched': 1}, job)
        with self.libretro.lock:
            covers = served()
        self.assertEqual(len(covers), len(set(covers)), covers)  # no cover libretro gave was fetched again
        self.assertEqual(len(covers), 3)
        self.assertEqual(sorted(self.ss_media()), ['video-normalized'] * 3)

    def test_a_chain_is_ps5_only_and_remembered(self):
        self.assertEqual(self.sign_in()[0], 200)
        status, body = self.request('POST', '/api/scraper/start?mode=pc&sources=libretro,screenscraper&kinds=cover', b'snes\t')
        self.assertEqual((status, json.loads(body)['error']), (409, 'This PC → PS5 takes one source at a time. Choose PS5 to combine sources.'))
        self.request('POST', '/api/scraper/settings?mode=ps5&sources=screenscraper,libretro&kinds=cover')
        self.assertEqual(json.loads(self.request('GET', '/api/scraper/settings')[1])['chain'], ['screenscraper', 'libretro'])

    def test_a_quota_pauses_the_job_until_resumed(self):
        self.assertEqual(self.sign_in()[0], 200)
        with self.source.lock:
            self.source.quota_hit = True
        job_id = self.start(kinds='cover')
        # Paused: every game back to waiting once the workers holding one let go.
        job = self.wait(job_id, lambda j: j['state'] == 'paused' and not j['counts'].get('working'))
        self.assertIn('quota for today is used up', job['message'])
        self.assertEqual(job['counts'].get('pending'), 5)
        with self.source.lock:
            self.source.quota_hit = False
        self.assertEqual(self.request('POST', f'/api/scraper/resume?id={job_id}')[0], 200)
        job = self.wait(job_id)
        self.assertEqual(job['counts'], {'ambiguous': 1, 'done': 3, 'unmatched': 1})

    def test_pc_mode_is_refused_so_no_account_leaves_the_console(self):
        # ScreenScraper's media links carry the developer account: no helper may get them.
        self.assertEqual(self.sign_in()[0], 200)
        status, body = self.request('POST', '/api/scraper/start?mode=pc&source=screenscraper&kinds=cover', b'snes\t')
        self.assertEqual(status, 409)
        self.assertIn('run on the PS5 only', json.loads(body)['error'])
        self.assertFalse(any(e == 'jeuInfos.php' for e, _ in self.source.calls))

    def test_sign_in_form_in_a_browser(self):
        playwright = next((str(p) for p in (Path.home() / '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright-core',
                                            Path('/usr/lib/chatgpt/resources/cua_node/lib/node_modules/playwright-core')) if p.exists()),
                          os.environ.get('PLAYWRIGHT_PATH', ''))
        if not playwright or not Path('/usr/bin/chromium').exists():
            self.skipTest('needs Playwright and Chromium')
        shutil.copytree(ROOT / 'webui', self.root / 'webui', dirs_exist_ok=True)
        run = subprocess.run(['node', 'tests/webui_screenscraper_browser.cjs'], cwd=ROOT, capture_output=True, text=True, timeout=300,
                             env={**os.environ, 'PLAYWRIGHT_PATH': playwright, 'WEBUI_TEST_URL': f'http://127.0.0.1:{self.port}',
                                  'SS_USER': USER, 'SS_PASSWORD': PASSWORD})
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertEqual(json.loads(self.request('GET', '/api/scraper/account?source=screenscraper')[1])['signed_in'], False)


if __name__ == '__main__':
    unittest.main()
