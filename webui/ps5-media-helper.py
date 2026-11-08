#!/usr/bin/env python3
# PS5 RetroArch media helper: downloads a scraping job's media on this PC and sends it
# to the console (the WebUI's "Download through this PC"). Python 3, standard library.
#
#   python3 ps5-media-helper.py http://<PS5-IP>:6769 <job id>
#
# The console identifies the games and hands out downloads; this PC fetches each file
# from the media source and sends the bytes to the console, which stores them; when a
# console has no route to the internet at all, it asks this PC those questions too (a
# probe is a HEAD to the source, a fetch a file the console reads itself, such as a
# folder's listing). Keep it running until the job is done; stop it with Ctrl+C and run
# it again to resume: the console never hands out a file it already has.
#
# Copyright (C) 2026 Mihawk; SPDX-License-Identifier: GPL-3.0-or-later
import http.client
import json
import os
import shutil
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

WORKERS = 16  # downloads at once: small files wait on the network, not the PC
CHUNK = 1 << 20


def console_request(base, method, path, body=None, length=None, token=None):
    url = urllib.parse.urlsplit(base)
    conn = http.client.HTTPConnection(url.hostname, url.port or 80, timeout=120)
    headers = {}
    if token:
        headers['X-RetroArch-Token'] = token
    if length is not None:
        headers['Content-Length'] = str(length)
    conn.request(method, path, body=body, headers=headers)
    response = conn.getresponse()
    data = response.read()
    conn.close()
    return response.status, data


class Helper:
    def __init__(self, base, job):
        self.base, self.job = base.rstrip('/'), job
        status, data = console_request(self.base, 'GET', '/api/status')
        if status != 200:
            sys.exit(f'The console did not answer at {self.base} (HTTP {status}).')
        self.token = json.loads(data)['token']
        self.lock = threading.Lock()
        self.downloaded = self.downloaded_bytes = self.sent = self.sent_bytes = self.missing = 0
        self.failed = 0
        self.temp = tempfile.mkdtemp(prefix='ps5-media-')
        self.fresh = True  # the first request: what an earlier helper held is ours now

    def note(self):
        with self.lock:
            print(f'\rDownloaded {self.downloaded} ({self.downloaded_bytes / 1e6:.1f} MB) · '
                  f'sent to the PS5 {self.sent} ({self.sent_bytes / 1e6:.1f} MB) · '
                  f'not at the source {self.missing} · failed {self.failed}   ', end='', flush=True)

    def tasks(self):
        status, data = console_request(self.base, 'GET', f'/api/scraper/pc/tasks?id={self.job}&max={WORKERS * 2}'
                                       + ('&fresh=1' if self.fresh else ''), token=self.token)
        self.fresh = False
        if status != 200:
            raise RuntimeError(f'The console refused the job (HTTP {status}).')
        result = json.loads(data)
        if 'error' in result:
            raise RuntimeError(result['error'])
        return result

    def run_task(self, task):
        query = f'id={self.job}&task={task["task"]}'
        if task['kind'] == 'probe':
            return self.run_probe(task, query)
        path = os.path.join(self.temp, f'{task["task"]}.part')
        try:
            # Download: streamed to a temporary file on this PC, in 1 MiB pieces.
            try:
                request = urllib.request.Request(task['url'], headers={'User-Agent': 'PS5-RetroArch-Helper/1'})
                with urllib.request.urlopen(request, timeout=60) as source, open(path, 'wb') as out:
                    shutil.copyfileobj(source, out, CHUNK)
            except urllib.error.HTTPError as error:
                if error.code == 404:
                    console_request(self.base, 'POST', f'/api/scraper/pc/downloaded?{query}&found=0&bytes=0',
                                    token=self.token)
                    with self.lock:
                        self.missing += 1
                    return
                raise
            size = os.path.getsize(path)
            console_request(self.base, 'POST', f'/api/scraper/pc/downloaded?{query}&found=1&bytes={size}',
                            token=self.token)
            with self.lock:
                self.downloaded += 1
                self.downloaded_bytes += size
            self.note()
            # Transfer: the bytes to the console, which stores them whole or not at all.
            with open(path, 'rb') as body:
                status, _ = console_request(self.base, 'PUT', f'/api/scraper/pc/media?{query}', body, size, self.token)
            if status == 201:
                with self.lock:
                    self.sent += 1
                    self.sent_bytes += size
            elif status != 409:  # 409: no longer wanted (cancelled, or done meanwhile)
                raise RuntimeError(f'the console answered {status}')
        except Exception as error:  # the lease runs out and the console hands it out again
            with self.lock:
                self.failed += 1
            print(f'\n{task["kind"]} of item {task["item"]}: {error}')
        finally:
            if os.path.exists(path):
                os.remove(path)
            self.note()

    def run_probe(self, task, query):
        # The console asks whether the source has a file; the answer decides which
        # name its game matches, so anything but a 404 or an answer is trouble to
        # retry when the lease runs out, not a miss.
        found = None
        try:
            request = urllib.request.Request(task['url'], headers={'User-Agent': 'PS5-RetroArch-Helper/1'},
                                             method='HEAD')
            with urllib.request.urlopen(request, timeout=60):
                found = 1
        except urllib.error.HTTPError as error:
            found = 0 if error.code == 404 else None
        except Exception:
            found = None
        if found is None:  # the lease runs out and the console asks again
            with self.lock:
                self.failed += 1
            return
        console_request(self.base, 'POST', f'/api/scraper/pc/downloaded?{query}&found={found}&bytes=0',
                        token=self.token)

    def run(self):
        print(f'Job {self.job} on {self.base}: downloading on this PC, sending to the PS5.')
        print('Keep this window open until the job is done. Ctrl+C stops; run again to resume.')
        threads = []
        try:
            while True:
                threads = [t for t in threads if t.is_alive()]
                if len(threads) >= WORKERS:
                    time.sleep(0.05)
                    continue
                result = self.tasks()
                if result['state'] != 'running':
                    print(f'\nThe job is {result["state"]}.')
                    break
                if not result['tasks']:
                    if not result['more'] and not threads:
                        break
                    time.sleep(1)
                    continue
                for task in result['tasks']:
                    thread = threading.Thread(target=self.run_task, args=(task,), daemon=True)
                    thread.start()
                    threads.append(thread)
            for thread in threads:
                thread.join()
            self.note()
            print('\nDone.')
        except KeyboardInterrupt:
            print('\nStopped. Run the same command again to resume.')
        finally:
            shutil.rmtree(self.temp, ignore_errors=True)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__ or 'usage: ps5-media-helper.py http://<PS5-IP>:6769 <job id>')
    Helper(sys.argv[1], sys.argv[2]).run()
