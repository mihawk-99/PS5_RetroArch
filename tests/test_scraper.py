"""The scraper (src/scraper.h) end to end on the host: the WebUI's server against a fake
libretro thumbnail server, in both download modes. A match, an ambiguous game resolved
by the user, an unmatched one, cached browsing that fetches nothing again, overwrite,
cancel and resume without repeating work, a server restart mid-job, and PC mode
through the real helper (webui/ps5-media-helper.py) — including a helper killed mid-job
and a console that reaches nothing but its LAN — and the shared store layout.
The shared store's layout is checked as RetroArch's lookup (src/ps5_library.c) and
EmulationStation read it."""
from pathlib import Path
import http.client
import http.server
import json
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.parse

sys.path.insert(0, str(Path(__file__).resolve().parent))
import webui_build  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DB = 'Nintendo - Super Nintendo Entertainment System'
FOLDERS = {'Named_Boxarts': 'cover', 'Named_Snaps': 'screenshot', 'Named_Titles': 'title'}
STORE = {'cover': 'covers', 'screenshot': 'screenshots', 'title': 'titlescreens'}
# The fake source: names it has, and which of the three images each has.
SOURCE = {
    'Jungle Run 2 - Partner\'s Island Quest (USA)': ('Named_Boxarts', 'Named_Snaps', 'Named_Titles'),
    'Space Hunter (Japan, USA) (En,Ja)': ('Named_Boxarts', 'Named_Snaps'),
    'Time Voyager (USA)': ('Named_Boxarts', 'Named_Snaps', 'Named_Titles'),
    'Time Voyager (Japan)': ('Named_Boxarts',),
}
SOURCE.update({f'Filler Game {i:02} (USA)': ('Named_Boxarts', 'Named_Snaps', 'Named_Titles') for i in range(24)})


def image(name, folder):
    return f'PNG {folder}/{name}'.encode() * 50


class FakeLibretro(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self):
        super().__init__(('127.0.0.1', 0), Handler)
        self.log, self.delay, self.lock = [], 0.0, threading.Lock()
        self.busy = self.most_busy = 0  # requests at once (how parallel a job is)
        self.times = []  # when each request came (the order of a funnel's passes)


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def answer(self, body):
        parts = urllib.parse.unquote(self.path).strip('/').split('/')
        with self.server.lock:
            self.server.log.append((self.command, self.headers.get('User-Agent', ''), '/'.join(parts[1:])))
            self.server.times.append(time.time())
            self.server.busy += 1
            self.server.most_busy = max(self.server.most_busy, self.server.busy)
        try:
            self.reply(parts, body)
        finally:
            with self.server.lock:
                self.server.busy -= 1

    def reply(self, parts, body):
        time.sleep(self.server.delay)
        if len(parts) == 2 and parts[0] == DB and parts[1] in FOLDERS:  # the folder's listing
            data = ''.join(f'<a href="{urllib.parse.quote(n)}.png">{n}.png</a>\n' for n, f in SOURCE.items() if parts[1] in f).encode()
        elif len(parts) == 3 and parts[0] == DB and parts[2].endswith('.png') and parts[2][:-4] in SOURCE \
                and parts[1] in SOURCE[parts[2][:-4]]:
            data = image(parts[2][:-4], parts[1])
        else:
            self.send_response(404); self.send_header('Content-Length', '0'); self.end_headers(); return
        self.send_response(200); self.send_header('Content-Length', str(len(data))); self.end_headers()
        if body:
            self.wfile.write(data)

    def do_GET(self):
        self.answer(True)

    def do_HEAD(self):
        self.answer(False)


class Scraper(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = webui_build.build(Path(cls.temp.name) / 'server', 'tests/webui_server_main.cpp')
        cls.source = FakeLibretro()
        threading.Thread(target=cls.source.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.source.shutdown()
        cls.temp.cleanup()

    def setUp(self):
        self.root = Path(tempfile.mkdtemp(dir=self.temp.name))
        for folder in ('config', 'content', 'webui', 'cores', 'info', 'playlists'):
            (self.root / folder).mkdir()
        (self.root / 'webui/index.html').write_text('<!doctype html><title>RetroArch</title>')
        (self.root / 'cores/snes9x_libretro.so').write_bytes(b'')
        (self.root / 'info/snes9x_libretro.info').write_text(
            'display_name = "Snes9x"\ncorename = "Snes9x"\ndatabase = "' + DB + '"\nsupported_extensions = "sfc|smc|zip"\n')
        self.games = {  # label -> path
            'Jungle Run 2 - Partner\'s Island Quest (USA)': '/app0/content/SNES/Jungle Run 2 - Partner\'s Island Quest (USA).zip',
            'Space Hunter (Japan, USA) (En,Ja)': '/app0/content/SNES/Space Hunter (Japan, USA) (En,Ja).sfc',
            'Time Voyager (1995)': '/app0/content/SNES/Time Voyager (1995).sfc',  # same title: the USA one, unasked
            'Time Voyager Special Edition': '/app0/content/SNES/Time Voyager SE.sfc',  # another title: ambiguous
            'Totally Unknown Homebrew': '/app0/content/SNES/Totally Unknown Homebrew.sfc',  # unmatched
        }
        self.write_playlist(self.games)
        self.source.delay = 0.0
        with self.source.lock:
            self.source.log.clear()
            self.source.times.clear()
        self.start_server()

    def write_playlist(self, games):
        items = [{'path': p, 'label': l, 'core_path': 'DETECT', 'core_name': 'DETECT', 'crc32': '', 'db_name': DB + '.lpl'}
                 for l, p in games.items()]
        (self.root / f'playlists/{DB}.lpl').write_text(json.dumps({'version': '1.5', 'items': items}))

    def start_server(self, lease=None, extra=None):
        # The developer's own proxy has no say here: a test that wants to make the
        # console networkless says so with extra (the helper is always given none).
        env = {k: v for k, v in os.environ.items()
               if k.lower() not in ('http_proxy', 'https_proxy', 'no_proxy')}
        env['PS5_SCRAPER_LIBRETRO_BASE'] = f'http://127.0.0.1:{self.source.server_port}'
        env.update(extra or {})
        if lease:
            env['PS5_SCRAPER_LEASE_SECONDS'] = str(lease)
        self.server, self.port, self.token = webui_build.start(self.binary, self.root, env)

    def stop_server(self):
        self.server.terminate(); self.server.wait(timeout=10)

    def tearDown(self):
        if self.server.poll() is None:
            self.stop_server()

    def request(self, method, path, body=None):
        conn = http.client.HTTPConnection('127.0.0.1', self.port, timeout=60)
        conn.request(method, path, body, {'X-RetroArch-Token': getattr(self, 'token', '')})
        response = conn.getresponse()
        data = response.read()
        conn.close()
        return response.status, data

    def start(self, mode='ps5', kinds='cover,screenshot,title', overwrite=False, lines=None):
        query = f'mode={mode}&source=libretro&kinds={kinds}&region=us&overwrite={int(overwrite)}'
        status, body = self.request('POST', f'/api/scraper/start?{query}', '\n'.join(lines or [f'snes\t']).encode())
        self.assertEqual(status, 201, body)
        return json.loads(body)['job']['id']

    def job(self, job_id=''):
        return json.loads(self.request('GET', f'/api/scraper/job?id={job_id}')[1])['job']

    def wait(self, job_id, until=lambda j: j['state'] != 'running', timeout=60):
        end = time.time() + timeout
        while time.time() < end:
            job = self.job(job_id)
            if until(job):
                return job
            time.sleep(0.1)
        self.fail(f'the job did not settle: {self.job(job_id)}')

    def media_gets(self, agent='Scraper'):
        with self.source.lock:
            return [p for m, a, p in self.source.log if m == 'GET' and agent in a and p.endswith('.png')]

    def stored(self, label, kind):
        key = Path(self.games[label]).stem
        return self.root / 'library/snes' / STORE[kind] / f'{key}.png'

    def test_ps5_mode_match_ambiguous_unmatched_and_cache(self):
        job_id = self.start()
        job = self.wait(job_id)
        self.assertEqual(job['state'], 'done')
        self.assertEqual(job['counts'], {'ambiguous': 1, 'done': 2, 'partial': 1, 'unmatched': 1})
        self.assertEqual(self.stored('Time Voyager (1995)', 'cover').read_bytes(), image('Time Voyager (USA)', 'Named_Boxarts'))
        dk = 'Jungle Run 2 - Partner\'s Island Quest (USA)'
        for kind, folder in (('cover', 'Named_Boxarts'), ('screenshot', 'Named_Snaps'), ('title', 'Named_Titles')):
            self.assertEqual(self.stored(dk, kind).read_bytes(), image(dk, folder))
        self.assertFalse(self.stored('Space Hunter (Japan, USA) (En,Ja)', 'title').exists())  # the source has none
        meta = (self.root / 'library/snes/metadata' / (Path(self.games[dk]).stem + '.meta')).read_text()
        self.assertIn('name = "Jungle Run 2 - Partner\'s Island Quest"', meta)
        self.assertIn('media.cover = "snes/covers/', meta)
        ambiguous = next(p for p in job['problems'] if p['state'] == 'ambiguous')
        self.assertEqual(ambiguous['label'], 'Time Voyager Special Edition')
        self.assertEqual(ambiguous['candidates'][0], 'Time Voyager (USA)')  # the region asked for first
        self.assertIn('Time Voyager (Japan)', ambiguous['candidates'])
        # A restart keeps what is offered: the server reads the job back with its candidates.
        self.stop_server()
        self.start_server()
        again = next(p for p in self.job(job_id)['problems'] if p['state'] == 'ambiguous')
        self.assertEqual(again['candidates'], ambiguous['candidates'])
        self.assertEqual(self.job(job_id)['downloaded'], job['downloaded'])  # counters kept too
        unmatched = next(p for p in job['problems'] if p['state'] == 'unmatched')
        self.assertEqual(unmatched['label'], 'Totally Unknown Homebrew')
        # The user chooses: the game's media arrives under its own name.
        status, body = self.request('POST', f'/api/scraper/resolve?id={job_id}&item={ambiguous["item"]}&action=choose&value='
                                    + urllib.parse.quote('Time Voyager (USA)'))
        self.assertEqual(status, 200, body)
        self.wait(job_id, lambda j: j['state'] == 'done' and not j['counts'].get('pending') and not j['counts'].get('working'))
        self.assertEqual(self.stored('Time Voyager Special Edition', 'cover').read_bytes(), image('Time Voyager (USA)', 'Named_Boxarts'))
        # A manual search, then skip.
        status, body = self.request('POST', f'/api/scraper/resolve?id={job_id}&item={unmatched["item"]}&action=search&value=hunter')
        self.assertEqual(status, 200)
        found = next(p for p in json.loads(body)['job']['problems'] if p['item'] == unmatched['item'])
        self.assertEqual(found['candidates'], ['Space Hunter (Japan, USA) (En,Ja)'])
        self.request('POST', f'/api/scraper/resolve?id={job_id}&item={unmatched["item"]}&action=skip')
        self.assertEqual(self.job(job_id)['counts'].get('skipped'), 1)
        # The library shows what is stored; a new job fetches nothing already there.
        library = json.loads(self.request('GET', '/api/library')[1])
        dk_entry = next(g for g in library['systems'][0]['games'] if g['label'] == dk)
        self.assertEqual(sorted(dk_entry['media']), ['cover', 'screenshot', 'title'])
        status, data = self.request('GET', f'/api/library/media?system=snes&game={urllib.parse.quote(dk_entry["key"])}&kind=cover')
        self.assertEqual((status, data), (200, image(dk, 'Named_Boxarts')))
        self.assertEqual(self.request('GET', '/api/library/media?system=snes&game=..%2F..%2Fconfig&kind=cover')[0], 404)
        before = len(self.media_gets())
        job2 = self.wait(self.start(lines=[f'snes\t{self.games[dk]}']))
        self.assertEqual(job2['counts'], {'skipped': 1})
        self.assertEqual(len(self.media_gets()), before)
        # Overwrite: fetched again.
        self.wait(self.start(lines=[f'snes\t{self.games[dk]}'], overwrite=True))
        self.assertEqual(len(self.media_gets()), before + 3)
        self.assertFalse(list((self.root / 'library').rglob('.partial-*')))

    def test_the_server_reports_the_page_build_it_started_with(self):
        self.stop_server()
        (self.root / 'webui/version.json').write_text('{"release": "", "build": "abc123", "repository": "x"}\n')
        self.start_server()
        self.assertEqual(json.loads(self.request('GET', '/api/status')[1])['build'], 'abc123')
        # Files replaced while it runs (a deploy, an update): it keeps the build it started with.
        (self.root / 'webui/version.json').write_text('{"build": "def456"}\n')
        self.assertEqual(json.loads(self.request('GET', '/api/status')[1])['build'], 'abc123')

    def test_libretro_has_no_details(self):
        status, body = self.request('POST', '/api/scraper/start?mode=ps5&source=libretro&kinds=&details=1', b'snes\t')
        self.assertEqual((status, json.loads(body)['error']), (409, 'libretro has no game details: choose ScreenScraper for them.'))
        settings = json.loads(self.request('GET', '/api/scraper/settings')[1])
        self.assertEqual({s['id']: s['details'] for s in settings['sources']}['libretro'], False)

    def test_cancel_resume_and_restart_do_not_repeat_work(self):
        games = {f'Filler Game {i:02} (USA)': f'/app0/content/SNES/Filler Game {i:02} (USA).sfc' for i in range(24)}
        self.games = games
        self.write_playlist(games)
        self.source.delay = 0.4  # 32 workers take every game at once: slow enough to cut
        job_id = self.start(kinds='cover,screenshot,title')
        self.wait(job_id, lambda j: j['downloaded']['files'] >= 4)
        self.assertEqual(json.loads(self.request('POST', f'/api/scraper/cancel?id={job_id}')[1]), {'cancelled': True})
        cancelled = self.wait(job_id, lambda j: j['state'] == 'cancelled')
        done_then = cancelled['counts'].get('done', 0)
        # A cut download leaves nothing (curl notices a cancel within a second).
        end = time.time() + 3
        while list((self.root / 'library').rglob('.partial-*')) and time.time() < end:
            time.sleep(0.1)
        self.assertFalse(list((self.root / 'library').rglob('.partial-*')))
        # Resume, then restart the server mid-way: it goes on by itself.
        self.assertEqual(self.request('POST', f'/api/scraper/resume?id={job_id}')[0], 200)
        self.wait(job_id, lambda j: j['downloaded']['files'] >= cancelled['downloaded']['files'] + 4)
        self.stop_server()
        self.start_server()
        job = self.wait(job_id, timeout=120)
        self.assertEqual((job['state'], job['counts']), ('done', {'done': 24}))
        # Every image fetched once, but those a cancel or a restart cut (at most one a worker,
        # 32 of them, each time).
        gets = self.media_gets()
        repeats = len(gets) - len(set(gets))
        self.assertEqual(len(set(gets)), 72)
        self.assertLessEqual(repeats, 64)
        for label in games:
            for kind in ('cover', 'screenshot', 'title'):
                self.assertTrue(self.stored(label, kind).exists())

    def run_helper(self, job_id, timeout=120):
        return subprocess.Popen([sys.executable, str(ROOT / 'webui/ps5-media-helper.py'), f'http://127.0.0.1:{self.port}', job_id],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                env={k: v for k, v in os.environ.items()
                                     if k.lower() not in ('http_proxy', 'https_proxy', 'no_proxy')})

    def test_pc_mode_bytes_pass_through_the_helper(self):
        job_id = self.start(mode='pc')
        helper = self.run_helper(job_id)
        out, _ = helper.communicate(timeout=120)
        self.assertEqual(helper.returncode, 0, out)
        job = self.wait(job_id)
        self.assertEqual(job['counts'], {'ambiguous': 1, 'done': 2, 'partial': 1, 'unmatched': 1})
        self.assertEqual((job['downloaded']['files'], job['transferred']['files']), (8, 8))
        dk = 'Jungle Run 2 - Partner\'s Island Quest (USA)'
        self.assertEqual(self.stored(dk, 'title').read_bytes(), image(dk, 'Named_Titles'))
        # The console asked the source nothing at all: the probes, the folder's listing
        # and every byte came through the helper. (A console without any route to the
        # internet runs the very same job: test_pc_mode_a_console_without_internet.)
        with self.source.lock:
            rows = list(self.source.log)
        self.assertEqual([m for m, a, _ in rows if 'Scraper' in a], [])
        self.assertTrue(any(m == 'HEAD' and 'Helper' in a for m, a, _ in rows))
        self.assertEqual(len(self.media_gets('Helper')), 8)
        self.assertFalse(list((self.root / 'library').rglob('.partial-*')))

    def test_pc_mode_a_console_without_internet(self):
        # The reported case (2026-11-05): the PS5 reaches only its LAN. Every question
        # PC mode asks the source now goes through the helper, so the job runs exactly
        # as it does with an online console; the probe used to answer "Could not
        # connect to server (curl 7)." for every game and the helper saw no task.
        self.stop_server()
        with socket.socket() as dead:  # a port nothing listens on (the LAN's internet is gone)
            dead.bind(('127.0.0.1', 0))
            dead_port = dead.getsockname()[1]
        proxy = f'http://127.0.0.1:{dead_port}'
        self.start_server(extra={'http_proxy': proxy, 'https_proxy': proxy})
        job_id = self.start(mode='pc')
        helper = self.run_helper(job_id)
        out, _ = helper.communicate(timeout=120)
        self.assertEqual(helper.returncode, 0, out)
        job = self.wait(job_id)
        self.assertEqual(job['counts'], {'ambiguous': 1, 'done': 2, 'partial': 1, 'unmatched': 1})
        dk = 'Jungle Run 2 - Partner\'s Island Quest (USA)'
        self.assertEqual(self.stored(dk, 'cover').read_bytes(), image(dk, 'Named_Boxarts'))
        self.assertEqual(self.stored('Time Voyager (1995)', 'title').read_bytes(),
                         image('Time Voyager (USA)', 'Named_Titles'))  # the listing, asked through the PC
        with self.source.lock:
            rows = list(self.source.log)
        self.assertEqual([a for _, a, _ in rows if 'Helper' not in a], [])

    def test_pc_mode_a_matched_game_with_nothing_left_to_fetch(self):
        # (console, 2026-11-06) A game whose media is all stored but one kind the
        # source has never carried: once the user has chosen its name, the probes
        # answer nothing, the hand-off has no tasks, and the item must finish
        # rather than wait "for the PC helper" forever behind an empty queue.
        tv = 'Time Voyager Special Edition'
        job_id = self.start(mode='pc', kinds='logo', lines=[f'snes\t{self.games[tv]}'])
        helper = self.run_helper(job_id)
        out, _ = helper.communicate(timeout=120)
        self.assertEqual(helper.returncode, 0, out)
        job = self.wait(job_id)
        ambiguous = next(p for p in job['problems'] if p['state'] == 'ambiguous')
        status, body = self.request('POST', f'/api/scraper/resolve?id={job_id}&item={ambiguous["item"]}&action=choose&value='
                                    + urllib.parse.quote('Time Voyager (USA)'))
        self.assertEqual(status, 200, body)
        helper = self.run_helper(job_id)  # the probe that answers "no logo under this name"
        out, _ = helper.communicate(timeout=120)
        self.assertEqual(helper.returncode, 0, out)
        job = self.wait(job_id, lambda j: j['state'] == 'done' and not j['counts'].get('transferring'), timeout=30)
        self.assertEqual(job['counts'].get('done'), 1)

    def test_pc_mode_helper_killed_then_resumed(self):
        games = {f'Filler Game {i:02} (USA)': f'/app0/content/SNES/Filler Game {i:02} (USA).sfc' for i in range(24)}
        self.games = games
        self.write_playlist(games)
        self.stop_server()
        self.start_server(lease=60)
        self.source.delay = 0.05
        job_id = self.start(mode='pc')
        helper = self.run_helper(job_id)
        self.wait(job_id, lambda j: j['transferred']['files'] >= 10)
        helper.kill(); helper.wait()
        sent = self.job(job_id)['transferred']['files']
        self.assertLess(sent, 72)
        # A cut upload's hidden file goes as the console notices the dropped connection.
        partials = lambda: list((self.root / 'library').rglob('.partial-*'))
        end = time.time() + 5
        while partials() and time.time() < end:
            time.sleep(0.1)
        self.assertFalse(partials())
        resumed = time.time()
        helper = self.run_helper(job_id)
        out, _ = helper.communicate(timeout=180)
        self.assertLess(time.time() - resumed, 30)  # the killed helper's leases at once, not after theirs
        self.assertEqual(helper.returncode, 0, out)
        job = self.wait(job_id)
        self.assertEqual((job['counts'], job['transferred']['files']), ({'done': 24}, 72))
        # Nothing stored was sent twice: each file was stored once.
        for label in games:
            for kind, folder in (('cover', 'Named_Boxarts'), ('screenshot', 'Named_Snaps'), ('title', 'Named_Titles')):
                self.assertEqual(self.stored(label, kind).read_bytes(), image(label, folder))

    def test_shared_layout_is_what_frontends_read(self):
        self.wait(self.start(lines=[f'snes\t{self.games["Jungle Run 2 - Partner\'s Island Quest (USA)"]}']))
        # The settings are remembered on the console.
        self.request('POST', '/api/scraper/settings?mode=pc&source=libretro&kinds=cover,title&region=eu&language=en')
        settings = json.loads(self.request('GET', '/api/scraper/settings')[1])
        self.assertEqual((settings['mode'], settings['kinds'], settings['region']), ('pc', ['cover', 'title'], 'eu'))
        # RetroArch's lookup and EmulationStation's folder names, as src/ps5_library.c reads them.
        probe = Path(self.temp.name) / 'media-probe'
        source = probe.with_suffix('.c')
        source.write_text('#include "ps5_library.h"\n#include <stdio.h>\nint main(int c, char **v)\n'
                          '{ char out[1024]; int found = ps5_library_media(v[1], v[2], v[3], v[4], out, sizeof out);'
                          ' printf("%d %s\\n", found, out); return 0; }\n')
        subprocess.run(['cc', '-std=c11', '-D_DEFAULT_SOURCE', '-Isrc', str(source), 'src/ps5_library.c', '-o', str(probe)], cwd=ROOT, check=True)
        path = self.games["Jungle Run 2 - Partner's Island Quest (USA)"]
        for system, folder, store in ((DB, 'Named_Boxarts', 'covers'), (DB + '.lpl', 'Named_Snaps', 'screenshots'),
                                      ('Nintendo - Super Nintendo Entertainment System', 'titlescreens', 'titlescreens')):
            out = subprocess.run([str(probe), str(self.root / 'library'), system, path, folder], capture_output=True, text=True).stdout
            self.assertEqual(out.strip(), f'1 {self.root}/library/snes/{store}/{Path(path).stem}.png')
        out = subprocess.run([str(probe), str(self.root / 'library'), DB, '/app0/content/SNES/missing.sfc', 'Named_Boxarts'],
                             capture_output=True, text=True).stdout
        self.assertEqual(out.strip(), '0')

    def test_a_game_in_a_playlist_and_the_content_folder_is_one_game(self):
        # The daemon reads the title from its real folder; the playlists name /app0. A game
        # in both (scanned, and listed) was listed twice, with its media shared by both cards.
        folder = self.root / 'content' / DB
        folder.mkdir(parents=True)
        listed = 'Jungle Run 2 - Partner\'s Island Quest (USA).zip'
        self.games = {listed[:-4]: f'/app0/content/{DB}/{listed}',
                      # RetroArch's scan lists a disc's track beside its index: one game.
                      'Disc (USA) track': f'/app0/content/{DB}/Disc (USA).bin',
                      'Disc (USA)': f'/app0/content/{DB}/Disc (USA).cue',
                      'Lone Image (USA)': f'/app0/content/{DB}/Lone Image (USA).iso'}
        self.write_playlist(self.games)
        (folder / listed).write_bytes(b'rom')
        (folder / 'Only In The Folder (USA).sfc').write_bytes(b'rom')
        library = json.loads(self.request('GET', '/api/library')[1])
        paths = sorted(g['path'] for s in library['systems'] for g in s['games'])
        self.assertEqual(paths, sorted([f'/app0/content/{DB}/{listed}', f'/app0/content/{DB}/Only In The Folder (USA).sfc',
                                        f'/app0/content/{DB}/Disc (USA).cue', f'/app0/content/{DB}/Lone Image (USA).iso']))

    def test_a_dead_writers_hidden_files_are_swept(self):
        covers = self.root / 'library/snes/covers'
        covers.mkdir(parents=True)
        old, young = covers / '.partial-dead', covers / '.partial-busy'
        old.write_bytes(b''); young.write_bytes(b'')
        os.utime(old, (time.time() - 3600, time.time() - 3600))
        self.stop_server()
        self.start_server()
        self.assertFalse(old.exists())
        self.assertTrue(young.exists())  # maybe a stale daemon's file still being written

    def test_a_game_shows_its_media_and_takes_the_users_own(self):
        dk = 'Jungle Run 2 - Partner\'s Island Quest (USA)'
        key = Path(self.games[dk]).stem
        self.wait(self.start(lines=[f'snes\t{self.games[dk]}']))
        status, body = self.request('GET', f'/api/library/game?system=snes&game={urllib.parse.quote(key)}')
        self.assertEqual(status, 200, body)
        game = json.loads(body)
        kinds = {k['id']: k for k in game['kinds']}
        self.assertEqual(len(kinds), 10)  # every kind, present or not
        self.assertEqual((kinds['cover']['present'], kinds['cover']['type'], kinds['cover']['uploaded']), (True, 'png', False))
        self.assertEqual((kinds['fanart']['present'], kinds['video']['accepts']), (False, ['mp4', 'webm']))
        self.assertEqual(game['details']['name'], "Jungle Run 2 - Partner's Island Quest")
        query = f'system=snes&game={urllib.parse.quote(key)}'
        mine = b'\xff\xd8\xff' + b'my own cover' * 1000
        # A type no frontend shows, a game not in the library, a kind that does not exist,
        # an address outside the library, and no token: all refused, nothing written.
        for path, expected in ((f'/api/library/media?{query}&kind=cover&type=gif', 415),
                               ('/api/library/media?system=snes&game=Nope&kind=cover&type=png', 415),
                               (f'/api/library/media?{query}&kind=config&type=png', 415),
                               ('/api/library/media?system=..&game=..%2Fconfig&kind=cover&type=png', 415)):
            status, body = self.request('PUT', path, mine)
            self.assertEqual(status, expected, (path, body))
        self.assertIn('takes png, jpg, jpeg files', json.loads(self.request('PUT', f'/api/library/media?{query}&kind=cover&type=gif', mine)[1])['error'])
        token, self.token = self.token, ''
        self.assertEqual(self.request('PUT', f'/api/library/media?{query}&kind=cover&type=jpg', mine)[0], 403)
        self.token = token
        self.assertTrue(self.stored(dk, 'cover').exists())
        # The user's cover: it replaces the scraped one, the only file of its kind.
        status, body = self.request('PUT', f'/api/library/media?{query}&kind=cover&type=JPG', mine)
        self.assertEqual(status, 201, body)
        cover = {k['id']: k for k in json.loads(body)['kinds']}['cover']
        self.assertEqual((cover['type'], cover['bytes'], cover['uploaded']), ('jpg', len(mine), True))
        self.assertFalse(self.stored(dk, 'cover').exists())  # the .png
        self.assertEqual(self.request('GET', f'/api/library/media?{query}&kind=cover')[1], mine)
        meta = (self.root / 'library/snes/metadata' / f'{key}.meta').read_text()
        self.assertIn('uploaded = "cover"', meta)
        self.assertIn('media.cover = "snes/covers/', meta)
        # A missing kind added; a scrape replacing everything leaves the user's files.
        fanart = b'\x89PNG' + b'fan art' * 500
        self.assertEqual(self.request('PUT', f'/api/library/media?{query}&kind=fanart&type=png', fanart)[0], 201)
        before = len(self.media_gets())
        self.wait(self.start(lines=[f'snes\t{self.games[dk]}'], overwrite=True))
        self.assertEqual(len(self.media_gets()), before + 2)  # screenshot and title, not the cover
        self.assertEqual(self.request('GET', f'/api/library/media?{query}&kind=cover')[1], mine)
        self.assertEqual((self.root / 'library/snes/fanarts' / f'{key}.png').read_bytes() if (self.root / 'library/snes/fanarts').exists()
                         else self.request('GET', f'/api/library/media?{query}&kind=fanart')[1], fanart)
        self.assertFalse(list((self.root / 'library').rglob('.partial-*')))

    def test_library_sort_details(self):
        folder = self.root / 'library/snes/metadata'
        folder.mkdir(parents=True)
        key = "Jungle Run 2 - Partner's Island Quest (USA)"
        fields = {'released': '1995-11-20', 'genre': 'Platform', 'developer': 'Rare',
                  'publisher': 'Nintendo', 'rating': '0.90'}
        (folder / f'{key}.meta').write_text(''.join(f'{key} = "{value}"\n' for key, value in fields.items()))
        games = json.loads(self.request('GET', '/api/library')[1])['systems'][0]['games']
        game = next(game for game in games if game['key'] == key)
        self.assertEqual({field: game[field] for field in fields}, fields)
        self.assertTrue(any('released' not in game for game in games))

    def test_add_game_and_savestate_preview(self):
        options = json.loads(self.request('GET', '/api/library/add-options')[1])
        self.assertIn('snes', [s['id'] for s in options['systems']])
        route = '/api/library/add?' + urllib.parse.urlencode({'system': 'snes', 'filename': 'New Homebrew.sfc'})
        status, body = self.request('PUT', route, b'synthetic backup')
        self.assertEqual(status, 201, body)
        game = json.loads(body)
        query = urllib.parse.urlencode({'system': 'snes', 'game': game['key'], 'path': game['path']})
        self.assertEqual(self.request('PUT', route, b'overwrite')[0], 400)
        for name in ['../escape.sfc', 'invalid.exe', '.hidden.sfc']:
            self.assertEqual(self.request('PUT', '/api/library/add?' + urllib.parse.urlencode({'system': 'snes', 'filename': name}), b'x')[0], 400)
        self.assertEqual(self.request('GET', '/api/library/file?' + query + '&kind=rom')[1], b'synthetic backup')
        self.assertEqual(self.request('POST', '/api/library/game?' + query, json.dumps({'partial': 'true', 'name': 'My Homebrew', 'launchbox_id': '42'}).encode())[0], 200)
        self.assertEqual(self.request('POST', '/api/library/game?' + query, json.dumps({'partial': 'true', 'developer': 'Homebrew author'}).encode())[0], 200)
        meta = (self.root / 'library/snes/metadata/New Homebrew.meta').read_text()
        self.assertIn('edited = "name,developer"', meta)
        self.assertNotIn('description =', meta)
        folder = self.root / 'savestates/Snes9x'
        folder.mkdir(parents=True)
        state = folder / 'New Homebrew.state3'; state.write_bytes(b'state')
        screenshot = folder / 'New Homebrew.state3.png'; screenshot.write_bytes(b'PNG fixture')
        files = json.loads(self.request('GET', '/api/library/files?' + query)[1])
        self.assertTrue(files['state'][0]['preview'])
        export = '/api/library/file?' + query + '&' + urllib.parse.urlencode({'kind': 'state', 'file': str(state)})
        self.assertEqual(self.request('GET', export + '&preview=1')[1], b'PNG fixture')
        self.assertEqual(self.request('PUT', export + '&existing=replace', b'new state')[0], 201)
        self.assertFalse(screenshot.exists(), 'An imported state must not keep the old screenshot')
        self.assertEqual(self.request('GET', export + '&preview=1')[0], 404)
        screenshot.symlink_to(state)
        self.assertFalse(json.loads(self.request('GET', '/api/library/files?' + query)[1])['state'][0]['preview'])
        self.assertEqual(self.request('GET', export + '&preview=1')[0], 404)

    def test_game_files_and_edited_metadata(self):
        path = next(iter(self.games.values()))
        rom = self.root / path.removeprefix('/app0/')
        rom.parent.mkdir(parents=True, exist_ok=True)
        rom.write_bytes(b'original backup')
        key = rom.stem
        query = urllib.parse.urlencode({'system': 'snes', 'game': key, 'path': path})
        route = '/api/library/file?' + query
        files_route = '/api/library/files?' + query
        (self.root / 'config/retroarch.cfg').write_text(
            'savefile_directory = ":/savefiles"\nsavestate_directory = ":/savestates"\n')
        status, body = self.request('GET', files_route)
        self.assertEqual(status, 200, body)
        files = json.loads(body)
        self.assertEqual(files['core'], 'Snes9x')
        self.assertEqual(files['saveFolder'], str(self.root / 'savefiles/Snes9x'))
        self.assertEqual(self.request('GET', route + '&kind=rom')[1], b'original backup')
        self.assertEqual(self.request('PUT', route + '&kind=rom', b'replacement')[0], 409)
        self.assertEqual(self.request('PUT', route + '&kind=rom&existing=replace', b'replacement')[0], 201)
        self.assertEqual(rom.read_bytes(), b'replacement')
        for kind, filename, folder in [('save', key + '.srm', 'savefiles'), ('state', key + '.state3', 'savestates')]:
            target = route + '&kind=' + kind + '&file=' + urllib.parse.quote(filename)
            self.assertEqual(self.request('PUT', target, b'new progress')[0], 201)
            stored = self.root / folder / 'Snes9x' / filename
            self.assertEqual(stored.read_bytes(), b'new progress')
            listing = json.loads(self.request('GET', files_route)[1])[kind]
            self.assertEqual([f['path'] for f in listing], [str(stored)])
            target = route + '&kind=' + kind + '&file=' + urllib.parse.quote(str(stored))
            self.assertEqual(self.request('GET', target)[1], b'new progress')
            self.assertEqual(self.request('PUT', target, b'bad')[0], 409)
            self.assertEqual(self.request('PUT', target + '&existing=replace', b'newer')[0], 201)
            self.assertEqual(stored.read_bytes(), b'newer')
            # A disconnected upload must not replace a whole save.
            conn = socket.create_connection(('127.0.0.1', self.port))
            conn.sendall((f'PUT {target}&existing=replace HTTP/1.1\r\nHost: 127.0.0.1:{self.port}\r\nX-RetroArch-Token: {self.token}\r\nContent-Length: 100\r\n\r\ncut').encode())
            conn.close()
            self.assertEqual(stored.read_bytes(), b'newer')
        for bad in ['../config/retroarch.cfg', key + '.state.png', key + '.state-2', 'other.state']:
            self.assertEqual(self.request('PUT', route + '&kind=state&file=' + urllib.parse.quote(bad), b'bad')[0], 400)
        self.assertEqual(self.request('PUT', route + '&kind=rom&existing=replace', b'')[0], 413)
        self.assertEqual(self.request('GET', files_route.replace('path=', 'invalid='))[0], 404)
        self.token, token = 'wrong', self.token
        self.assertEqual(self.request('PUT', route + '&kind=rom&existing=replace', b'bad')[0], 403)
        self.token = token
        # Links never expose or overwrite a different file.
        save = self.root / 'savefiles/Snes9x' / (key + '.sav')
        save.symlink_to(rom)
        self.assertEqual(self.request('PUT', route + '&kind=save&file=' + urllib.parse.quote(save.name), b'bad')[0], 400)
        self.assertEqual(rom.read_bytes(), b'replacement')
        # The same basename in another core remains a separate selectable file.
        alternate = self.root / 'savefiles/Other Core' / (key + '.srm')
        alternate.parent.mkdir(); alternate.write_bytes(b'other core')
        self.assertEqual(len(json.loads(self.request('GET', files_route)[1])['save']), 2)
        cfg = self.root / 'config/retroarch.cfg'
        cfg.write_text('sort_savestates_enable = "false"\nsort_savestates_by_content_enable = "true"\n')
        self.assertEqual(json.loads(self.request('GET', files_route)[1])['stateFolder'], str(self.root / 'savestates/SNES'))
        metadata = {field: '' for field in ('name', 'description', 'developer', 'publisher', 'released', 'genre', 'players', 'rating')}
        metadata.update(name='My game', description='Line one\n"quoted" \\ text', rating='0.85')
        status, body = self.request('POST', '/api/library/game?' + query, json.dumps(metadata).encode())
        self.assertEqual(status, 200, body)
        self.assertEqual(json.loads(body)['details']['description'], metadata['description'])
        self.assertEqual(next(g for system in json.loads(self.request('GET', '/api/library')[1])['systems'] for g in system['games'] if g['path'] == path)['name'], 'My game')
        meta = (self.root / 'library/snes/metadata' / (key + '.meta')).read_text()
        self.assertIn('edited = "name,description,developer,publisher,genre,players,rating,released"', meta)
        # RetroArch's menus show the playlist's label: the new name reaches it, the other
        # entries and the playlist's own fields stay (issue 55).
        playlist = json.loads((self.root / f'playlists/{DB}.lpl').read_text())
        self.assertEqual(playlist['version'], '1.5')
        labels = {item['path']: item['label'] for item in playlist['items']}
        self.assertEqual(labels, {p: 'My game' if p == path else l for l, p in self.games.items()})
        self.assertEqual({item['core_path'] for item in playlist['items']}, {'DETECT'})
        metadata['rating'] = 'NaN'
        self.assertEqual(self.request('POST', '/api/library/game?' + query, json.dumps(metadata).encode())[0], 400)
        self.assertEqual(rom.read_bytes(), b'replacement')

    def test_delete_game(self):
        """Issue 57: a game deleted from Games takes its file, the tracks its disc file
        names and its playlist entries; details and media go only with the last copy of
        that name; an archive holding other games, or a name outside its folder, stays."""
        (self.root / 'info/snes9x_libretro.info').write_text(
            'display_name = "Snes9x"\ncorename = "Snes9x"\ndatabase = "' + DB + '"\nsupported_extensions = "sfc|smc|zip|cue|iso|chd"\n')
        folder = self.root / 'content/SNES'
        folder.mkdir(parents=True, exist_ok=True)
        games = dict(self.games, **{'Disc Game': '/app0/content/SNES/Disc Game.cue', 'Twin CHD': '/app0/content/SNES/Twin.chd',
                                    'Twin ISO': '/app0/content/SNES/Twin.iso', 'Pack A': '/app0/content/SNES/Pack.zip#a.sfc',
                                    'Pack B': '/app0/content/SNES/Pack.zip#b.sfc'})
        self.write_playlist(games)
        for path in games.values():
            (self.root / path.removeprefix('/app0/').split('#')[0]).write_bytes(b'backup')
        (folder / 'Disc Game.cue').write_text('FILE "Disc Game (Track 1).bin" BINARY\n  TRACK 01 MODE2/2352\n'
                                              'FILE "Disc Game (Track 2).bin" BINARY\n  TRACK 02 AUDIO\n'
                                              'FILE "../../config/keep.cfg" BINARY\n')
        tracks = [folder / 'Disc Game (Track 1).bin', folder / 'Disc Game (Track 2).bin']
        for track in tracks:
            track.write_bytes(b'track')
        keep = self.root / 'config/keep.cfg'
        keep.write_text('outside the content folder')
        meta = self.root / 'library/snes/metadata/Twin.meta'
        cover = self.root / 'library/snes/covers/Twin.png'
        for path in (meta, cover):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'shared')

        def delete(path):
            library = json.loads(self.request('GET', '/api/library')[1])
            system, game = next((s['id'], g) for s in library['systems'] for g in s['games'] if g['path'] == path)
            return self.request('POST', '/api/library/delete?' + urllib.parse.urlencode({'system': system, 'game': game['key'], 'path': path}))

        def listed():
            return {item['path'] for item in json.loads((self.root / f'playlists/{DB}.lpl').read_text())['items']}

        status, body = delete(games['Twin ISO'])
        self.assertEqual(status, 200, body)
        self.assertFalse((folder / 'Twin.iso').exists())
        self.assertTrue((folder / 'Twin.chd').exists() and meta.exists() and cover.exists())
        self.assertNotIn(games['Twin ISO'], listed())
        self.assertIn(games['Twin CHD'], listed())
        self.assertEqual(delete(games['Twin CHD'])[0], 200)
        self.assertFalse(meta.exists() or cover.exists())

        self.assertEqual(delete(games['Disc Game'])[0], 200)
        self.assertFalse((folder / 'Disc Game.cue').exists() or any(t.exists() for t in tracks))
        self.assertTrue(keep.exists())

        status, body = delete(games['Pack A'])
        self.assertEqual(status, 409, body)
        self.assertTrue((folder / 'Pack.zip').exists())
        self.assertTrue({games['Pack A'], games['Pack B']} <= listed())
        query = urllib.parse.urlencode({'system': 'snes', 'game': 'keep', 'path': '/app0/config/keep.cfg'})
        self.assertEqual(self.request('POST', '/api/library/delete?' + query)[0], 404)
        self.assertTrue(keep.exists())
        self.assertEqual(listed(), set(games.values()) - {games['Twin ISO'], games['Twin CHD'], games['Disc Game']})

    def test_games_page_in_a_browser(self):
        playwright = next((str(p) for p in (Path.home() / '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright-core',
                                            Path('/usr/lib/chatgpt/resources/cua_node/lib/node_modules/playwright-core')) if p.exists()),
                          os.environ.get('PLAYWRIGHT_PATH', ''))
        if not playwright or not Path('/usr/bin/chromium').exists():
            self.skipTest('needs Playwright and Chromium')
        import shutil
        shutil.copytree(ROOT / 'webui', self.root / 'webui', dirs_exist_ok=True)
        for game_path in self.games.values():
            backup = self.root / game_path.removeprefix('/app0/')
            backup.parent.mkdir(parents=True, exist_ok=True)
            backup.write_bytes(b'game backup fixture')
        # Page files newer than the running server: the page asks for a restart.
        (self.root / 'webui/version.json').write_text('{"build": "newer-than-the-server"}\n')
        # Covers must decode: the transfer-only fixture is text, and the page
        # replaces failed images, racing the browser's cover-count assertion.
        import base64
        from unittest.mock import patch
        png = base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4////fwAJ+wP9KobjigAAAABJRU5ErkJggg==')
        state_folder = self.root / 'savestates/Snes9x'
        state_folder.mkdir(parents=True)
        key = Path(next(path for label, path in self.games.items() if 'Jungle Run' in label)).stem
        (state_folder / (key + '.state5')).write_bytes(b'preview state')
        (state_folder / (key + '.state5.png')).write_bytes(png)
        with patch(__name__ + '.image', return_value=png):
            run = subprocess.run(['node', 'tests/webui_scraper_browser.cjs'], cwd=ROOT, capture_output=True, text=True, timeout=300,
                                 env={**os.environ, 'PLAYWRIGHT_PATH': playwright, 'WEBUI_TEST_URL': f'http://127.0.0.1:{self.port}'})
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
