"""LaunchBox Games Database as a scraping source (src/scraper.cpp) on the host, against a
fake LaunchBox: its database fetched once and indexed a platform, games matched by title,
by an alternate name and, for arcade romsets, by MAME's name; images in the region asked
for; the games' details; an ambiguous game resolved; PC mode through the real helper; a
chain where LaunchBox is asked only for what libretro lacks; a failed database download
that pauses the job until resumed."""
from pathlib import Path
import http.server
import io
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.parse
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_scraper as base  # noqa: E402
import webui_build  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
SNES = 'Super Nintendo Entertainment System'
GAMES = [  # id, name, platform, extra fields
    ('2001', "Jungle Run 2: Partner&apos;s Island Quest", SNES,
     '<ReleaseDate>1995-11-20T00:00:00-08:00</ReleaseDate><Overview>Partner &amp; Friend.</Overview>'
     '<Developer>Rare</Developer><Publisher>Nintendo</Publisher><Genres>Platform; Action</Genres>'
     '<MaxPlayers>2</MaxPlayers><CommunityRating>4.5</CommunityRating>'),
    ('2002', 'Hunter III', SNES, '<ReleaseYear>1994</ReleaseYear>'),
    ('2003', 'Time Voyager', SNES, ''),
    ('2004', 'Voyager Cross', 'Sony Playstation', ''),
    ('2005', 'Time Voyager: Hover Race Special', SNES, ''),
    ('3001', 'Run and Gun 3', 'Arcade', '<Developer>SNK</Developer>'),
]
ALTERNATE = [('2002', 'Space Hunter')]
IMAGES = [  # id, type, region, file
    ('2001', 'Box - Front', 'Japan', 'dk-jp.png'), ('2001', 'Box - Front', 'North America', 'dk-us.png'),
    ('2001', 'Clear Logo', '', 'dk-logo.png'), ('2001', 'Screenshot - Gameplay', 'North America', 'dk-shot.jpg'),
    ('2001', 'Banner', '', 'dk-banner.png'), ('2002', 'Box - Front', '', 'hunter.png'),
    ('2003', 'Box - Front', 'World', 'voyager.png'), ('2003', 'Clear Logo', '', 'voyager-logo.png'),
    ('2004', 'Box - Front', '', 'cross.png'), ('2005', 'Box - Front', '', 'jetbike.png'),
    ('3001', 'Box - Front', '', 'runner3.png'), ('3001', 'Clear Logo', '', 'runner3-logo.png'),
]


def picture(name):
    return f'IMAGE {name}'.encode() * 30


def metadata_zip(entries=GAMES):
    games = ''.join(f'<Game><Name>{n}</Name><DatabaseID>{i}</DatabaseID><Platform>{p}</Platform>{x or "<Genres />"}</Game>'
                    for i, n, p, x in entries)
    alternate = ''.join(f'<GameAlternateName><AlternateName>{n}</AlternateName><DatabaseID>{i}</DatabaseID></GameAlternateName>'
                        for i, n in ALTERNATE)
    images = ''.join(f'<GameImage><DatabaseID>{i}</DatabaseID><FileName>{f}</FileName><Type>{t}</Type>'
                     + (f'<Region>{r}</Region>' if r else '') + '<CRC32>1</CRC32></GameImage>' for i, t, r, f in IMAGES)
    mame = ('<MameFile><FileName>runner3</FileName><Name>Run and Gun 3</Name><Status>good</Status></MameFile>'
            '<MameFile><FileName>runner3a</FileName><Name>Run and Gun 3</Name><CloneOf>runner3</CloneOf></MameFile>')
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('Metadata.xml', f'<?xml version="1.0" standalone="yes"?>\n<LaunchBox>\n{games}{alternate}{images}</LaunchBox>')
        z.writestr('Mame.xml', f'<?xml version="1.0" standalone="yes"?>\n<LaunchBox>{mame}</LaunchBox>')
        z.writestr('Platforms.xml', '<LaunchBox />')
    return out.getvalue()


class FakeLaunchBox(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self):
        super().__init__(('127.0.0.1', 0), Handler)
        self.lock, self.zip = threading.Lock(), metadata_zip()
        self.reset()

    def reset(self):
        self.log, self.broken = [], False


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        path = urllib.parse.unquote(self.path)
        with self.server.lock:
            self.server.log.append(path)
        if path == '/Metadata.zip' and not self.server.broken:
            data = self.server.zip
        elif path.startswith('/images/') and any(f == path[8:] for *_, f in IMAGES):
            data = picture(path[8:])
        else:
            self.send_response(500 if path == '/Metadata.zip' else 404); self.send_header('Content-Length', '0'); self.end_headers(); return
        self.send_response(200); self.send_header('Content-Length', str(len(data))); self.end_headers(); self.wfile.write(data)


class LaunchBox(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = webui_build.build(Path(cls.temp.name) / 'server', 'tests/webui_server_main.cpp')
        cls.source = FakeLaunchBox()
        threading.Thread(target=cls.source.serve_forever, daemon=True).start()
        cls.libretro = base.FakeLibretro()
        threading.Thread(target=cls.libretro.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.source.shutdown()
        cls.libretro.shutdown()
        cls.temp.cleanup()

    def setUp(self):
        self.root = Path(tempfile.mkdtemp(dir=self.temp.name))
        for folder in ('config', 'content', 'webui', 'cores', 'info', 'playlists'):
            (self.root / folder).mkdir()
        (self.root / 'webui/index.html').write_text('<!doctype html><title>RetroArch</title>')
        for core, database, extensions in (('snes9x', base.DB, 'sfc|smc|zip'), ('fbneo', 'FBNeo - Arcade Games', 'zip')):
            (self.root / f'cores/{core}_libretro.so').write_bytes(b'')
            (self.root / f'info/{core}_libretro.info').write_text(
                f'display_name = "{core}"\ncorename = "{core}"\ndatabase = "{database}"\nsupported_extensions = "{extensions}"\n')
        self.games = {  # label -> (path, database)
            "Jungle Run 2 - Partner's Island Quest (USA)": ("/app0/content/SNES/Jungle Run 2 - Partner's Island Quest (USA).zip", base.DB),
            'Space Hunter (Japan, USA) (En,Ja)': ('/app0/content/SNES/Space Hunter (Japan, USA) (En,Ja).sfc', base.DB),  # an alternate name
            'Time Voyager (1995)': ('/app0/content/SNES/Time Voyager (1995).sfc', base.DB),
            'Time Voyager Special Edition': ('/app0/content/SNES/Time Voyager SE.sfc', base.DB),  # ambiguous
            'Totally Unknown Homebrew': ('/app0/content/SNES/Totally Unknown Homebrew.sfc', base.DB),
            'runner3': ('/app0/content/FBNeo/runner3.zip', 'FBNeo - Arcade Games'),  # a romset
        }
        for database in {d for _, d in self.games.values()}:
            items = [{'path': p, 'label': l, 'core_path': 'DETECT', 'core_name': 'DETECT', 'crc32': '', 'db_name': d + '.lpl'}
                     for l, (p, d) in self.games.items() if d == database]
            (self.root / f'playlists/{database}.lpl').write_text(json.dumps({'version': '1.5', 'items': items}))
        with self.source.lock:
            self.source.reset()
        with self.libretro.lock:
            self.libretro.log.clear()
        self.start_server()

    def start_server(self):
        env = {**os.environ, 'PS5_SCRAPER_LAUNCHBOX_BASE': f'http://127.0.0.1:{self.source.server_port}',
               'PS5_SCRAPER_LIBRETRO_BASE': f'http://127.0.0.1:{self.libretro.server_port}'}
        self.server, self.port, self.token = webui_build.start(self.binary, self.root, env)

    def tearDown(self):
        if self.server.poll() is None:
            self.server.terminate(); self.server.wait(timeout=10)

    request = base.Scraper.request

    def start(self, mode='ps5', sources='launchbox', kinds='cover,logo,screenshot', details=True, lines=('snes\t', 'fbneo\t')):
        query = f'mode={mode}&sources={sources}&kinds={kinds}&region=us&language=en&overwrite=0&details={int(details)}'
        status, body = self.request('POST', f'/api/scraper/start?{query}', '\n'.join(lines).encode())
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

    def stored(self, label, folder, ext='.png'):
        path, database = self.games[label]
        system = 'snes' if database == base.DB else 'fbneo'
        return self.root / 'library' / system / folder / (Path(path).stem + ext)

    def fetched(self, prefix):
        with self.source.lock:
            return [p for p in self.source.log if p.startswith(prefix)]

    def test_lookup_details_before_game_upload(self):
        original = self.source.zip
        self.addCleanup(setattr, self.source, 'zip', original)
        self.source.zip = metadata_zip(GAMES + [('4000', 'Space Hunter', SNES, ''), ('4001', 'Space Hunter (MSU-1 Enhanced)', SNES, '')] + [(str(5000 + i), f'Space Hunter Variant {i:02}', SNES, '') for i in range(30)])
        status, body = self.request('POST', '/api/library/launchbox?system=snes&q=Space%20Hunter')
        self.assertEqual(status, 200, body)
        results = json.loads(body)['results']
        self.assertTrue(results)
        self.assertEqual(results[0]['id'], '4000')  # Literal title beats an edition with the same normalized key.
        self.assertEqual(results[1]['id'], '2002')  # Exact alias still beats partial/variant names.
        self.assertEqual(json.loads(self.request('POST', '/api/library/launchbox?system=snes&q=space%20hunter')[1])['results'][0]['id'], '4000')
        self.assertEqual(len(results), 20)
        self.assertIn('description', results[0])
        self.assertIn('id', results[0])
        self.assertEqual(self.request('POST', '/api/library/launchbox?system=unknown&q=Hunter')[0], 400)
        status, body = self.request('PUT', '/api/library/add?system=snes&filename=My%20Custom%20Backup.sfc', b'synthetic game')
        self.assertEqual(status, 201, body)
        game = json.loads(body)
        query = urllib.parse.urlencode({'system': 'snes', 'game': game['key']})
        self.assertEqual(self.request('POST', '/api/library/game?' + query, json.dumps({'partial': 'true', 'name': 'A custom display name', 'launchbox_id': '2002'}).encode())[0], 200)
        job = self.start(kinds='cover', details=True, lines=('snes\t' + game['path'],))
        self.wait(job)
        info = json.loads(self.request('GET', '/api/library/game?' + query)[1])
        self.assertEqual(info['details']['name'], 'A custom display name')
        self.assertTrue(next(k for k in info['kinds'] if k['id'] == 'cover')['present'])


    def test_ps5_mode_titles_alternate_names_romsets_regions_and_details(self):
        self.assertEqual({s['id']: s['available'] for s in json.loads(self.request('GET', '/api/scraper/settings')[1])['sources']}['launchbox'], True)
        # Add Game without a database choice still permits later ambiguity resolution.
        query = urllib.parse.urlencode({'system': 'snes', 'game': 'Time Voyager SE'})
        self.assertEqual(self.request('POST', '/api/library/game?' + query,
                         json.dumps({'partial': 'true', 'launchbox_id': ''}).encode())[0], 200)
        job_id = self.start()
        job = self.wait(job_id)
        self.assertEqual(job['state'], 'done', job)
        self.assertEqual(job['counts'], {'ambiguous': 1, 'done': 1, 'partial': 3, 'unmatched': 1}, job)
        dk = "Jungle Run 2 - Partner's Island Quest (USA)"
        self.assertEqual(self.stored(dk, 'covers').read_bytes(), picture('dk-us.png'))  # North America over Japan
        self.assertEqual(self.stored(dk, 'marquees').read_bytes(), picture('dk-logo.png'))
        self.assertEqual(self.stored(dk, 'screenshots', '.jpg').read_bytes(), picture('dk-shot.jpg'))
        meta = (self.root / 'library/snes/metadata' / (Path(self.games[dk][0]).stem + '.meta')).read_text()
        for line in ('description = "Partner & Friend."', 'developer = "Rare"', 'released = "1995-11-20"',
                     'genre = "Platform, Action"', 'players = "2"', 'rating = "0.90"', 'source = "launchbox"'):
            self.assertIn(line, meta)
        self.assertEqual(self.stored('Space Hunter (Japan, USA) (En,Ja)', 'covers').read_bytes(), picture('hunter.png'))
        self.assertEqual(self.stored('Time Voyager (1995)', 'covers').read_bytes(), picture('voyager.png'))
        self.assertEqual(self.stored('runner3', 'covers').read_bytes(), picture('runner3.png'))  # MAME's name for the romset
        self.assertIn('name = "Run and Gun 3"', (self.root / 'library/fbneo/metadata/runner3.meta').read_text())
        self.assertFalse(self.fetched('/images/cross.png'))  # another platform's game is never taken
        # The database: fetched once, kept as a small index a platform, the zip deleted.
        self.assertEqual(len(self.fetched('/Metadata.zip')), 1)
        index = self.root / 'library/.launchbox'
        self.assertTrue((index / 'super_nintendo_entertainment_system.index').exists())
        self.assertFalse((index / 'Metadata.zip').exists())
        # The ambiguous game: LaunchBox's names offered, one chosen; a search by words.
        problem = next(p for p in job['problems'] if p['label'] == 'Time Voyager Special Edition')
        self.assertIn('Time Voyager: Hover Race Special [2005]', problem['candidates'])
        self.assertNotIn('Voyager Cross [2004]', problem['candidates'])
        self.request('POST', f'/api/scraper/resolve?id={job_id}&item={problem["item"]}&action=choose&value='
                     + urllib.parse.quote('Time Voyager: Hover Race Special [2005]'))
        self.wait(job_id, lambda j: j['state'] == 'done' and not j['counts'].get('pending') and not j['counts'].get('working'))
        self.assertEqual(self.stored('Time Voyager Special Edition', 'covers').read_bytes(), picture('jetbike.png'))
        unknown = next(p for p in self.job(job_id)['problems'] if p['label'] == 'Totally Unknown Homebrew')
        status, body = self.request('POST', f'/api/scraper/resolve?id={job_id}&item={unknown["item"]}&action=search&value=hunter')
        found = next(p for p in json.loads(body)['job']['problems'] if p['item'] == unknown['item'])
        self.assertEqual(found['candidates'], ['Hunter III [2002]'])
        # A second job: nothing fetched again, the index read from the console.
        # (Jungle Run has all it was asked for; the others lack kinds LaunchBox has not.)
        self.stop_restart()
        images = sorted(self.fetched('/images/'))
        again = self.wait(self.start(lines=('snes\t',)))
        self.assertEqual(again['counts'].get('skipped'), 1)
        self.assertEqual(len(self.fetched('/Metadata.zip')), 1)
        self.assertEqual(sorted(self.fetched('/images/')), images)  # no stored image fetched again

    def stop_restart(self):
        self.server.terminate(); self.server.wait(timeout=10)
        self.start_server()

    def test_pc_mode_through_the_helper(self):
        # The helper answers every question the source is asked, the database with
        # them: a console with no route to the internet runs this job unchanged.
        job_id = self.start(mode='pc', kinds='cover,logo', details=False)
        helper = subprocess.run([sys.executable, str(ROOT / 'webui/ps5-media-helper.py'), f'http://127.0.0.1:{self.port}', job_id],
                                capture_output=True, text=True, timeout=120)
        self.assertEqual(helper.returncode, 0, helper.stdout + helper.stderr)
        job = self.wait(job_id)
        self.assertEqual(job['transferred']['files'], 7)  # 4 covers, 3 logos
        self.assertEqual(self.stored('runner3', 'marquees').read_bytes(), picture('runner3-logo.png'))
        self.assertEqual(len(self.fetched('/Metadata.zip')), 1)  # the database came through the PC too

    def test_a_chain_asks_launchbox_only_for_what_libretro_lacks(self):
        job_id = self.start(sources='libretro,launchbox', kinds='cover,logo', details=False, lines=('snes\t',))
        self.wait(job_id)
        covers = self.fetched('/images/')
        self.assertNotIn('/images/dk-us.png', covers)  # libretro had DK's box art
        self.assertIn('/images/dk-logo.png', covers)  # libretro has no logo for it
        self.assertEqual(self.stored("Jungle Run 2 - Partner's Island Quest (USA)", 'covers').read_bytes(),
                         base.image("Jungle Run 2 - Partner's Island Quest (USA)", 'Named_Boxarts'))

    def test_a_failed_database_download_pauses_until_resumed(self):
        with self.source.lock:
            self.source.broken = True
        job_id = self.start(lines=('snes\t',))
        job = self.wait(job_id, lambda j: j['state'] == 'paused')
        self.assertIn('LaunchBox answered 500', job['message'])
        with self.source.lock:
            self.source.broken = False
        self.assertEqual(self.request('POST', f'/api/scraper/resume?id={job_id}')[0], 200)
        job = self.wait(job_id)
        self.assertEqual(job['counts'], {'ambiguous': 1, 'done': 1, 'partial': 2, 'unmatched': 1}, job)


if __name__ == '__main__':
    unittest.main()
