/* The page's transfer engine in Chromium against a native webui_server_main fixture
 * (tests/test_webui_transfer_browser.py starts it): many small files go as batches,
 * large ones as parallel parts, the rest one to a request; all arrive whole, names
 * that exist are refused, and a cancelled large file leaves nothing behind. */
const assert = require('node:assert/strict');
const { chromium } = require(process.env.PLAYWRIGHT_PATH || 'playwright');
const base = process.env.WEBUI_TEST_URL;
(async () => {
  const browser = await chromium.launch({ headless: true, executablePath: process.env.CHROMIUM_PATH || '/usr/bin/chromium' });
  try {
    const page = await browser.newPage();
    const errors = []; page.on('pageerror', e => errors.push(e.message));
    await page.route('https://api.github.com/**', route => route.fulfill({ json: [] }));
    const requests = []; page.on('request', r => { if (r.url().includes('/api/upload')) requests.push(new URL(r.url()).pathname); });
    await page.goto(base);
    await page.waitForFunction(() => !document.querySelector('#browse-files').disabled);
    // Every wait has a deadline and says what it saw: a stuck transfer fails here with its
    // state instead of hanging the gate (it once timed out silently after 600 s).
    await page.evaluate(() => {
      window.report = () => { const s = {}; for (const x of transfers) s[x.state] = (s[x.state] || 0) + 1;
        return JSON.stringify({ states: s, open: transfers.filter(x => ['queued', 'uploading'].includes(x.state)).slice(0, 5).map(x => `${x.name} ${x.state} ${x.sent || 0}/${x.size || x.file?.size} ${x.message || ''}`) }); };
      window.settled = (limit = 180000) => new Promise((resolve, reject) => { const end = Date.now() + limit;
        const t = setInterval(() => { if (!transfers.some(x => ['queued', 'uploading'].includes(x.state))) { clearInterval(t); resolve(); }
          else if (Date.now() > end) { clearInterval(t); reject(new Error('transfers still open after ' + limit / 1000 + ' s: ' + report())); } }, 50); });
    });
    // 600 small files, 3 medium, 2 large: made in the page, queued as a person drops them.
    const summary = await page.evaluate(async () => {
      const make = (name, size, seed) => { const b = new Uint8Array(size); for (let i = 0; i < size; i += 4096) b[i] = (seed + i) & 255; return new File([b], name); };
      const files = [];
      for (let i = 0; i < 600; i++) files.push(make(`shot-${String(i).padStart(4, '0')}.png`, 1000 + (i * 997) % 60000, i));
      for (let i = 0; i < 3; i++) files.push(make(`medium-${i}.bin`, 9 * 1024 * 1024 + i, 7 + i));
      for (let i = 0; i < 2; i++) files.push(make(`large-${i}.iso`, 150 * 1024 * 1024 + 4097 * i, 31 + i));
      const started = performance.now();
      queueFiles(files, 'engine');
      await settled();
      const states = {}; for (const x of transfers) states[x.state] = (states[x.state] || 0) + 1;
      return { states, seconds: (performance.now() - started) / 1000, failed: transfers.filter(x => x.state === 'failed').map(x => x.name + ': ' + x.message).slice(0, 5) };
    });
    assert.deepEqual(summary.states, { complete: 605 }, JSON.stringify(summary));
    const count = name => requests.filter(p => p === name).length;
    assert.ok(count('/api/upload/batch') >= 3 && count('/api/upload/batch') <= 10, `batches: ${count('/api/upload/batch')}`);
    assert.equal(count('/api/upload'), 3);
    assert.equal(count('/api/upload/session'), 2);
    assert.equal(count('/api/upload/commit'), 2);
    assert.ok(count('/api/upload/part') >= 12, `parts: ${count('/api/upload/part')}`);
    // The same names again: every one is refused, none replaced.
    const again = await page.evaluate(async () => {
      const files = [new File([new Uint8Array(10)], 'shot-0001.png'), new File([new Uint8Array(9 * 1024 * 1024)], 'medium-0.bin')];
      const before = transfers.length; queueFiles(files, 'engine');
      await settled();
      return transfers.slice(before).map(x => x.state + ': ' + x.message);
    });
    assert.deepEqual(again.map(x => x.split(':')[0]), ['failed', 'failed']);
    assert.match(again[0], /already exists/); assert.match(again[1], /already exists/);
    // A large file cancelled mid-way: the session is dropped.
    const cancelled = await page.evaluate(async () => {
      const before = transfers.length; queueFiles([new File([new Uint8Array(200 * 1024 * 1024)], 'cancel.iso')], 'engine');
      const transfer = transfers[before];
      // Until 20 MiB went, or it ended first (then the state says why), or a minute passed.
      const end = Date.now() + 60000;
      await new Promise((resolve, reject) => { const t = setInterval(() => {
        if ((transfer.sent || 0) > 20 * 1024 * 1024) { clearInterval(t); resolve(); }
        else if (!['queued', 'uploading'].includes(transfer.state)) { clearInterval(t); reject(new Error('cancel.iso ended before 20 MiB: ' + transfer.state + ' ' + (transfer.message || ''))); }
        else if (Date.now() > end) { clearInterval(t); reject(new Error('cancel.iso sent ' + (transfer.sent || 0) + ' bytes in 60 s')); } }, 10); });
      cancelTransfer(transfer);
      await new Promise(resolve => setTimeout(resolve, 1500));
      return transfer.state;
    });
    assert.equal(cancelled, 'cancelled');
    assert.deepEqual(errors, []);
    console.log(`PASS: 605 files (600 small in ${count('/api/upload/batch')} batches, 3 single, 2 large in ${count('/api/upload/part')} parts) in ${summary.seconds.toFixed(1)} s; duplicates refused; cancel drops the session.`);
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exit(1); });
