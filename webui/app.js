/* RetroArch PS5 WebUI. No external runtime or browser storage of console credentials. */
'use strict';
const $ = (selector, root = document) => root.querySelector(selector);
const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
const repo = 'mihawk-99/PS5_RetroArch';
let token = '', connected = false, freeBytes = null, uploadLimit = 64 * 1024 ** 3;
let currentPath = '', entries = [], folderRequest = 0, settingsValues = {};
let sessionRequest = null, sending = false, nextTransfer = 0, frontend = 'retroarch';
const transfers = [];
// The Games and Download Media tabs' state: declared here, as a page opened straight on
// either tab reaches them before the code below them has run.
let library = { systems: [] }, selectedGames = new Map(), shownGames = 300;
let scrapeChain = ['libretro'], scrapeKinds = new Set(), scraperSettings = null, scrapeDetails = true;
let recap = null, recapStamp = '', recapFilter = 'all', recapKind = '', recapLimit = {};
let jobTimer = null, jobShown = null;
function element(tag, text, className) {
  const node = document.createElement(tag);
  if (text !== undefined) node.textContent = text;
  if (className) node.className = className;
  return node;
}
function icon(kind) { return uiIcon({folder:'folder', file:'file-text', check:'check'}[kind] || kind); }
function bytes(n) {
  if (n === null || !Number.isFinite(n)) return 'Unknown';
  const units = ['B', 'KiB', 'MiB', 'GiB', 'TiB'];
  let i = 0;
  while (n >= 1024 && i < units.length - 1) { n /= 1024; i++; }
  return `${n.toLocaleString(undefined, { maximumFractionDigits: i ? 1 : 0 })} ${units[i]}`;
}
function announce(message, failure = false) {
  const node = $('#announcement'); node.textContent = message;
  node.classList.toggle('error', failure);
}
async function api(path, options = {}) {
  const headers = new Headers(options.headers || {});
  if (options.method && options.method !== 'GET') headers.set('X-RetroArch-Token', token);
  const response = await fetch(path, { ...options, headers, cache: 'no-store', signal: options.signal || AbortSignal.timeout(15000) });
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || `Request failed (${response.status}). Try again.`);
  return data;
}
function setConnection(ok) {
  connected = ok;
  $('#connection').classList.toggle('offline', !ok);
  // The WebUI stays up while the title changes frontend (src/webui_link.h): say which one shows.
  const showing = { retroarch: 'RetroArch is running', picker: 'Frontend picker is open', 'es-de': 'EmulationStation is open', title: 'PS5 RetroArch is starting' }[frontend] || 'PS5 RetroArch is closed';
  $('#connection span:last-child').textContent = ok ? showing : 'Console disconnected';
  $('#connection-notice').hidden = ok;
  for (const id of ['destination', 'dropzone', 'browse-files', 'upload-here', 'folder-name', 'quick-volume', 'quick-rumble', 'quick-frontend', 'settings-fields', 'save-settings']) {
    $('#' + id).disabled = !ok || (['settings-fields', 'save-settings'].includes(id) && !editorRevision) || (['quick-volume', 'quick-rumble', 'quick-frontend', 'settings-fields', 'save-settings'].includes(id) && !Object.keys(settingsValues).length);
  }
  $('#folder-form button').disabled = !ok;
  updateButton();
  $('#install-update').disabled = !ok;
}
// A server older than the page files beside it (a deploy or an update while it ran):
// some features are missing until the title restarts it.
async function checkServerBuild(build) {
  try {
    const page = await (await fetch('/version.json', { cache: 'no-store', signal: AbortSignal.timeout(10000) })).json();
    $('#restart-notice').hidden = !page.build || build === page.build;
  } catch { $('#restart-notice').hidden = true; }
}
async function reconnect() {
  if (sessionRequest) return sessionRequest;
  sessionRequest = (async () => {
    try {
      const state = await api('/api/status');
      if (token && token !== state.token && updateState === 'installing') { location.reload(); return false; }
      const recovered = !connected || token !== state.token;
      token = state.token; freeBytes = state.freeBytes; uploadLimit = state.uploadLimit; frontend = state.frontend ?? 'retroarch';
      setConnection(true);
      i18n.follow(state.language);
      checkServerBuild(state.build);
      $('#storage-info').textContent = freeBytes === null ? 'Games and files stored on your PS5' : `${bytes(freeBytes)} free on the console`;
      if (recovered) await Promise.all([loadLibrary(), loadSettings(), loadContent(currentPath), loadAlerts(), loadUpdate()]);
      return true;
    } catch (error) { setConnection(false); return false; }
    finally { sessionRequest = null; }
  })();
  return sessionRequest;
}
function pageFromHash() {
  const name = location.hash.slice(1).split('?')[0];
  return ['content', 'games', 'media', 'transfers', 'settings'].includes(name) ? name : 'overview';
}
function navigate() {
  const page = pageFromHash();
  $$('.page').forEach(node => { node.hidden = node.id !== page; });
  $$('[data-page]').forEach(node => { if (node.dataset.page === page) node.setAttribute('aria-current', 'page'); else node.removeAttribute('aria-current'); });
  $('#page-title').textContent = page === 'media' ? 'Download Media' : page[0].toUpperCase() + page.slice(1);
  if (page === 'content') loadContent(currentPath);
  if (page === 'games') loadGames();
  if (page === 'media') openMedia();
  $('#main').focus({ preventScroll: true });
}
function openFolder(path) {
  ++folderRequest; // Ignore a previous folder response while the route changes.
  currentPath = path;
  if (location.hash !== '#content') location.hash = 'content';
  else loadContent(path);
}
async function loadLibrary() {
  const target = $('#library-folders');
  try {
    const data = await api('/api/content');
    const folders = data.entries.filter(e => e.directory);
    target.replaceChildren();
    const select = $('#destination'), selected = select.value;
    select.replaceChildren(new Option('Content folder', ''));
    for (const entry of folders) {
      select.add(new Option(entry.name, entry.name));
      if (target.childElementCount < 3) {
        const row = element('a', undefined, 'folder-row'); row.href = '#content';
        row.append(icon('folder'), element('strong', entry.name), element('span', 'Open content'));
        row.addEventListener('click', event => { event.preventDefault(); openFolder(entry.name); });
        target.append(row);
      }
    }
    if ([...select.options].some(o => o.value === selected)) select.value = selected;
    if (!folders.length) {
      const box = element('div', undefined, 'list-message');
      box.append(element('p', 'Your content starts here. Upload a file or create a folder.'));
      const link = element('a', 'Open content'); link.href = '#content'; box.append(link); target.append(box);
    } else if (folders.length > 3) {
      const more = element('a', `View all ${folders.length} folders`, 'list-message'); more.href = '#content';
      more.addEventListener('click', event => { event.preventDefault(); openFolder(''); }); target.append(more);
    }
  } catch (error) { target.replaceChildren(element('p', 'Library unavailable. Reconnect or open Content to retry.', 'list-message')); }
}
function renderBreadcrumbs() {
  const nav = $('#breadcrumbs'); nav.replaceChildren();
  const parts = currentPath ? currentPath.split('/') : [];
  ['', ...parts].forEach((part, index) => {
    const path = parts.slice(0, index).join('/');
    const button = element('button', index ? part : 'Content', 'text-button');
    button.type = 'button'; button.disabled = index === parts.length;
    button.addEventListener('click', () => openFolder(path)); nav.append(button);
  });
}
async function loadContent(path) {
  const request = ++folderRequest;
  $('#content-error').hidden = true;
  $('#content-list').setAttribute('aria-busy', 'true');
  try {
    const data = await api('/api/content?path=' + encodeURIComponent(path));
    if (request !== folderRequest) return;
    currentPath = data.path; entries = data.entries; renderBreadcrumbs();
    $('#file-search').value = '';
    $('#folder-caption').textContent = data.truncated ? 'Showing the first 10,000 items. Organize files into smaller folders to see more.' : `${entries.length} ${entries.length === 1 ? 'item' : 'items'}`;
    renderContent();
  } catch (error) {
    if (request !== folderRequest) return;
    $('#content-error').textContent = error.message; $('#content-error').hidden = false;
    $('#content-list').replaceChildren();
  } finally { if (request === folderRequest) $('#content-list').removeAttribute('aria-busy'); }
}
function renderContent() {
  const target = $('#content-list'); target.replaceChildren();
  const search = $('#file-search').value.toLocaleLowerCase();
  for (const entry of entries.filter(e => e.name.toLocaleLowerCase().includes(search))) {
    const row = element('div', undefined, 'content-row');
    const path = [currentPath, entry.name].filter(Boolean).join('/');
    const name = element(entry.directory ? 'button' : 'a', entry.name, 'file-name');
    if (entry.directory) { name.type = 'button'; name.addEventListener('click', () => openFolder(path)); }
    else {
      name.href = '/api/download?path=' + encodeURIComponent(path); name.download = entry.name;
      name.addEventListener('click', () => recordDownload(entry.name));
    }
    row.append(icon(entry.directory ? 'folder' : 'file'), name, element('span', entry.directory ? 'Folder' : bytes(entry.size), 'file-size'));
    if (!entry.directory) {
      const link = element('a', 'Download'); link.href = name.href; link.download = entry.name;
      link.addEventListener('click', () => recordDownload(entry.name)); row.append(link);
    }
    target.append(row);
  }
  if (!target.childElementCount) target.append(element('p', search ? 'No matching files. Try another name.' : 'This folder is empty. Upload files here to get started.', 'list-message'));
}
function validName(name) { return !!name && name[0] !== '.' && !/[\x00-\x1f\x7f\\/:]/.test(name) && new TextEncoder().encode(name).length <= 255; }
$('#folder-form').addEventListener('submit', async event => {
  event.preventDefault(); const input = $('#folder-name'), name = input.value.trim();
  if (!validName(name)) { announce('Use a folder name without slashes, a leading dot, or control characters.', true); return; }
  const button = $('#folder-form button'); button.disabled = true;
  try {
    await api('/api/folder?path=' + encodeURIComponent([currentPath, name].filter(Boolean).join('/')), { method: 'POST' });
    input.value = ''; announce(`Created ${name}.`); await Promise.all([loadLibrary(), loadContent(currentPath)]);
  } catch (error) { announce(error.message, true); }
  finally { button.disabled = !connected; }
});
function drawTransfers() {
  for (const selector of ['#recent-transfers', '#all-transfers']) {
    const target = $(selector);
    const shown = selector === '#recent-transfers' ? transfers.slice(-3).reverse() : [...transfers].reverse();
    const visible = new Set(shown.map(transfer => String(transfer.id)));
    for (const child of [...target.children]) if (!visible.has(child.dataset.transfer)) child.remove();
    if (!shown.length) { const empty = element('div', undefined, 'empty-state'); empty.append(icon('file'), element('p', 'Your transfers will appear here.')); target.append(empty); }
    for (const [index, transfer] of shown.entries()) {
      let row = target.querySelector(`[data-transfer="${transfer.id}"]`);
      if (!row) {
        row = element('div', undefined, 'transfer-row'); row.dataset.transfer = transfer.id;
        const details = element('div', undefined, 'transfer-details');
        details.append(element('strong', transfer.name), element('span', '', 'muted')); row.append(details);
        target.insertBefore(row, target.children[index] || null);
      }
      const details = row.firstElementChild, message = details.children[1];
      message.textContent = transfer.message; message.className = transfer.state === 'failed' ? 'inline-error' : 'muted';
      if (['queued', 'uploading'].includes(transfer.state)) {
        let progress = details.querySelector('progress');
        if (!progress) {
          progress = document.createElement('progress'); progress.max = 100;
          progress.setAttribute('aria-label', `Upload progress for ${transfer.name}`); details.append(progress);
          const cancel = element('button', 'Cancel', 'secondary');
          cancel.addEventListener('click', () => cancelTransfer(transfer));
          row.append(cancel);
        }
        progress.value = transfer.percent || 0;
      } else { details.querySelector('progress')?.remove(); row.querySelector('button')?.remove(); }
    }
  }
}
function recordDownload(name) {
  transfers.push({ id: ++nextTransfer, name, state: 'download', message: 'Download requested · check your browser’s download manager' }); drawTransfers();
}
function queueFiles(files, destination) {
  if (!connected) { announce('Reconnect to the console before uploading files.', true); return; }
  for (const file of files) {
    if (!validName(file.name) || file.size > uploadLimit) { announce(`${file.name}: choose a valid filename and a file no larger than ${bytes(uploadLimit)}.`, true); continue; }
    transfers.push({ id: ++nextTransfer, name: file.name, file, path: [destination, file.name].filter(Boolean).join('/'), state: 'queued', message: `Queued · ${bytes(file.size)}`, percent: 0 });
  }
  drawTransfers(); sendNext();
}
// The transfer engine (src/webui_transfer.h on the console): six lanes run at once.
// Small files travel many to a request (a batch), large files as parts on every free
// lane (an upload session), the rest one to a request. Every file is renamed into
// place on the console only once whole.
const LANES = 6, SMALL = 4 * 1024 ** 2, LARGE = 64 * 1024 ** 2, PART = 32 * 1024 ** 2;
const BATCH_FILES = 256, BATCH_BYTES = 16 * 1024 ** 2, PART_TRIES = 3;
let jobs = [], opening = 0; // sessions being opened: their parts are about to queue
function cancelTransfer(transfer) {
  if (!['queued', 'uploading'].includes(transfer.state)) return;
  transfer.state = 'cancelled'; transfer.message = 'Cancelled';
  for (const xhr of transfer.requests || []) xhr.abort();
  if (transfer.session) api('/api/upload/session?id=' + encodeURIComponent(transfer.session), { method: 'DELETE' }).catch(() => {});
  transfer.file = null; drawTransfers();
}
function showProgress(transfer, sent) {
  transfer.sent = Math.min(sent, transfer.file?.size ?? sent);
  const size = transfer.file?.size || 0, percent = size ? Math.floor(transfer.sent / size * 100) : 100;
  if (percent === transfer.percent && transfer.state === 'uploading') return;
  transfer.percent = percent; transfer.state = 'uploading';
  transfer.message = percent >= 100 ? 'Finishing on the console…' : `${percent}% · ${bytes(transfer.sent)} of ${bytes(size)}`;
  const now = Date.now(); if (now - (transfer.drawn || 0) > 150 || percent >= 100) { transfer.drawn = now; drawTransfers(); }
}
function finish(transfer, state, message) {
  if (transfer.state === 'cancelled') return;
  transfer.state = state; transfer.message = message; transfer.file = null; transfer.requests = [];
  transfer.session = null; drawTransfers();
}
// One request on a lane: resolves with {status, body}, or rejects when the connection fails.
function send(method, url, body, transfers, onProgress) {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    for (const transfer of transfers) (transfer.requests ||= []).push(xhr);
    const done = () => { for (const transfer of transfers) transfer.requests = (transfer.requests || []).filter(x => x !== xhr); };
    xhr.open(method, url); xhr.setRequestHeader('X-RetroArch-Token', token);
    if (onProgress) xhr.upload.onprogress = event => onProgress(event.loaded);
    xhr.onload = () => { done(); let parsed = {}; try { parsed = JSON.parse(xhr.responseText); } catch {} resolve({ status: xhr.status, body: parsed }); };
    xhr.onerror = () => { done(); reject(new Error('Connection lost. Reconnect, then choose this file again.')); };
    xhr.onabort = () => { done(); reject(new Error('Cancelled')); };
    xhr.send(body);
  });
}
// The batch stream: per file a u16 name length, the name (UTF-8), a u64 size and the
// file itself, then a zero length. A Blob of the files' own Blobs: nothing is copied.
function batchBody(transfers) {
  const parts = [], encoder = new TextEncoder();
  for (const transfer of transfers) {
    const name = encoder.encode(transfer.path), head = new Uint8Array(2 + name.length + 8), view = new DataView(head.buffer);
    view.setUint16(0, name.length, true); head.set(name, 2); view.setBigUint64(2 + name.length, BigInt(transfer.file.size), true);
    parts.push(head, transfer.file);
  }
  parts.push(new Uint8Array(2));
  return new Blob(parts);
}
async function runBatch(transfers) {
  transfers = transfers.filter(transfer => transfer.state === 'queued');
  if (!transfers.length) return;
  const encoder = new TextEncoder(), starts = []; let at = 0;
  for (const transfer of transfers) { at += 2 + encoder.encode(transfer.path).length + 8; starts.push(at); at += transfer.file.size; }
  for (const transfer of transfers) showProgress(transfer, 0);
  try {
    const { status, body: result } = await send('PUT', '/api/upload/batch', batchBody(transfers), transfers,
      loaded => transfers.forEach((transfer, i) => showProgress(transfer, loaded - starts[i])));
    const failed = new Map((result.failed || []).map(entry => [entry.name, entry.error]));
    const skipped = new Set(result.skippedNames || []);
    for (const transfer of transfers) {
      if (transfer.state !== 'uploading') continue;
      if (skipped.has(transfer.path)) finish(transfer, 'failed', 'A file with this name already exists. Rename your file first.');
      else if (failed.has(transfer.path)) finish(transfer, 'failed', `Upload failed (${failed.get(transfer.path)}). Choose the file again to retry.`);
      else if (status === 200) finish(transfer, 'complete', `Uploaded · ${bytes(transfer.file.size)}`);
      else finish(transfer, 'failed', result.error || result.problem || 'Upload failed. Choose the file again to retry.');
    }
  } catch (error) {
    for (const transfer of transfers) if (transfer.state === 'uploading') finish(transfer, 'failed', error.message);
    if (error.message !== 'Cancelled') setConnection(false);
  }
}
async function runSingle(transfer) {
  if (transfer.state !== 'queued') return;
  showProgress(transfer, 0);
  try {
    const { status, body } = await send('PUT', '/api/upload?path=' + encodeURIComponent(transfer.path), transfer.file, [transfer], loaded => showProgress(transfer, loaded));
    if (status === 201) finish(transfer, 'complete', `Uploaded · ${bytes(transfer.file.size)}`);
    else finish(transfer, 'failed', body.error || 'Upload failed. Choose the file again to retry.');
  } catch (error) { finish(transfer, error.message === 'Cancelled' ? 'cancelled' : 'failed', error.message); if (error.message !== 'Cancelled') setConnection(false); }
}
// A large file: a session, then its parts as jobs any lane takes. The part that
// completes the file commits it, on its own lane: no lane ever waits on another.
async function runLarge(transfer) {
  if (transfer.state !== 'queued') return;
  showProgress(transfer, 0);
  let opened;
  opening++;
  try { opened = await send('POST', `/api/upload/session?path=${encodeURIComponent(transfer.path)}&size=${transfer.file.size}`, null, [transfer]); }
  catch (error) { finish(transfer, 'failed', error.message); return; }
  finally { opening--; }
  if (opened.status !== 201) { finish(transfer, 'failed', opened.body.error || 'Upload failed. Choose the file again to retry.'); return; }
  transfer.session = opened.body.id;
  const size = transfer.file.size, part = Math.max(4 * 1024 ** 2, Math.min(opened.body.partSize || PART, Math.ceil(size / LANES)));
  const sent = new Map(), parts = [];
  for (let start = 0; start < size; start += part) parts.push([start, Math.min(start + part, size)]);
  let left = parts.length, failure = null;
  const abandon = message => {
    if (failure) return;
    failure = message;
    api('/api/upload/session?id=' + encodeURIComponent(transfer.session), { method: 'DELETE' }).catch(() => {});
    if (message !== 'Cancelled') finish(transfer, 'failed', message);
  };
  const commit = async () => {
    try {
      const { status, body } = await send('POST', '/api/upload/commit?id=' + encodeURIComponent(transfer.session), null, [transfer]);
      if (status === 201) finish(transfer, 'complete', `Uploaded · ${bytes(size)}`);
      else finish(transfer, 'failed', body.error || 'Upload failed. Choose the file again to retry.');
    } catch (error) { finish(transfer, 'failed', error.message); }
  };
  const partJob = ([start, end]) => async () => {
    for (let attempt = 1; !failure; attempt++) {
      if (transfer.state !== 'uploading') { abandon('Cancelled'); return; }
      try {
        const { status, body } = await send('PUT', `/api/upload/part?id=${encodeURIComponent(transfer.session)}&offset=${start}`, transfer.file.slice(start, end), [transfer],
          loaded => { sent.set(start, loaded); showProgress(transfer, [...sent.values()].reduce((a, b) => a + b, 0)); });
        if (status === 200) { sent.set(start, end - start); break; }
        if (attempt >= PART_TRIES || status === 404 || status === 416) { abandon(body.error || 'A part could not be written.'); return; }
      } catch (error) {
        if (error.message === 'Cancelled') { abandon('Cancelled'); return; }
        if (attempt >= PART_TRIES) { abandon(error.message); return; }
      }
    }
    if (!failure && --left === 0) await commit();
  };
  // First in the queue, so every free lane joins this file at once.
  jobs.unshift(...parts.map(partJob));
}
// Turns the queued transfers into jobs: batches of small files, sessions, single files.
function planJobs() {
  let batch = [], batchBytes = 0;
  const flush = () => { if (batch.length) { const group = batch; jobs.push(() => runBatch(group)); } batch = []; batchBytes = 0; };
  for (const transfer of transfers) {
    if (transfer.state !== 'queued' || transfer.planned) continue;
    transfer.planned = true;
    const size = transfer.file.size;
    if (size < SMALL) {
      if (batch.length >= BATCH_FILES || batchBytes + size > BATCH_BYTES) flush();
      batch.push(transfer); batchBytes += size;
    } else if (size >= LARGE) jobs.push(() => runLarge(transfer));
    else jobs.push(() => runSingle(transfer));
  }
  flush();
}
async function sendNext() {
  planJobs();
  if (sending) return;
  sending = true;
  try {
    if (!connected && !await reconnect()) {
      for (const transfer of transfers) if (transfer.state === 'queued') finish(transfer, 'failed', 'Console disconnected. Reconnect, then choose this file again.');
      jobs = []; return;
    }
    // Each lane runs one job at a time until none is left; a job is one request (or a
    // part and its file's commit), so no lane waits on another lane's work.
    const lane = async () => {
      for (;;) {
        planJobs();
        const job = jobs.shift();
        if (job) await job();
        else if (opening) await new Promise(resolve => setTimeout(resolve, 25));
        else return;
      }
    };
    await Promise.all(Array.from({ length: LANES }, lane));
  } finally { sending = false; await Promise.all([loadLibrary(), loadContent(currentPath), reconnect()]); }
}
let pickerDestination = '';
function pickFiles(destination) { pickerDestination = destination; $('#file-input').click(); }
$('#browse-files').addEventListener('click', () => pickFiles($('#destination').value));
$('#dropzone').addEventListener('click', () => pickFiles($('#destination').value));
$('#upload-here').addEventListener('click', () => pickFiles(currentPath));
$('#file-input').addEventListener('change', event => { queueFiles([...event.target.files], pickerDestination); event.target.value = ''; });
for (const name of ['dragenter', 'dragover']) $('#dropzone').addEventListener(name, event => { event.preventDefault(); $('#dropzone').classList.add('dragging'); });
for (const name of ['dragleave', 'drop']) $('#dropzone').addEventListener(name, event => { event.preventDefault(); $('#dropzone').classList.remove('dragging'); });
$('#dropzone').addEventListener('drop', event => queueFiles([...event.dataTransfer.files], $('#destination').value));
window.addEventListener('dragover', event => event.preventDefault());
window.addEventListener('drop', event => event.preventDefault());
window.addEventListener('beforeunload', event => { if (Object.keys(editorDraft).length || transfers.some(t => ['queued', 'uploading'].includes(t.state))) { event.preventDefault(); event.returnValue = ''; } });
$('#clear-transfers').addEventListener('click', () => { for (let i = transfers.length - 1; i >= 0; i--) if (!['queued', 'uploading'].includes(transfers[i].state)) transfers.splice(i, 1); drawTransfers(); });
function updateQuick() {
  for (const [id, key, unit] of [['quick-volume', 'audio_volume', ' dB'], ['quick-rumble', 'input_rumble_gain', '%']]) {
    $('#' + id).value = settingsValues[key]; $(`output[for="${id}"]`).textContent = Number(settingsValues[key]) + unit;
  }
  // The frontend the title opens on: the picker every time, or one straight away.
  if (settingsValues.frontend_start) $('#quick-frontend').value = settingsValues.frontend_start;
}
let editorSettings = [], editorValues = {}, editorDraft = {}, editorRevision = '', editorPage = 0, editorRequest = 0;
let editorProfile = '', editorKind = 'core-options', editorMode = 'guided', editorCategory = '';
let editorMetadata = {}, editorCategories = [], metadataUnavailable = false;
const pageSize = 40;
function settingTitle(key) {
  const known = { audio_volume: 'Audio volume', input_rumble_gain: 'Rumble strength', video_smooth: 'Smooth image scaling', video_vsync: 'Vertical sync', menu_driver: 'Console menu' };
  return known[key] || key.replace(/[_-]/g, ' ').replace(/\b\w/g, c => c.toUpperCase()).replace(/\b(ppsspp|snes|nes|gba|gpu|cpu|msaa|fps|vsync|xmb|rgui)\b/gi, word => word.toUpperCase());
}
function editorUrl() { return '/api/config?scope=' + (editorProfile ? editorKind : 'global') + '&core=' + encodeURIComponent(editorProfile); }
function markEdits() {
  const count = Object.keys(editorDraft).length;
  $('#settings-result').textContent = count ? `${count} unsaved ${count === 1 ? 'change' : 'changes'}` : '';
  $('#refresh-settings').textContent = count ? 'Discard changes & refresh' : 'Refresh settings';
}
function settingGuide(setting) {
  if (!editorProfile || editorKind === 'core-settings') return globalGuide[setting.key];
  const meta = editorMetadata[setting.key];
  if (!meta) return null;
  return { ...meta, category: meta.category || 'general', control: meta.choices?.length ? 'select' : 'text', description: coreHelp(setting.key) || meta.description || 'This core provides the available choices but no explanation for this option. Keep its current value unless you know the change you need.' };
}
function choiceLabel(value, label) {
  if (['enabled', 'true'].includes(label)) return 'On';
  if (['disabled', 'Disabled', 'false'].includes(label)) return 'Off';
  return label || value;
}
function renderSettings() {
  const guided = editorMode === 'guided', query = $('#settings-search').value.trim().toLocaleLowerCase();
  const available = editorSettings.filter(s => !guided || settingGuide(s));
  if (guided) {
    const order = Object.keys(!editorProfile || editorKind === 'core-settings' ? globalGuide : editorMetadata);
    const rank = new Map(order.map((key, index) => [key, index]));
    available.sort((a, b) => rank.get(a.key) - rank.get(b.key));
  }
  const categories = (!editorProfile || editorKind === 'core-settings' ? globalCategories : [...editorCategories, { key: 'general', label: 'General', description: 'Options supplied by this core.' }])
    .filter(c => available.some(s => settingGuide(s)?.category === c.key));
  if (!categories.some(c => c.key === editorCategory)) editorCategory = categories[0]?.key || '';
  const nav = $('#settings-categories'); nav.replaceChildren(); nav.hidden = !guided || !categories.length;
  for (const category of categories) {
    const button = element('button', category.label); button.type = 'button'; button.dataset.category = category.key;
    button.setAttribute('aria-pressed', String(category.key === editorCategory && !query));
    button.addEventListener('click', () => { editorCategory = category.key; editorPage = 0; $('#settings-search').value = ''; renderSettings(); [...nav.children].find(n => n.dataset.category === category.key)?.focus(); }); nav.append(button);
  }
  const active = categories.find(c => c.key === editorCategory);
  $('#settings-category-title').textContent = guided ? (query ? 'Search results' : active?.label || 'Guided settings') : 'All settings';
  $('#settings-category-help').textContent = guided ? (query ? 'Matching settings from every category.' : active?.description || 'No option catalog is available for this core.') : 'Technical names and exact saved values. Use this view for settings not covered by the guide.';
  const uncovered = editorSettings.length - available.length;
  $('#settings-coverage').textContent = !guided ? '' : metadataUnavailable ? 'Core guidance could not be loaded. Refresh to try again, or use Advanced.' : uncovered ? `${uncovered} additional ${uncovered === 1 ? 'setting is' : 'settings are'} available in Advanced.` : '';
  $('#settings-coverage').hidden = !$('#settings-coverage').textContent;
  const matched = available.filter(s => {
    const meta = settingGuide(s);
    return (query ? `${meta?.label || settingTitle(s.key)} ${s.key} ${meta?.description || ''}`.toLocaleLowerCase().includes(query) : !guided || meta?.category === editorCategory);
  });
  const pages = Math.max(1, Math.ceil(matched.length / pageSize)); editorPage = Math.min(editorPage, pages - 1);
  const fields = $('#settings-fields'); fields.replaceChildren();
  $('#settings-count').textContent = query ? `${matched.length} settings match your search` : `${matched.length} settings`;
  for (const setting of matched.slice(editorPage * pageSize, (editorPage + 1) * pageSize)) {
    const meta = settingGuide(setting), value = editorDraft[setting.key] ?? setting.value;
    const row = element('div', undefined, 'setting-row'), details = element('div', undefined, 'setting-details');
    const label = element('label', guided ? meta.label : settingTitle(setting.key), 'setting-label');
    const id = 'setting-' + setting.key; label.htmlFor = id;
    if (!guided) label.append(element('small', setting.key));
    const description = element('p', meta?.description || 'No description is available for this technical setting. Keep its value unless you know the configuration change you need.', 'setting-description');
    description.id = id + '-help'; details.append(label, description);
    let input, control = element('div', undefined, 'setting-control'), output;
    const choices = guided ? meta.choices : setting.key === 'menu_driver' ? [['xmb', 'XMB'], ['rgui', 'RGUI']] : null;
    if (choices?.length) {
      input = document.createElement('select');
      for (const [value, text] of choices) input.add(new Option(choiceLabel(value, text), value));
      if (![...input.options].some(o => o.value === value)) input.add(new Option(`Current: ${value || '(empty)'}`, value));
      input.value = value;
    } else {
      input = document.createElement('input');
      input.type = guided && ['volume', 'rumble'].includes(meta.control) ? 'range' : (guided && meta.control === 'toggle') || setting.kind === 'bool' ? 'checkbox' : setting.kind;
      // A password the console keeps is never sent here (src/webui_ps5.cpp): it can be
      // replaced, not read; left empty, the saved one stays.
      if (setting.secret) { input.type = 'password'; input.autocomplete = 'new-password'; input.placeholder = setting.set ? 'Saved on your PS5' : 'Not set'; }
      if (input.type === 'checkbox') input.checked = value === 'true';
      else { input.value = value; input.maxLength = 4096; if (setting.kind === 'number') { input.step = 'any'; input.required = true; } }
      if (input.type === 'range') { input.min = meta.control === 'volume' ? -80 : 0; input.max = meta.control === 'volume' ? 12 : 100; input.step = meta.control === 'volume' ? '0.1' : '1'; input.value = value; }
      if (['checkbox', 'range'].includes(input.type)) {
        output = element('output'); output.htmlFor = id;
        const updateOutput = () => { output.textContent = input.type === 'checkbox' ? (input.checked ? 'On' : 'Off') : input.value + (meta?.control === 'volume' ? ' dB' : '%'); };
        updateOutput(); input.addEventListener('input', updateOutput);
      }
    }
    if (setting.key === 'audio_volume') { input.min = -80; input.max = 12; }
    if (setting.key === 'input_rumble_gain') { input.min = 0; input.max = 100; }
    input.id = id; input.name = setting.key; input.setAttribute('aria-describedby', description.id);
    input.addEventListener('input', () => {
      const updated = input.type === 'checkbox' ? String(input.checked) : input.value;
      if (updated === editorValues[setting.key]) delete editorDraft[setting.key]; else editorDraft[setting.key] = updated;
      row.classList.toggle('setting-edited', Object.hasOwn(editorDraft, setting.key)); markEdits();
    });
    row.classList.toggle('setting-edited', Object.hasOwn(editorDraft, setting.key));
    control.append(input); if (output) control.append(output); row.append(details, control); fields.append(row);
  }
  if (!matched.length) fields.append(element('p', query ? 'No matching settings. Try another search or switch to Advanced.' : guided ? 'No guided options are available. Refresh to try again, or use Advanced for saved values.' : 'No options are available for this profile.', 'list-message'));
  $('#settings-page').textContent = `Page ${editorPage + 1} of ${pages}`;
  $('.settings-paging').hidden = pages <= 1;
  $('#settings-previous').disabled = editorPage === 0; $('#settings-next').disabled = editorPage >= pages - 1;
}
async function loadEditor() {
  const request = ++editorRequest; editorRevision = '';
  for (const id of ['settings-profile', 'settings-kind', 'refresh-settings']) $('#' + id).disabled = true;
  $('#settings-fields').disabled = true; $('#save-settings').disabled = true;
  $('#settings-result').textContent = 'Loading settings…';
  try {
    const [data, metadata] = await Promise.all([api(editorUrl()), editorProfile && editorKind === 'core-options' ? api('/api/core-metadata?core=' + encodeURIComponent(editorProfile)).catch(() => null) : Promise.resolve({ categories: [], settings: [] })]);
    if (request !== editorRequest) return;
    metadataUnavailable = !metadata;
    // The previous running title may serve new assets before its next restart.
    const catalogs = [metadata?.bundled || {}, metadata?.runtime || metadata || {}];
    editorMetadata = Object.fromEntries(catalogs.flatMap(m => m.settings || []).map(s => [s.key, s]));
    editorCategories = [...new Map(catalogs.flatMap(m => m.categories || []).map(c => [c.key, c])).values()];
    editorSettings = data.settings; editorValues = Object.fromEntries(data.settings.map(s => [s.key, s.value]));
    editorRevision = data.revision; editorDraft = {}; editorPage = 0;
    $('#settings-heading').textContent = editorProfile ? `${editorProfile} · ${editorKind === 'core-options' ? 'Core options' : 'RetroArch overrides'}` : 'Global RetroArch settings';
    $('#settings-help').textContent = editorProfile
      ? 'Saved changes apply when you restart RetroArch. Game-specific settings may take priority. Core options control the emulator; RetroArch overrides change shared preferences for just this core.'
      : 'Start with everyday preferences in Guided, or switch to Advanced for the full configuration. Saved changes apply when you restart RetroArch. Core and game preferences may take priority.';
    renderSettings(); markEdits();
  } catch (error) { if (request === editorRequest) { editorRevision = ''; $('#settings-fields').replaceChildren(); $('#settings-result').textContent = error.message; } }
  finally { if (request === editorRequest) { for (const id of ['settings-profile', 'settings-kind', 'refresh-settings']) $('#' + id).disabled = false; $('#settings-fields').disabled = !connected || !editorRevision; $('#save-settings').disabled = !connected || !editorRevision; } }
}
async function loadSettings() {
  try {
    const [data, profiles] = await Promise.all([api('/api/settings'), api('/api/cores')]);
    settingsValues = Object.fromEntries(data.settings.map(s => [s.key, s.value]));
    const select = $('#settings-profile'); select.replaceChildren(new Option('Global RetroArch', ''));
    for (const name of profiles.cores) select.add(new Option(name, name)); select.value = editorProfile;
    setConnection(connected); updateQuick(); if (!Object.keys(editorDraft).length) await loadEditor();
  } catch (error) { $('#settings-result').textContent = 'Settings unavailable. Reconnect to try again.'; }
}
async function saveSettings(changes) {
  const body = Object.entries(changes).map(([key, value]) => `${key}=${value}`).join('\n');
  if (!body) return;
  await api('/api/settings', { method: 'POST', body, headers: { 'Content-Type': 'text/plain' } });
  Object.assign(settingsValues, changes); updateQuick();
  if (!editorProfile && !Object.keys(editorDraft).length) await loadEditor();
  announce('Settings saved. They will apply the next time you open RetroArch.');
}
$('#settings-form').addEventListener('submit', async event => {
  event.preventDefault(); if (!Object.keys(editorDraft).length) { $('#settings-result').textContent = 'No changes to save.'; return; }
  $('#save-settings').disabled = true; $('#settings-fields').disabled = true;
  for (const id of ['settings-profile', 'settings-kind', 'refresh-settings']) $('#' + id).disabled = true;
  try {
    const body = Object.entries(editorDraft).map(([key, value]) => `${key}=${value}`).join('\n');
    await api(editorUrl(), { method: 'POST', body, headers: { 'Content-Type': 'text/plain', 'X-RetroArch-Revision': editorRevision } });
    if (!editorProfile) { Object.assign(settingsValues, editorDraft); updateQuick(); }
    await loadEditor(); $('#settings-result').textContent = 'Saved for next launch.'; announce('Settings saved. Restart RetroArch to apply them.');
  } catch (error) { $('#settings-result').textContent = error.message; announce(error.message, true); }
  finally { for (const id of ['settings-profile', 'settings-kind', 'refresh-settings']) $('#' + id).disabled = false; $('#save-settings').disabled = !connected || !editorRevision; $('#settings-fields').disabled = !connected || !editorRevision; }
});
function changeProfile() {
  if (Object.keys(editorDraft).length) { $('#settings-profile').value = editorProfile; $('#settings-kind').value = editorKind; announce('Save your changes or discard them with Refresh before switching profiles.', true); return; }
  editorProfile = $('#settings-profile').value; editorKind = $('#settings-kind').value;
  $('#settings-kind-label').hidden = !editorProfile; $('#settings-search').value = ''; loadEditor();
}
for (const mode of ['guided', 'advanced']) $('#settings-' + mode).addEventListener('click', () => {
  editorMode = mode; editorPage = 0;
  for (const name of ['guided', 'advanced']) $('#settings-' + name).setAttribute('aria-pressed', String(name === mode));
  $('#settings-mode-help').textContent = mode === 'guided' ? 'Clear explanations and ready-to-use choices.' : 'Full configuration with technical names and exact values.';
  renderSettings();
});
$('#settings-profile').addEventListener('change', changeProfile); $('#settings-kind').addEventListener('change', changeProfile);
$('#refresh-settings').addEventListener('click', () => { editorDraft = {}; loadSettings(); });
$('#settings-search').addEventListener('input', () => { editorPage = 0; renderSettings(); });
$('#settings-previous').addEventListener('click', () => { --editorPage; renderSettings(); });
$('#settings-next').addEventListener('click', () => { ++editorPage; renderSettings(); });
for (const [id, key, unit] of [['quick-volume', 'audio_volume', ' dB'], ['quick-rumble', 'input_rumble_gain', '%']]) {
  $('#' + id).addEventListener('input', event => { $(`output[for="${id}"]`).textContent = event.target.value + unit; });
  $('#' + id).addEventListener('change', async event => {
    const input = event.target; input.disabled = true;
    try { await saveSettings({ [key]: input.value }); const full = $('#setting-' + key); if (full) full.value = settingsValues[key]; }
    catch (error) { announce(error.message, true); updateQuick(); }
    finally { input.disabled = !connected; }
  });
}
try { if (localStorage.getItem('retroarch-theme') === 'dark') { document.documentElement.dataset.theme = 'dark'; $('#theme').value = 'dark'; } } catch { /* Browser storage may be disabled. */ }
$('#quick-frontend').addEventListener('change', async event => {
  const input = event.target; input.disabled = true;
  try { await saveSettings({ frontend_start: input.value }); }
  catch (error) { announce(error.message, true); updateQuick(); }
  finally { input.disabled = !connected; }
});
$('#theme').addEventListener('change', event => { document.documentElement.dataset.theme = event.target.value; try { localStorage.setItem('retroarch-theme', event.target.value); } catch { /* Theme still works for this visit. */ } });
// Numeric identifiers and prerelease ordering follow SemVer; alphas are published releases too.
function compareVersions(left, right) {
  const parse = value => /^v?(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$/.exec(value);
  const a = parse(left), b = parse(right); if (!a || !b) return null;
  for (let i = 1; i <= 3; i++) { if (BigInt(a[i]) !== BigInt(b[i])) return BigInt(a[i]) > BigInt(b[i]) ? 1 : -1; }
  if (!a[4] || !b[4]) return a[4] === b[4] ? 0 : !a[4] ? 1 : -1;
  const aa = a[4].split('.'), bb = b[4].split('.');
  for (let i = 0; i < Math.max(aa.length, bb.length); i++) {
    if (aa[i] === bb[i]) continue;
    if (aa[i] === undefined || bb[i] === undefined) return aa[i] === undefined ? -1 : 1;
    const an = /^\d+$/.test(aa[i]), bn = /^\d+$/.test(bb[i]);
    if (an && bn) { if (BigInt(aa[i]) !== BigInt(bb[i])) return BigInt(aa[i]) > BigInt(bb[i]) ? 1 : -1; else continue; }
    if (an !== bn) return an ? -1 : 1;
    return aa[i] > bb[i] ? 1 : -1;
  }
  return 0;
}
let installed = null, latestUpdate = null, updateState = 'idle', updatePolling = false;
function updateButton() {
  const busy = ['downloading', 'verifying', 'ready', 'installing'].includes(updateState);
  $('#download-update').disabled = !connected || !latestUpdate || busy;
  $('#download-update').textContent = busy ? 'Update in progress' : installed && !installed.release ? 'Install latest release' : 'Update RetroArch';
}
function renderUpdate(data) {
  updateState = data.state;
  $('#update-panel').hidden = data.state === 'idle' && $('#update-error').hidden;
  $('#update-tag').textContent = data.tag || '';
  $('#update-development-note').hidden = !installed || Boolean(installed.release);
  const downloading = data.state === 'downloading';
  $('#update-message').textContent = data.message + (downloading && data.received ? ` ${bytes(data.received)} received.` : '');
  $('#update-progress').hidden = !['downloading', 'verifying'].includes(data.state);
  $('#install-update').hidden = data.state !== 'ready';
  $('#install-update').disabled = !connected;
  updateButton();
}
async function loadUpdate() {
  if (updatePolling) return;
  updatePolling = true;
  try { renderUpdate(await api('/api/update')); }
  catch (error) {
    if (updateState === 'installing') {
      $('#update-message').textContent = 'PS5 RetroArch has closed for installation. Reopen it from your launcher when installation finishes, then check the version above.';
    } else if (updateState !== 'idle') {
      $('#update-message').textContent = 'Update status is unavailable. Reconnect to check its progress before retrying.';
    }
  } finally { updatePolling = false; }
}
async function loadAlerts() {
  const button = $('#refresh-alerts'); button.disabled = true;
  try {
    const data = await api('/api/alerts'), list = $('#alerts-list'); list.replaceChildren();
    // What no game of a core runs without first; then what only some games need.
    const draw = (alerts, heading, note) => {
      if (!alerts.length) return;
      const part = element('section', undefined, 'alert-part'); part.append(element('h3', heading, 'alert-part-title'));
      if (note) part.append(element('p', note, 'muted alert-part-note'));
      const groups = new Map();
      for (const alert of alerts) {
        if (!groups.has(alert.core)) { const group = element('section', undefined, 'alert-group'); group.append(element('h4', alert.core)); groups.set(alert.core, group); part.append(group); }
        const row = element('div', undefined, `alert-item ${alert.level === 'some' ? 'some' : 'required'}`), what = element('div', undefined, 'alert-what');
        what.append(element('strong', alert.title));
        if (alert.message) what.append(element('p', alert.message, 'muted'));
        if (alert.any_of?.length) {
          const names = element('p', undefined, 'alert-names'); names.append(document.createTextNode(alert.any_of.length > 1 ? 'Any one of: ' : 'File: '));
          alert.any_of.forEach((name, i) => { if (i) names.append(document.createTextNode(' ')); names.append(element('code', name)); });
          what.append(names);
        }
        row.append(what);
        if (alert.path) { const where = element('div', undefined, 'alert-where'); where.append(element('span', alert.any_of?.length ? 'In the folder' : 'Expected at', 'muted'), element('code', alert.path)); row.append(where); }
        groups.get(alert.core).append(row);
      }
      list.append(part);
    };
    const needed = data.alerts.filter(a => a.level !== 'some'), some = data.alerts.filter(a => a.level === 'some');
    draw(needed, 'Needed', '');
    draw(some, 'Only for some games', 'These cores run most games without them.');
    const count = needed.length;
    $('#alerts-count').textContent = count; $('#alerts-count').hidden = !count;
    $('#alerts-summary').textContent = count ? `${count} ${count === 1 ? 'file is' : 'files are'} needed by your installed cores${some.length ? `, and ${some.length} more only for some games` : ''}.`
      : some.length ? `Nothing required is missing. ${some.length} ${some.length === 1 ? 'file is' : 'files are'} needed only for some games.` : 'No missing required BIOS or system files found.';
    const shown = data.alerts.length;
    $('#alerts-details').hidden = !shown;
    $('.alerts-panel').dataset.state = count ? 'warning' : 'clear';
  } catch (error) { $('#alerts-summary').textContent = 'Couldn’t check required files. Reconnect and try again.'; $('#alerts-details').hidden = true; $('#alerts-count').hidden = true; }
  finally { button.disabled = false; }
}
$('#refresh-alerts').addEventListener('click', loadAlerts);
$('#download-update').addEventListener('click', async () => {
  if (!latestUpdate) return;
  $('#download-update').disabled = true; $('#update-error').hidden = true;
  try { renderUpdate(await api(`/api/update/download?tag=${encodeURIComponent(latestUpdate)}`, { method: 'POST' })); }
  catch (error) { $('#update-panel').hidden = false; $('#update-error').textContent = error.message; $('#update-error').hidden = false; updateButton(); }
});
$('#install-update').addEventListener('click', async () => {
  $('#install-update').disabled = true; $('#update-error').hidden = true;
  renderUpdate({ state: 'installing', tag: $('#update-tag').textContent, message: 'Requesting installation…' });
  try { renderUpdate(await api('/api/update/install', { method: 'POST' })); }
  catch (error) { $('#update-error').textContent = error.message + ' Rechecking installation status…'; $('#update-error').hidden = false; await loadUpdate(); }
});
setInterval(() => { if (!document.hidden && connected) loadUpdate(); }, 2000);
async function checkRelease() {
  const button = $('#check-release'), bar = $('.release-bar'); button.disabled = true;
  latestUpdate = null; updateButton();
  $('#release-summary').textContent = 'Checking releases…'; bar.dataset.state = 'checking';
  try {
    { const response = await fetch('/version.json', { cache: 'no-store', signal: AbortSignal.timeout(10000) }); if (!response.ok) throw new Error(); installed = await response.json(); }
    $('#release-title').textContent = installed.release || 'Development build';
    const response = await fetch(`https://api.github.com/repos/${repo}/releases?per_page=30`, { headers: { Accept: 'application/vnd.github+json' }, signal: AbortSignal.timeout(12000) });
    if (!response.ok) throw new Error();
    const releases = await response.json(); if (!Array.isArray(releases)) throw new Error();
    const published = releases.filter(r => !r.draft && r.published_at && typeof r.tag_name === 'string');
    published.sort((a, b) => { const order = compareVersions(a.tag_name, b.tag_name); return order === null ? Date.parse(b.published_at) - Date.parse(a.published_at) : -order; });
    const latest = published[0];
    if (!latest) { $('#release-summary').textContent = 'No published releases yet'; $('#show-notes').hidden = true; bar.dataset.state = 'unknown'; return; }
    const comparison = installed.release ? compareVersions(installed.release, latest.tag_name) : null;
    const assetNames = new Set((latest.assets || []).map(asset => asset.name));
    const archive = `PS5_RetroArch-${latest.tag_name}.zip`;
    if ((comparison === null || comparison < 0) && assetNames.has(archive) && assetNames.has(archive + '.sha256')) latestUpdate = latest.tag_name;
    bar.dataset.state = comparison === null ? 'development' : comparison < 0 ? 'update' : 'current';
    $('#release-summary').textContent = comparison === null ? `Latest release: ${latest.tag_name}` : comparison < 0 ? `Update available · ${latest.tag_name}` : 'You’re up to date';
    $('.release-icon').replaceWith(uiIcon(comparison !== null && comparison >= 0 ? 'check' : 'info', 'release-icon'));
    $('#notes-title').textContent = `What’s new in ${latest.tag_name}`;
    $('#release-date').textContent = new Date(latest.published_at).toLocaleDateString(undefined, { dateStyle: 'long' });
    $('#release-notes').textContent = latest.body || 'No release notes were provided for this version.';
    $('#release-link').href = `https://github.com/${repo}/releases/tag/${encodeURIComponent(latest.tag_name)}`;
    $('#show-notes').hidden = false;
  } catch { bar.dataset.state = 'unknown'; $('#release-summary').textContent = 'Couldn’t check updates. Try again.'; }
  finally { button.disabled = false; updateButton(); }
}
function showNotes(show) { $('#release-details').hidden = !show; $('#show-notes').setAttribute('aria-expanded', String(show)); if (show) $('#release-details').scrollIntoView({ behavior: 'smooth', block: 'start' }); }
$('#show-notes').addEventListener('click', () => showNotes($('#release-details').hidden));
$('#close-notes').addEventListener('click', () => { showNotes(false); $('#show-notes').focus(); });
$('#check-release').addEventListener('click', checkRelease);
$('#reconnect').addEventListener('click', reconnect);
$('#refresh-content').addEventListener('click', () => loadContent(currentPath));
$('#file-search').addEventListener('input', renderContent);
window.addEventListener('hashchange', navigate);
setInterval(() => { if (!document.hidden) reconnect(); }, 10000);
// The language: the one this browser used last at once, then the console's (webui/i18n.js).
languagePicker($('#language-header'), { compact: true });
languagePicker($('#language-settings'), { inline: true });
if (i18n.remembered() && i18n.remembered() !== 'en') i18n.use(i18n.remembered());
navigate(); drawTransfers(); reconnect(); checkRelease();


// Colorful provider badges, console silhouettes and accurate region flags.
const ICONS = {
  globe: '<svg class="flag globe" viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="9" fill="#2f80ed"/><path d="M3 12h18M12 3c-3 3-3 15 0 18M12 3c3 3 3 15 0 18M5 7h14M5 17h14" fill="none" stroke="#fff" stroke-width="1.2"/></svg>',
  ps5: '<svg class="device" viewBox="0 0 24 24" aria-hidden="true"><path d="M7 2c-2 0-3 1-3 3l1 14c0 2 1 3 3 3h1V2Z" fill="currentColor" opacity=".35"/><path d="M17 2c2 0 3 1 3 3l-1 14c0 2-1 3-3 3h-1V2Z" fill="currentColor" opacity=".35"/><rect x="9.5" y="2" width="5" height="20" rx="1" fill="currentColor"/><circle cx="12" cy="17.5" r=".9" fill="var(--surface)"/></svg>',
  pc: '<svg class="device" viewBox="0 0 24 24" aria-hidden="true"><rect x="2" y="3" width="20" height="13" rx="1.5" fill="none" stroke="currentColor" stroke-width="1.8"/><path d="M9 20h6M12 16v4" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>',
  arrow: '<svg class="device arrow" viewBox="0 0 24 24" aria-hidden="true"><path d="M4 12h15m-5-5 5 5-5 5" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>',
  'source-libretro': '<img class="source-logo" src="assets/retroarch.svg" alt="">',
  'source-screenscraper': '<svg class="source-logo" viewBox="0 0 24 24" aria-hidden="true"><rect x="1" y="1" width="22" height="22" rx="6" fill="#e2463c"/><rect x="5" y="6" width="14" height="9" rx="1.5" fill="none" stroke="#fff" stroke-width="1.8"/><path d="M9 19h6M12 15v4" stroke="#fff" stroke-width="1.8" stroke-linecap="round"/><path d="M8 11.5l2.2-2.2 2 2 1.8-1.8 2 2" fill="none" stroke="#fff" stroke-width="1.4" stroke-linejoin="round"/></svg>',
  'source-launchbox': '<svg class="source-logo" viewBox="0 0 24 24" aria-hidden="true"><rect x="1" y="1" width="22" height="22" rx="6" fill="#2d6cdf"/><path d="M6 9l6-3 6 3v7l-6 3-6-3Z M6 9l6 3 6-3 M12 12v7" fill="none" stroke="#fff" stroke-width="1.6" stroke-linejoin="round"/></svg>',
  'source-emumovies': '<svg class="source-logo" viewBox="0 0 24 24" aria-hidden="true"><rect x="1" y="1" width="22" height="22" rx="6" fill="#f08a24"/><rect x="5" y="6" width="14" height="12" rx="1.5" fill="none" stroke="#fff" stroke-width="1.6"/><path d="M8 6v12M16 6v12" stroke="#fff" stroke-width="1.2"/><path d="M10.5 9.5v5l4-2.5Z" fill="#fff"/></svg>' };
function iconNode(name) {
  if (name.startsWith('flag-')) return assetIcon('assets/flags/' + name.slice(5) + '.svg','flag');
  if (name === 'pc') return uiIcon('monitor','device');
  if (name === 'globe') return uiIcon('globe','flag');
  const template = document.createElement('template'); template.innerHTML = ICONS[name] || ''; return template.content.firstElementChild || uiIcon(name);
}
// A region tag of a game's name, as a flag ("USA", "Europe", "USA, Europe"...).
const REGION_FLAGS = { USA: 'flag-us', Europe: 'flag-eu', Japan: 'flag-jp', Korea: 'flag-kr', World: 'globe', UK:'flag-gb', France:'flag-fr', Germany:'flag-de', Spain:'flag-es', Italy:'flag-it', Australia:'flag-au', Canada:'flag-ca', Brazil:'flag-br', China:'flag-cn', Taiwan:'flag-tw', Russia:'flag-ru' };
function tagNode(tag) {
  const node = element('span', undefined, 'version-tag');
  const flags = tag.split(/,\s*/).map(part => REGION_FLAGS[part]).filter(Boolean);
  if (flags.length && flags.length === tag.split(/,\s*/).length) { for (const flag of flags) node.append(iconNode(flag)); node.title = tag; node.setAttribute('aria-label', tag); node.classList.add('flags'); }
  else node.textContent = tag;
  return node;
}
// The page's own [data-icon] spans get their drawing once.
for (const span of $$('[data-icon]')) {
  const name = span.dataset.icon, label = span.textContent;
  span.replaceChildren();
  span.append(iconNode(name === 'pc-to-ps5' ? 'pc' : name)); // the words say where it goes
  span.append(element('b', label));
}
// The games and their media (src/scraper.h): the shared library every frontend reads.
const KIND_NAMES = { cover: 'Box art', backcover: 'Back', box3d: '3D box', screenshot: 'Screenshot', title: 'Title', logo: 'Logo', physical: 'Disc', fanart: 'Fan art', manual: 'Manual', video: 'Video' };
function gameName(game) { return game.name || game.label; }
// What tells versions of one game apart: the (region), (Rev 1), (Proto), [a1], (1992)
// tags of its label or, failing that, of its file name. Two cards with one title are
// two versions, and their tags say which.
function gameTags(game) {
  const tags = [], text = /[(\[]/.test(game.label) ? game.label : game.key;
  for (const match of text.matchAll(/\(([^)]*)\)|\[([^\]]*)\]/g)) tags.push((match[1] ?? match[2]).trim());
  return tags.filter(Boolean);
}
function visibleGames() {
  $('#games-system-icon').replaceChildren(systemIcon($('#games-system').value));
  const system = $('#games-system').value, words = $('#games-search').value.trim().toLowerCase(), missing = $('#games-missing').checked;
  const letter = $('#games-letters [aria-pressed="true"]')?.dataset.letter || '';
  const sort = $('#games-sort').value, direction = $('#games-direction').dataset.direction === 'desc' ? -1 : 1;
  const collator = new Intl.Collator(undefined, { numeric: true, sensitivity: 'base' });
  function sortValue({ system, game }) {
    if (sort === 'name') return gameName(game);
    if (sort === 'system') return system.name;
    const value = (game[sort] || '').trim();
    if (!value) return null;
    if (sort === 'rating') return Number.isFinite(Number(value)) ? Number(value) : null;
    // Sources store ISO dates or just a year. Compare partial dates without a timezone.
    if (sort === 'released') return /^\d{4}(?:-\d{2}(?:-\d{2})?)?$/.test(value) && Number(value.slice(0, 4)) > 0 ? Number(value.replaceAll('-', '').padEnd(8, '0')) : null;
    return value;
  }
  const out = [];
  for (const s of library.systems) {
    if (system && s.id !== system) continue;
    for (const game of s.games) {
      const initial = gameName(game).trim().normalize('NFD').replace(/[\u0300-\u036f]/g, '').charAt(0).toUpperCase();
      if (letter && letter !== (/^[A-Z]$/.test(initial) ? initial : '#')) continue;
      if (words && !gameName(game).toLowerCase().includes(words) && !game.label.toLowerCase().includes(words)) continue;
      if (missing && game.media.includes('cover') && game.media.includes('screenshot')) continue;
      out.push({ system: s, game });
    }
  }
  return out.sort((a, b) => {
    const av = sortValue(a), bv = sortValue(b);
    if (av === null || bv === null) {
      if (av !== bv) return av === null ? 1 : -1;
    } else {
      const order = typeof av === 'number' ? av - bv : collator.compare(av, bv);
      if (order) return order * direction;
    }
    return collator.compare(gameName(a.game), gameName(b.game)) || collator.compare(a.game.path, b.game.path);
  });
}
function mediaUrl(system, game, kind) {
  return `/api/library/media?system=${encodeURIComponent(system)}&game=${encodeURIComponent(game.key)}&kind=${kind}&v=${encodeURIComponent(game.scraped || '')}`;
}
function gamesView() { return $('input[name="games-view"]:checked')?.value || 'cover'; }
function drawGames() {
  const grid = $('#games-grid'), list = visibleGames(), view = gamesView();
  grid.dataset.view = view;
  grid.replaceChildren();
  const total = library.systems.reduce((n, s) => n + s.games.length, 0);
  $('#games-results').textContent = `${list.length.toLocaleString()} of ${total.toLocaleString()} games`;
  $('#games-sort-hint').hidden = ['name', 'system'].includes($('#games-sort').value);
  const withCovers = library.systems.reduce((n, s) => n + s.games.filter(g => g.media.includes('cover')).length, 0);
  $('#games-summary').textContent = `${total} games in ${library.systems.length} systems · ${withCovers} with covers · shared by RetroArch, EmulationStation and every frontend`;
  if (!list.length) { grid.append(element('p', total ? 'No game matches.' : 'No games yet. Add content or scan it in RetroArch, then refresh.', 'list-message')); }
  for (const { system, game } of list.slice(0, shownGames)) {
    const card = element('article', undefined, 'game-card'), cover = element('div', undefined, 'game-cover'), info = element('div', undefined, 'game-info');
    const pick = document.createElement('input'); pick.type = 'checkbox'; pick.className = 'game-select';
    pick.setAttribute('aria-label', `Select ${gameName(game)}`); pick.checked = selectedGames.has(game.path);
    card.setAttribute('aria-selected', pick.checked);
    pick.addEventListener('change', () => { if (pick.checked) selectedGames.set(game.path, system.id); else selectedGames.delete(game.path); card.setAttribute('aria-selected', pick.checked); drawSelection(); });
    // The picture the view asks for (box art, 3D box, logo, cartridge or disc).
    if (game.media.includes(view)) {
      const img = document.createElement('img'); img.loading = 'lazy'; img.alt = `${gameName(game)} ${KIND_NAMES[view] || view}`; img.src = mediaUrl(system.id, game, view);
      img.addEventListener('error', () => img.replaceWith(svgIcon(KIND_ICONS[view]))); cover.append(img);
    } else { cover.classList.add('none'); cover.append(svgIcon(KIND_ICONS[view])); }
    const badges = element('div', undefined, 'media-badges');
    for (const kind of game.media) badges.append(element('span', KIND_NAMES[kind] || kind));
    const tags = element('div', undefined, 'version-tags');
    for (const tag of gameTags(game)) tags.append(tagNode(tag));
    card.title = `${game.path.split('/').pop()} · open to see and change its media`;
    const platform = element('small', undefined, 'game-platform'); platform.append(systemIcon(system.id),document.createTextNode(system.name));
    info.append(element('strong', gameName(game)), tags, platform, badges);
    // The card opens the game's media; its checkbox still selects it for a download.
    card.tabIndex = 0; card.setAttribute('aria-haspopup', 'dialog');
    card.addEventListener('click', event => { if (event.target !== pick) openGame(system, game); });
    card.addEventListener('keydown', event => { if ((event.key === 'Enter' || event.key === ' ') && event.target === card) { event.preventDefault(); openGame(system, game); } });
    card.append(pick, cover, info); grid.append(card);
  }
  if (list.length > shownGames) {
    const more = element('button', `Show ${Math.min(300, list.length - shownGames)} more of ${list.length - shownGames}`, 'secondary games-more');
    more.addEventListener('click', () => { shownGames += 300; drawGames(); }); grid.append(more);
  }
  drawSelection();
}
function drawSelection() {
  $('#games-selection').textContent = selectedGames.size ? `${selectedGames.size} game${selectedGames.size === 1 ? '' : 's'} selected` : 'Select games to scrape them, or scrape whole systems.';
  $('#scrape-scope-selected').textContent = `Selected games (${selectedGames.size})`;
}
async function loadGames() {
  try {
    library = await api('/api/library', { signal: AbortSignal.timeout(60000) });
    const select = $('#games-system'), chosen = select.value;
    select.replaceChildren(new Option('All systems', ''));
    for (const s of library.systems) select.add(new Option(`${s.name} (${s.games.length})`, s.id));
    if ([...select.options].some(o => o.value === chosen)) select.value = chosen;
    drawGames();
  } catch (error) { $('#games-grid').replaceChildren(element('p', error.message, 'list-message inline-error')); }
}
for (const id of ['#games-system', '#games-missing']) $(id).addEventListener('change', () => { shownGames = 300; drawGames(); });
for (const letter of ['', '#', ...'ABCDEFGHIJKLMNOPQRSTUVWXYZ']) {
  const button = element('button', letter || 'All', 'chip'); button.type = 'button'; button.dataset.letter = letter;
  button.setAttribute('aria-pressed', String(!letter));
  if (letter === '#') button.setAttribute('aria-label', 'Numbers and other characters');
  button.addEventListener('click', () => {
    for (const other of $$('#games-letters button')) other.setAttribute('aria-pressed', String(other === button));
    shownGames = 300; drawGames();
  });
  $('#games-letters').append(button);
}
function setGamesDirection(direction) {
  $('#games-direction').dataset.direction = direction;
  $('#games-direction').replaceChildren(uiIcon(direction === 'desc' ? 'arrow-down' : 'arrow-up'), document.createTextNode(direction === 'desc' ? 'Descending' : 'Ascending'));
}
try {
  const sort = localStorage.getItem('ps5-games-sort');
  if ([...$('#games-sort').options].some(option => option.value === sort)) $('#games-sort').value = sort;
  setGamesDirection(localStorage.getItem('ps5-games-direction') === 'desc' ? 'desc' : 'asc');
} catch {}
function changeGamesSort() {
  try {
    localStorage.setItem('ps5-games-sort', $('#games-sort').value);
    localStorage.setItem('ps5-games-direction', $('#games-direction').dataset.direction);
  } catch {}
  shownGames = 300; drawGames();
}
$('#games-sort').addEventListener('change', changeGamesSort);
$('#games-direction').addEventListener('click', () => {
  setGamesDirection($('#games-direction').dataset.direction === 'asc' ? 'desc' : 'asc'); changeGamesSort();
});

// Media Inspector: one preview, editable details, and game-bound file transfers.
let sheetGame = null, sheetBusy = false;
const DETAIL_NAMES = [['name', 'Game name'], ['developer', 'Developer'], ['publisher', 'Publisher'], ['released', 'Release date'], ['genre', 'Genre'], ['players', 'Players'], ['rating', 'Rating (%)'], ['description', 'Description']];
function sheetQuery(context = sheetGame) {
  return new URLSearchParams({ system: context.system.id, game: context.game.key, path: context.game.path }).toString();
}
async function openGame(system, game) {
  const context = sheetGame = { system, game, info: null, tab: 'media', selected: 'cover' };
  const dialog = $('#game-sheet');
  $('#sheet-title').textContent = gameName(game);
  $('#sheet-tags').replaceChildren(...gameTags(game).map(tagNode));
  $('#sheet-sub').replaceChildren(systemIcon(system.id), document.createTextNode(`${system.name} · ${game.path.split('/').pop()}`));
  $('#sheet-details').hidden = true;
  $('#sheet-media-panel').hidden = false; $('#sheet-files').hidden = true;
  $('#sheet-thumbs').replaceChildren();
  $('#sheet-media').replaceChildren(element('p', 'Loading…', 'list-message'));
  drawSheetCover();
  if (!dialog.open) dialog.showModal();
  try {
    const info = await api(`/api/library/game?${sheetQuery(context)}`, { signal: AbortSignal.timeout(60000) });
    if (sheetGame !== context) return;
    context.info = info; drawSheet();
  } catch (error) {
    if (sheetGame === context) $('#sheet-media').replaceChildren(element('p', error.message, 'list-message inline-error'));
  }
}
function sheetMediaUrl(kind) {
  const { info } = sheetGame, entry = info?.kinds.find(k => k.id === kind);
  return `/api/library/media?${sheetQuery()}&kind=${kind}&v=${entry?.changed || ''}`;
}
function drawSheetCover() {
  const box = $('#sheet-cover'), { system, game, info } = sheetGame;
  const has = info ? info.kinds.find(k => k.id === 'cover')?.present : game.media.includes('cover');
  if (!has) { box.replaceChildren(icon('file')); return; }
  const img = document.createElement('img'); img.alt = ''; img.src = info ? sheetMediaUrl('cover') : mediaUrl(system.id, game, 'cover');
  img.addEventListener('error', () => img.replaceWith(icon('file'))); box.replaceChildren(img);
}
function drawSheet() {
  const { info } = sheetGame;
  $('#sheet-title').textContent = info.details.name || gameName(sheetGame.game);
  $('#sheet-media-count').textContent = `${info.kinds.filter(k => k.present).length} of ${info.kinds.length} types`;
  const selected = info.kinds.find(k => k.id === sheetGame.selected) || info.kinds[0];
  $('#sheet-media').replaceChildren(mediaRow(selected));
  $('#sheet-thumbs').replaceChildren(...info.kinds.map(kind => {
    const button = element('button', undefined, 'sheet-thumb'); button.type = 'button';
    button.setAttribute('aria-pressed', String(kind.id === selected.id));
    const picture = element('span', undefined, 'thumb-picture');
    if (kind.present && kind.id !== 'video' && kind.type !== 'pdf') {
      const img = document.createElement('img'); img.src = sheetMediaUrl(kind.id); img.alt = ''; img.loading = 'lazy';
      img.addEventListener('error', () => img.replaceWith(svgIcon(KIND_ICONS[kind.id] || KIND_ICONS.cover))); picture.append(img);
    } else picture.append(svgIcon(KIND_ICONS[kind.id] || KIND_ICONS.cover));
    button.append(picture, element('span', kind.name), element('small', kind.present ? 'Available' : 'Missing'));
    button.addEventListener('click', () => { if (!sheetBusy) { sheetGame.selected = kind.id; drawSheet(); } });
    return button;
  }));
  drawSheetCover();
  selectSheetTab(sheetGame.tab);
}
function selectSheetTab(tab) {
  if (sheetBusy || !sheetGame?.info) return;
  sheetGame.tab = tab;
  for (const button of $$('#sheet-nav button')) button.setAttribute('aria-pressed', String(button.dataset.tab === tab));
  $('#sheet-media-panel').hidden = tab !== 'media';
  $('#sheet-details').hidden = tab !== 'details';
  $('#sheet-files').hidden = tab !== 'files';
  if (tab === 'details') drawSheetDetails();
  if (tab === 'files') loadSheetFiles();
}
for (const button of $$('#sheet-nav button')) button.addEventListener('click', () => selectSheetTab(button.dataset.tab));
$('#sheet-edit').addEventListener('click', () => selectSheetTab('details'));
function drawSheetDetails() {
  const context = sheetGame, form = element('form', undefined, 'sheet-editor');
  form.append(element('h3', 'Edit game details'), element('p', 'Your changes are kept when media and details are downloaded again.', 'muted sheet-hint'));
  const fields = element('div', undefined, 'sheet-fields');
  for (const [key, title] of DETAIL_NAMES) {
    const label = element('label', title, key === 'description' || key === 'name' ? 'wide-field' : '');
    const input = document.createElement(key === 'description' ? 'textarea' : 'input');
    input.name = key; input.maxLength = key === 'description' ? 12000 : 512;
    input.value = key === 'name' ? context.info.details.name || gameName(context.game) : context.info.details[key] || '';
    if (key === 'name') input.required = true;
    if (key === 'rating') { input.type = 'number'; input.min = 0; input.max = 100; input.step = 'any'; input.value = input.value === '' ? '' : String(Number(input.value) * 100); }
    if (context.draft) input.value = context.draft[key];
    if (key === 'released') input.placeholder = 'YYYY-MM-DD, YYYY-MM or YYYY';
    label.append(input); fields.append(label);
  }
  const actions = element('div', undefined, 'sheet-actions'), save = element('button', 'Save changes', 'primary'), cancel = element('button', 'Cancel', 'secondary');
  save.type = 'submit'; cancel.type = 'button'; cancel.addEventListener('click', () => { context.draft = null; selectSheetTab('media'); });
  const status = element('p', '', 'sheet-status'); status.setAttribute('role', 'status');
  actions.append(save, cancel);
  form.append(fields, element('p', 'The display name does not rename the game backup or change its system.', 'muted sheet-hint'), actions, status);
  form.addEventListener('input', () => { context.draft = Object.fromEntries(new FormData(form)); });
  form.addEventListener('submit', async event => {
    event.preventDefault(); if (sheetBusy) return;
    const values = Object.fromEntries(new FormData(form));
    if (values.rating !== '') values.rating = String(Number(values.rating) / 100);
    sheetBusy = true; save.disabled = cancel.disabled = true; status.textContent = 'Saving…';
    try {
      const info = await api(`/api/library/game?${sheetQuery(context)}`, { method: 'POST', body: JSON.stringify(values) });
      context.info = info; context.draft = null; Object.assign(context.game, info.details); context.game.title = info.details.name;
      $('#sheet-title').textContent = info.details.name; drawGames(); status.textContent = 'Details saved.';
    } catch (error) { status.textContent = error.message; status.classList.add('inline-error'); }
    finally { sheetBusy = false; save.disabled = cancel.disabled = false; }
  });
  $('#sheet-details').replaceChildren(form);
}
async function loadSheetFiles() {
  const context = sheetGame, box = $('#sheet-files');
  box.replaceChildren(element('p', 'Finding game files…', 'muted'));
  try {
    const data = await api(`/api/library/files?${sheetQuery(context)}`, { signal: AbortSignal.timeout(60000) });
    if (sheetGame !== context || context.tab !== 'files') return;
    context.files = data;
    box.replaceChildren(element('h3', 'Game files'), element('p', 'Close this game on the console before replacing or importing files. Export first to keep a copy.', 'muted sheet-hint'));
    if (data.warning) box.append(element('p', data.warning, 'inline-error'));
    for (const [kind, title, hint] of [['rom', 'Game backup (ROM)', 'Your legally obtained backup. The existing filename is kept.'], ['save', 'In-game save', 'Saved progress files. Import the native format used by your core.'], ['state', 'RetroArch savestate', 'Use the same game and compatible core version. Select a file or a new slot.']]) {
      const group = element('section', undefined, 'sheet-file-group'); group.dataset.kind = kind;
      group.append(element('h4', title), element('p', hint, 'muted sheet-hint'));
      const list = kind === 'rom' ? [data.rom] : data[kind], select = document.createElement('select');
      select.setAttribute('aria-label', `${title} file`);
      for (const file of list) select.add(new Option(`${file.display} · ${bytes(file.bytes)}`, file.path));
      if (kind !== 'rom' && data[kind + 'Folder']) select.add(new Option(`Import a new ${kind === 'state' ? 'slot' : 'save file'}`, ''));
      if (!select.options.length) select.add(new Option('No matching files found', ''));
      const slotLabel = element('label', 'Slot (0 is the default, -1 is automatic)', 'sheet-slot'), slot = document.createElement('input');
      slot.type = 'number'; slot.min = -1; slot.max = 999999; slot.value = 0; slotLabel.append(slot);
      const destination = element('p', '', 'muted sheet-path');
      const preview = element('div', undefined, 'state-preview'); preview.hidden = kind !== 'state';
      const actions = element('div', undefined, 'sheet-actions'), upload = element('button', kind === 'rom' ? 'Replace…' : 'Import…', 'secondary'), download = element('a', 'Export', 'secondary');
      upload.type = 'button'; const input = document.createElement('input'); input.type = 'file'; input.hidden = true;
      if (kind === 'rom') input.accept = '.' + data.rom.name.split('.').pop();
      if (kind === 'save') input.accept = '.srm,.sav,.dsv,.rtc,.eep,.fla,.sra,.mpk';
      const status = element('p', '', 'sheet-status'); status.setAttribute('role', 'status');
      const refresh = () => {
        const file = list.find(f => f.path === select.value);
        slotLabel.hidden = kind !== 'state' || !!file;
        if (kind === 'state') {
          preview.replaceChildren();
          if (file?.preview) {
            const image = document.createElement('img'); image.alt = 'Screenshot of ' + file.name;
            image.src = `/api/library/file?${sheetQuery(context)}&kind=state&file=${encodeURIComponent(file.path)}&preview=1`;
            const enlarge = element('button', undefined, 'state-preview-image'); enlarge.type = 'button'; enlarge.setAttribute('aria-label', 'Enlarge savestate screenshot'); enlarge.append(image);
            enlarge.addEventListener('click', () => openLightbox(image.src, {name:'Savestate screenshot'})); preview.append(enlarge);
            image.addEventListener('error', () => preview.replaceChildren(element('p', 'Screenshot unavailable. Refresh to try again.', 'muted')));
          } else preview.append(uiIcon('image'), element('p', file ? 'No screenshot for this slot. Enable Savestate Thumbnails in RetroArch and save the state again.' : 'A screenshot appears here when a saved slot has a thumbnail.', 'muted'));
        }
        destination.textContent = file ? file.display : data[kind + 'Folder'] ? `Destination: ${data[kind + 'Folder']}` : 'Launch this game with its core and save once, then refresh to locate its files.';
        upload.disabled = !file && !data[kind + 'Folder'];
        if (file) { download.href = `/api/library/file?${sheetQuery(context)}&kind=${kind}&file=${encodeURIComponent(file.path)}`; download.removeAttribute('aria-disabled'); }
        else { download.removeAttribute('href'); download.setAttribute('aria-disabled', 'true'); }
      };
      select.addEventListener('change', refresh); refresh();
      upload.addEventListener('click', () => { if (!sheetBusy) input.click(); });
      input.addEventListener('change', async () => {
        const file = input.files[0]; input.value = ''; if (!file || sheetBusy) return;
        if (!file.size) { status.textContent = 'Choose a non-empty file.'; return; }
        let target = select.value;
        if (kind === 'rom' && file.name.split('.').pop().toLowerCase() !== data.rom.name.split('.').pop().toLowerCase()) { status.textContent = 'Choose a backup with the same file extension.'; return; }
        if (!target) {
          if (kind === 'state') {
            if (!slot.checkValidity() || slot.value === '') { slot.reportValidity(); return; }
            target = data.stem + '.state' + (Number(slot.value) === -1 ? '.auto' : Number(slot.value) === 0 ? '' : Number(slot.value));
          } else target = data.stem + '.' + file.name.split('.').pop().toLowerCase();
        }
        const replacement = !!select.value;
        if (!confirm(`${replacement ? 'Replace ' : 'Import to '}${target}?\n\nClose this game on the console first.${replacement ? '\nThe existing file will be overwritten. Export it first if you need a copy.' : ''}`)) return;
        sheetBusy = true; select.disabled = upload.disabled = true;
        const progress = document.createElement('progress'); progress.max = 100; progress.value = 0; progress.setAttribute('aria-label', 'Upload progress'); group.append(progress);
        const cancel = element('button', 'Cancel upload', 'secondary'); cancel.type = 'button'; actions.append(cancel);
        try {
          await new Promise((resolve, reject) => {
            const request = new XMLHttpRequest();
            request.open('PUT', `/api/library/file?${sheetQuery(context)}&kind=${kind}&file=${encodeURIComponent(target)}${replacement ? '&existing=replace' : ''}`);
            request.setRequestHeader('X-RetroArch-Token', token);
            request.upload.addEventListener('progress', e => { if (e.lengthComputable) { progress.value = e.loaded / e.total * 100; status.textContent = `${Math.round(progress.value)}% · ${bytes(e.loaded)} of ${bytes(e.total)}`; } });
            cancel.addEventListener('click', () => request.abort());
            request.addEventListener('load', () => { let result = {}; try { result = JSON.parse(request.responseText); } catch {} request.status === 201 ? resolve() : reject(new Error(result.error || 'Upload failed. Try again.')); });
            request.addEventListener('error', () => reject(new Error('Connection lost. Refresh to check whether the upload completed.')));
            request.addEventListener('abort', () => reject(new Error('Upload cancelled. Refresh to check its status.')));
            request.send(file);
          });
          sheetBusy = false; await loadSheetFiles();
          $('#sheet-files').prepend(element('p', `${title} stored successfully.`, 'sheet-status'));
        } catch (error) { status.textContent = error.message; status.classList.add('inline-error'); }
        finally { sheetBusy = false; select.disabled = false; refresh(); progress.remove(); cancel.remove(); }
      });
      actions.append(upload, download, input); group.append(select, destination, preview, slotLabel, actions, status); box.append(group);
    }
    box.append(element('p', 'File transfers preserve bytes; they do not convert save formats. Cores with private save directories and multi-file disc sets may need the Content browser or FTP.', 'muted sheet-hint'));
  } catch (error) { if (sheetGame === context) box.replaceChildren(element('p', error.message, 'inline-error')); }
}
function mediaRow(kind) {
  const row = element('div', undefined, `media-row-item${kind.present ? '' : ' missing'}`), stage = element('div', undefined, 'media-stage');
  row.dataset.kind = kind.id;
  if (!kind.present) stage.append(svgIcon(KIND_ICONS[kind.id] || KIND_ICONS.cover), element('span', `No ${kind.name.toLowerCase()} yet · drop a file here`));
  else if (kind.id === 'video') {
    // The player at full size, with a big play button until it starts.
    stage.classList.add('video');
    const video = document.createElement('video'); video.src = sheetMediaUrl(kind.id); video.preload = 'metadata'; video.playsInline = true; video.controls = true;
    const play = element('button', undefined, 'play'); play.type = 'button'; play.setAttribute('aria-label', 'Play the video');
    play.append(uiIcon('video'));
    play.addEventListener('click', () => { video.play(); });
    video.addEventListener('play', () => { play.hidden = true; });
    video.addEventListener('pause', () => { play.hidden = false; });
    stage.append(video, play);
  } else if (kind.type === 'pdf') {
    const a = element('a', undefined, 'pdf'); a.href = sheetMediaUrl(kind.id); a.target = '_blank'; a.rel = 'noopener';
    a.append(uiIcon('book-open')); a.append(element('span', 'Open the manual (PDF)'));
    stage.append(a);
  } else {
    const img = document.createElement('img'); img.alt = kind.name; img.src = sheetMediaUrl(kind.id);
    img.addEventListener('error', () => img.replaceWith(svgIcon(KIND_ICONS[kind.id] || KIND_ICONS.cover)));
    img.addEventListener('click', () => openLightbox(img.src, kind));
    stage.append(img);
  }
  const side = element('div', undefined, 'media-side'), title = element('strong');
  title.append(svgIcon(KIND_ICONS[kind.id] || KIND_ICONS.cover), element('span', kind.name));
  const state = kind.present ? element('span', `${kind.type.toUpperCase()} · ${bytes(kind.bytes)}${kind.uploaded ? ' · your file, kept when downloading again' : ''}`, `state${kind.uploaded ? ' mine' : ''}`)
    : element('span', `Missing · takes ${kind.accepts.join(', ').toUpperCase()}`, 'state');
  const actions = element('div', undefined, 'actions'), choose = element('button', kind.present ? 'Replace…' : 'Add…', 'secondary');
  choose.type = 'button';
  const input = document.createElement('input'); input.type = 'file'; input.hidden = true;
  input.accept = kind.accepts.map(type => '.' + type).join(',');
  choose.addEventListener('click', () => input.click());
  input.addEventListener('change', () => { if (input.files[0]) uploadMedia(kind, input.files[0], row); input.value = ''; });
  if (kind.present && kind.id !== 'video' && kind.type !== 'pdf') {
    const enlarge = element('button', 'Enlarge', 'secondary'); enlarge.type = 'button';
    enlarge.addEventListener('click', () => openLightbox(sheetMediaUrl(kind.id), kind)); actions.append(enlarge);
  }
  actions.append(choose, input);
  side.append(title, element('p', kind.description, 'muted'), state, actions);
  // A file dropped on the row replaces that kind.
  row.addEventListener('dragover', event => { event.preventDefault(); row.classList.add('drop'); });
  row.addEventListener('dragleave', () => row.classList.remove('drop'));
  row.addEventListener('drop', event => { event.preventDefault(); row.classList.remove('drop'); const file = event.dataTransfer.files[0]; if (file) uploadMedia(kind, file, row); });
  row.append(stage, side);
  return row;
}
// A picture at full size over the page; a click or Escape closes it.
function openLightbox(src, kind) {
  const box = element('div', undefined, 'lightbox'), img = document.createElement('img');
  img.src = src; img.alt = kind.name; box.append(img, element('p', `${kind.name} · click or press Escape to close`));
  box.addEventListener('click', () => box.remove());
  $('#game-sheet').append(box);
}
function uploadMedia(kind, file, row) {
  if (sheetBusy) return;
  const context = sheetGame;
  const type = (file.name.split('.').pop() || '').toLowerCase();
  const state = row.querySelector('.state');
  if (!kind.accepts.includes(type)) { state.textContent = `${kind.name} takes ${kind.accepts.join(', ').toUpperCase()} files (what every frontend can show).`; state.className = 'state inline-error'; return; }
  const { system, game } = sheetGame, bar = document.createElement('progress'); bar.max = 100; bar.value = 0;
  sheetBusy = true;
  row.classList.add('busy'); state.textContent = `Sending ${file.name} · ${bytes(file.size)}`; state.className = 'state'; state.after(bar);
  const request = new XMLHttpRequest();
  request.open('PUT', `/api/library/media?system=${encodeURIComponent(system.id)}&game=${encodeURIComponent(game.key)}&kind=${kind.id}&type=${encodeURIComponent(type)}`);
  request.setRequestHeader('X-RetroArch-Token', token);
  request.upload.addEventListener('progress', event => { if (event.lengthComputable) bar.value = event.loaded / event.total * 100; });
  request.addEventListener('load', () => {
    sheetBusy = false;
    let data = {}; try { data = JSON.parse(request.responseText); } catch {}
    if (request.status !== 201) { row.classList.remove('busy'); bar.remove(); state.textContent = data.error || `The upload failed (${request.status}). Try again.`; state.className = 'state inline-error'; return; }
    if (sheetGame !== context) return;
    context.info = data;
    // The card and its media list follow, for every frontend reads the same file.
    game.media = data.kinds.filter(k => k.present).map(k => k.id); game.scraped = String(Date.now());
    drawSheet(); drawGames();
    announce(`${kind.name} of ${gameName(game)} replaced.`);
  });
  request.addEventListener('error', () => { sheetBusy = false; row.classList.remove('busy'); bar.remove(); state.textContent = 'The connection was lost. Try again.'; state.className = 'state inline-error'; });
  request.send(file);
}
function closeSheet() {
  if (sheetBusy || (sheetGame?.draft && !confirm(i18n.t('Discard unsaved game details?')))) return;
  $('#game-sheet').close();
}
$('#sheet-close').addEventListener('click', closeSheet);
$('#game-sheet').addEventListener('click', event => { if (event.target === $('#game-sheet')) closeSheet(); });
$('#game-sheet').addEventListener('close', () => { for (const v of $$('#sheet-media video')) v.pause(); });
// Escape closes a full-size picture first, then the game.
$('#game-sheet').addEventListener('cancel', event => { if (sheetBusy) { event.preventDefault(); return; } const box = $('#game-sheet .lightbox'); event.preventDefault(); if (box) box.remove(); else closeSheet(); });
$('#games-search').addEventListener('input', () => { shownGames = 300; drawGames(); });
// The cards' picture, remembered in this browser.
try { const saved = localStorage.getItem('ps5-games-view'); const radio = saved && $(`input[name="games-view"][value="${saved}"]`); if (radio) radio.checked = true; } catch {}
for (const radio of $$('input[name="games-view"]')) radio.addEventListener('change', () => { try { localStorage.setItem('ps5-games-view', radio.value); } catch {} drawGames(); });
$('#refresh-games').addEventListener('click', loadGames);

// The Download Media tab: a few choices, a live summary, and the job's progress.
const METHOD_NAMES = { pc: 'Downloaded on this PC, then transferred to PS5', ps5: 'Downloaded on the PS5' };
const METHOD_HINTS = {
  ps5: 'Your PS5 downloads everything itself, straight to its storage. You can close this page; the job keeps going.',
  pc: 'Downloads on this PC first, then transfers each file to your PS5. A small helper program on this PC does both; keep it running until the job is done (this page can close).',
  '': 'PS5: the console downloads directly. This PC → PS5: this PC downloads, then transfers to the PS5. Your choice is remembered on this console.' };
const RECOMMENDED = ['cover', 'screenshot', 'title'];
const KIND_ICONS = {cover:'book-image', backcover:'book-open', box3d:'box', screenshot:'image', title:'panels-top-left', logo:'type', physical:'disc-3', fanart:'wallpaper', manual:'book-open', video:'video', details:'list'};
const DETAILS_TEXT = 'Description, developer, publisher, release date, genre, players and rating, shown in EmulationStation and on each game here.';
function detailsOn() { return scrapeDetails && !!chosenSource()?.details; }
function drawDetails() {
  const source = chosenSource(), tile = element('button', undefined, 'details-tile'); tile.type = 'button';
  tile.setAttribute('aria-pressed', detailsOn()); tile.disabled = !source.details;
  const tick = element('span', undefined, 'tick'); tick.append(uiIcon('check'));
  tile.append(svgIcon(KIND_ICONS.details), element('strong', 'Game details'), source.details ? tick : element('em', `Not from ${source.name}`, 'soon-tag'), element('small', DETAILS_TEXT));
  const hint = source.details ? `Game details: ${DETAILS_TEXT} Text only, so it costs almost nothing; details you edited yourself are never replaced.` : `Game details: ${source.name} has pictures only. ScreenScraper has details.`;
  tile.title = hint;
  for (const event of ['mouseenter', 'focus']) tile.addEventListener(event, () => { $('#scrape-kind-hint').textContent = hint; });
  tile.addEventListener('click', () => { scrapeDetails = !scrapeDetails; drawDetails(); drawSummary(); });
  $('#scrape-details-box').replaceChildren(tile);
}
// The chain of sources, in order; chosenSource() is what they offer together.
function chainSources() { return scrapeChain.map(id => scraperSettings.sources.find(s => s.id === id)).filter(Boolean); }
function chosenSource() {
  const list = chainSources();
  if (!list.length) return { id: '', ids: [], name: 'no source', kinds: [], details: false, description: 'Turn on at least one source.' };
  return { id: list.map(s => s.id).join(','), ids: list.map(s => s.id), name: list.map(s => s.name).join(' → '),
    kinds: [...new Set(list.flatMap(s => s.kinds))], details: list.some(s => s.details ?? s.id !== 'libretro'), description: list.length === 1 ? list[0].description : '' };
}
function sourcesFor(kind) { return chainSources().filter(s => kind === 'details' ? (s.details ?? s.id !== 'libretro') : s.kinds.includes(kind)).map(s => s.name); }
function pcAllowed() { const ids = chosenSource().ids; return ids.length <= 1 && !ids.includes('screenscraper'); }
function svgIcon(name) { return uiIcon(name); }
// The Games tab's view choices get their drawings (KIND_ICONS is set by now).
for (const span of $$('[data-view-icon]')) span.prepend(svgIcon(KIND_ICONS[span.dataset.viewIcon]));
function drawMethod() {
  const method = $('input[name="scrape-method"]:checked')?.value || '';
  const ids = scraperSettings ? chosenSource().ids : [];
  $('#scrape-method-hint').textContent = METHOD_HINTS[method] + (ids.includes('screenscraper')
    ? ' ScreenScraper downloads run on the PS5 only: its links carry private account details that never leave the console.'
    : ids.length > 1 ? ' Combined sources download on the PS5.' : '');
  drawSummary();
}
function drawSources() {
  const box = $('#scrape-sources'), ordered = [...chainSources(), ...scraperSettings.sources.filter(s => !scrapeChain.includes(s.id))];
  box.replaceChildren();
  const move = (id, by) => { const i = scrapeChain.indexOf(id), j = i + by; if (i < 0 || j < 0 || j >= scrapeChain.length) return; [scrapeChain[i], scrapeChain[j]] = [scrapeChain[j], scrapeChain[i]]; drawSources(); drawKinds(); };
  for (const source of ordered) {
    const on = scrapeChain.includes(source.id), order = scrapeChain.indexOf(source.id);
    const item = element('li', undefined, `source-item ${!source.available ? 'unavailable' : on ? 'on' : 'off'}`); item.dataset.source = source.id;
    const number = element('span', on ? String(order + 1) : '', 'source-order'); number.setAttribute('aria-hidden', 'true');
    const logo = iconNode('source-' + source.id) || element('span');
    const text = element('div', undefined, 'source-text'), name = element('strong');
    name.append(element('span', source.name));
    if (!source.available) name.append(element('em', 'Coming', 'soon-tag'));
    else if (source.account && source.signed_in) name.append(checkNode());
    text.append(name, element('small', source.description));
    // On or off: a source with an account asks for it first.
    let control;
    if (!source.available) control = element('span');
    else if (source.account && !source.signed_in) {
      control = element('button', 'Sign in', 'secondary source-sign'); control.type = 'button';
      control.addEventListener('click', () => openSignIn(source));
    } else {
      control = element('button', undefined, 'source-toggle'); control.type = 'button'; control.setAttribute('role', 'switch');
      control.setAttribute('aria-checked', on); control.setAttribute('aria-label', `Use ${source.name}`);
      control.addEventListener('click', () => { scrapeChain = on ? scrapeChain.filter(id => id !== source.id) : [...scrapeChain, source.id]; drawSources(); drawKinds(); });
    }
    const moves = element('div', undefined, 'source-move');
    const up = element('button'), down = element('button'); up.type = down.type = 'button';
    up.append(uiIcon('chevron-up')); down.append(uiIcon('chevron-down'));
    up.setAttribute('aria-label', `Ask ${source.name} earlier`); down.setAttribute('aria-label', `Ask ${source.name} later`);
    up.disabled = !on || order === 0; down.disabled = !on || order === scrapeChain.length - 1;
    up.addEventListener('click', () => move(source.id, -1)); down.addEventListener('click', () => move(source.id, 1));
    moves.append(up, down);
    item.append(number, logo, text, control, moves);
    // Dragging reorders the sources that are on.
    if (on && scrapeChain.length > 1) {
      item.draggable = true;
      item.addEventListener('dragstart', event => { event.dataTransfer.setData('text/x-source', source.id); item.classList.add('dragging'); });
      item.addEventListener('dragend', () => item.classList.remove('dragging'));
    }
    if (on) {
      item.addEventListener('dragover', event => { if ([...event.dataTransfer.types].includes('text/x-source')) { event.preventDefault(); item.classList.add('over'); } });
      item.addEventListener('dragleave', () => item.classList.remove('over'));
      item.addEventListener('drop', event => {
        event.preventDefault(); item.classList.remove('over');
        const dragged = event.dataTransfer.getData('text/x-source'); if (!dragged || dragged === source.id) return;
        const rest = scrapeChain.filter(id => id !== dragged); rest.splice(rest.indexOf(source.id), 0, dragged); scrapeChain = rest; drawSources(); drawKinds();
      });
    }
    for (const event of ['mouseenter', 'focusin']) item.addEventListener(event, () => { $('#scrape-source-hint').textContent = source.description; });
    box.append(item);
  }
  const chosen = chosenSource(), names = chainSources().map(s => s.name);
  $('#scrape-source-hint').textContent = !names.length ? 'Turn on at least one source.'
    : names.length === 1 ? chosen.description
    : `${names[0]} goes through every game first, at full speed; then ${names.slice(1).join(', then ')} only for what is still missing. Free sources first save the quota of limited ones.`;
  const signed = chainSources().find(s => s.account && s.signed_in);
  $('#scrape-account').hidden = !signed;
  if (signed) { $('#scrape-account-text').textContent = `Signed in to ${signed.name}`; $('#scrape-account-manage').onclick = () => openSignIn(signed); }
  $('#scrape-language-pick').hidden = !chosen.ids.includes('screenscraper');
  // Combined sources and ScreenScraper download on the PS5.
  const pc = $('input[name="scrape-method"][value="pc"]'), pcOff = !pcAllowed();
  pc.disabled = pcOff; pc.closest('label').title = pcOff ? 'Combined sources and ScreenScraper download on the PS5.' : '';
  if (pcOff && pc.checked) { pc.checked = false; $('input[name="scrape-method"][value="ps5"]').checked = true; }
  drawMethod();
}
function checkNode() {
  const badge = element('span', undefined, 'signed-check'); badge.setAttribute('role', 'img'); badge.setAttribute('aria-label', 'Signed in'); badge.append(uiIcon('check')); return badge;
}

// Sign-in: the name and password go to the console in a request body (never in an
// address), are checked with the source there and kept there; answers carry no password.
let signInSource = null;
function showAccount(account) {
  const signed = account.signed_in;
  $('#sign-in-fields').hidden = signed; $('#sign-in-account').hidden = !signed;
  $('#sign-in-forget').hidden = !signed; $('#sign-in-change').hidden = !signed;
  $('#sign-in-submit').textContent = signed ? 'Done' : 'Sign in';
  $('#sign-in-title').textContent = signed ? `Your ${signInSource.name} account` : `Sign in to ${signInSource.name}`;
  if (!signed) return;
  $('#sign-in-who').replaceChildren(checkNode(), element('span', `Signed in as ${account.user}`));
  const rows = [['Downloads today', account.max_requests ? `${Number(account.requests_today || 0).toLocaleString()} of ${Number(account.max_requests).toLocaleString()}` : '—'],
    ['Games at once', account.max_threads || '1'], ['Level', account.level || '—']];
  $('#sign-in-quota').replaceChildren(...rows.flatMap(([k, v]) => [element('dt', k), element('dd', String(v))]));
}
async function openSignIn(source) {
  signInSource = source;
  const dialog = $('#sign-in'), logo = iconNode('source-' + source.id);
  $('#sign-in-logo').replaceChildren(...(logo ? [logo] : []));
  $('#sign-in-error').textContent = ''; $('#sign-in-password').value = '';
  showAccount({ signed_in: source.signed_in });
  dialog.showModal();
  if (source.signed_in) {
    try { showAccount(await api('/api/scraper/account?source=' + encodeURIComponent(source.id))); }
    catch (error) { $('#sign-in-error').textContent = error.message; }
  } else $('#sign-in-user').focus();
}
function signedIn(state) {
  signInSource.signed_in = state;
  if (!scraperSettings) return;
  const source = scraperSettings.sources.find(s => s.id === signInSource.id);
  source.signed_in = signInSource.signed_in = state;
  if (state && !scrapeChain.includes(source.id)) scrapeChain.push(source.id);
  if (!state) scrapeChain = scrapeChain.filter(id => id !== source.id);
  drawSources(); drawKinds();
}
$('#sign-in-form').addEventListener('submit', async event => {
  event.preventDefault();
  const dialog = $('#sign-in');
  if (!$('#sign-in-account').hidden) { dialog.close(); return; }
  const user = $('#sign-in-user').value.trim(), password = $('#sign-in-password').value;
  if (!user || !password) { $('#sign-in-error').textContent = 'Enter your name and password.'; return; }
  dialog.setAttribute('aria-busy', 'true'); $('#sign-in-error').textContent = ''; $('#sign-in-submit').textContent = 'Checking…';
  try {
    const account = await api('/api/scraper/account?source=' + encodeURIComponent(signInSource.id), { method: 'POST', body: `${user}\n${password}`, headers: { 'Content-Type': 'text/plain' }, signal: AbortSignal.timeout(45000) });
    $('#sign-in-password').value = '';
    showAccount(account); signedIn(true);
    announce(`Signed in to ${signInSource.name} as ${account.user}.`);
  } catch (error) { $('#sign-in-error').textContent = error.message; $('#sign-in-submit').textContent = 'Sign in'; }
  finally { dialog.removeAttribute('aria-busy'); }
});
$('#sign-in-cancel').addEventListener('click', () => $('#sign-in').close());
$('#sign-in-change').addEventListener('click', () => { showAccount({ signed_in: false }); $('#sign-in-user').focus(); });
$('#sign-in-forget').addEventListener('click', async () => {
  try {
    await api('/api/scraper/account?source=' + encodeURIComponent(signInSource.id), { method: 'DELETE' });
    $('#sign-in').close(); signedIn(false); announce(`Signed out of ${signInSource.name}. Its password is gone from the console.`);
  } catch (error) { $('#sign-in-error').textContent = error.message; }
});
$('#sign-in').addEventListener('close', () => { $('#sign-in-password').value = ''; });
function drawKinds() {
  const source = chosenSource(), box = $('#scrape-kinds'); box.replaceChildren();
  for (const kind of scraperSettings.catalog) {
    const has = source.kinds.includes(kind.id), tile = element('button', undefined, 'kind-tile'); tile.type = 'button';
    tile.setAttribute('aria-pressed', has && scrapeKinds.has(kind.id)); tile.disabled = !has;
    tile.append(svgIcon(KIND_ICONS[kind.id] || KIND_ICONS.cover), element('span', kind.name));
    if (!has) tile.append(element('em', scraperSettings.sources.some(s => s.kinds.includes(kind.id)) ? 'Coming' : '—', 'soon-tag'));
    const others = scraperSettings.sources.filter(s => !source.ids?.includes(s.id) && s.kinds.includes(kind.id)).map(s => s.name);
    const from = sourcesFor(kind.id);
    const hint = `${kind.name}: ${kind.description}` + (has ? (from.length > 1 ? ` Asked of ${from.join(', then ')}.` : ` From ${from[0]}.`)
      : ` Not from ${source.name || 'the sources on'}${others.length ? `; ${others.join(', ')} will have it` : ''}.`);
    tile.title = hint;
    tile.addEventListener('mouseenter', () => { $('#scrape-kind-hint').textContent = hint; });
    tile.addEventListener('focus', () => { $('#scrape-kind-hint').textContent = hint; });
    tile.addEventListener('click', () => {
      if (scrapeKinds.has(kind.id)) scrapeKinds.delete(kind.id); else scrapeKinds.add(kind.id);
      tile.setAttribute('aria-pressed', scrapeKinds.has(kind.id)); drawSummary();
    });
    box.append(tile);
  }
  drawDetails();
  drawSummary();
}
function setKinds(ids) { scrapeKinds = new Set(ids.filter(id => chosenSource().kinds.includes(id))); drawKinds(); }
function scrapeScope() { return $('input[name="scrape-scope"]:checked')?.value || 'all'; }
function scopeGames() {
  const scope = scrapeScope();
  if (scope === 'selected') return selectedGames.size;
  const system = $('#scrape-system').value;
  return library.systems.filter(s => scope === 'all' || s.id === system).reduce((n, s) => n + s.games.length, 0);
}
function drawSummary() {
  if (!scraperSettings) return;
  const kinds = [...scrapeKinds].filter(id => chosenSource().kinds.includes(id)), games = scopeGames();
  const method = $('input[name="scrape-method"]:checked')?.value;
  $('#scrape-system').hidden = scrapeScope() !== 'system';
  $('#scrape-scope-selected').textContent = `Selected (${selectedGames.size})`;
  const details = detailsOn(), ready = method && chainSources().length && (kinds.length || details) && games;
  $('#scrape-start').disabled = !ready;
  $('#scrape-summary').textContent = !method ? 'Choose where to download from.' : !chainSources().length ? 'Turn on at least one source.' : !kinds.length && !details ? 'Choose at least one kind of media, or Game details.'
    : !games ? (scrapeScope() === 'selected' ? 'Select games on the Games tab, or choose a system.' : 'No games here yet.')
    : `${[kinds.length ? `${kinds.length} kind${kinds.length === 1 ? '' : 's'} of media` : '', details ? 'game details' : ''].filter(Boolean).join(' + ')} · ${games.toLocaleString()} game${games === 1 ? '' : 's'} · ${chosenSource().name} · ${method === 'pc' ? 'downloaded on this PC, then transferred to the PS5' : 'downloaded on the PS5'}${$('#scrape-overwrite').checked ? ' · replacing what you have' : ' · only what’s missing'}`;
}
async function openMedia() {
  try {
    if (!library.systems.length) library = await api('/api/library', { signal: AbortSignal.timeout(60000) });
    scraperSettings = await api('/api/scraper/settings');
  } catch (error) { announce(error.message, true); return; }
  for (const radio of $$('input[name="scrape-method"]')) radio.checked = radio.value === scraperSettings.mode;
  const usable = id => scraperSettings.sources.some(s => s.id === id && s.available && (!s.account || s.signed_in));
  scrapeChain = (scraperSettings.chain || [scraperSettings.source]).filter(usable);
  if (!scrapeChain.length) scrapeChain = ['libretro'];
  $('#scrape-language').value = scraperSettings.language || 'en';
  scrapeKinds = new Set(scraperSettings.kinds); scrapeDetails = scraperSettings.details !== false;
  for (const radio of $$('input[name="scrape-region"]')) radio.checked = radio.value === scraperSettings.region;
  const select = $('#scrape-system'), chosen = select.value || $('#games-system').value;
  select.replaceChildren(...library.systems.map(s => new Option(`${s.name} (${s.games.length})`, s.id)));
  if (chosen) select.value = chosen;
  if (selectedGames.size) $('input[name="scrape-scope"][value="selected"]').checked = true;
  drawMethod(); drawSources(); drawKinds(); loadJob();
}

for (const node of $$('input[name="scrape-method"], input[name="scrape-scope"]')) node.addEventListener('change', () => { drawMethod(); });
$('#scrape-system').addEventListener('change', drawSummary);
const EXISTING_HINTS = {
  keep: 'Only media your games don’t have yet is downloaded: a game with box art gets no box art again. Files on your PS5 and details already filled in stay as they are. Fastest, and uses the least of your daily quota.',
  replace: 'Every kind you chose is downloaded again and replaces the files on your PS5 (and the details), for example to switch region or source. Details you edited yourself are never replaced. Uses more of your daily quota.' };
function drawExisting() { $('#scrape-existing-hint').textContent = EXISTING_HINTS[$('#scrape-overwrite').checked ? 'replace' : 'keep']; drawSummary(); }
for (const radio of $$('input[name="scrape-existing"]')) radio.addEventListener('change', drawExisting);
drawExisting();
$('#kinds-recommended').addEventListener('click', () => setKinds(RECOMMENDED));
$('#kinds-all').addEventListener('click', () => setKinds(chosenSource().kinds));
$('#kinds-none').addEventListener('click', () => setKinds([]));
$('#scrape-start').addEventListener('click', async () => {
  const method = $('input[name="scrape-method"]:checked')?.value;
  if (!method) { $('#scrape-result').textContent = 'Choose where to download from.'; return; }
  const kinds = [...scrapeKinds].filter(id => chosenSource().kinds.includes(id)), scope = scrapeScope();
  const lines = scope === 'selected' ? [...selectedGames].map(([path, system]) => `${system}\t${path}`)
    : library.systems.filter(s => scope === 'all' || s.id === $('#scrape-system').value).map(s => `${s.id}\t`);
  if ((!kinds.length && !detailsOn()) || !lines.length) { drawSummary(); return; }
  const query = new URLSearchParams({ mode: method, source: scrapeChain[0] || '', sources: scrapeChain.join(','), kinds: kinds.join(','), region: $('input[name="scrape-region"]:checked')?.value || 'us', language: $('#scrape-language').value || scraperSettings.language || 'en', overwrite: $('#scrape-overwrite').checked ? '1' : '0', details: detailsOn() ? '1' : '0' });
  $('#scrape-start').disabled = true; $('#scrape-result').textContent = '';
  try {
    await api('/api/scraper/settings?' + query, { method: 'POST' });
    const started = await api('/api/scraper/start?' + query, { method: 'POST', body: lines.join('\n'), headers: { 'Content-Type': 'text/plain' }, signal: AbortSignal.timeout(60000) });
    drawJob(started.job); pollJob(); $('#scrape-job').scrollIntoView({ behavior: 'smooth', block: 'start' });
  } catch (error) { $('#scrape-result').textContent = error.message; }
  finally { drawSummary(); }
});

// The job: polled while this tab is open; the console keeps it whether or not it is.
function count(job, ...states) { return states.reduce((n, s) => n + (job.counts[s] || 0), 0); }
function countTo(node, value) {
  const from = Number(node.dataset.value || 0); node.dataset.value = value;
  if (from === value || matchMedia('(prefers-reduced-motion: reduce)').matches) { node.textContent = value.toLocaleString(); return; }
  const start = performance.now();
  const step = now => { const k = Math.min(1, (now - start) / 400); node.textContent = Math.round(from + (value - from) * k).toLocaleString(); if (k < 1) requestAnimationFrame(step); };
  requestAnimationFrame(step);
}
function drawJob(job) {
  const panel = $('#scrape-job');
  if (!job) { panel.hidden = true; return; }
  panel.hidden = false; jobShown = job.id;
  const total = job.total, running = job.state === 'running';
  const identified = total - count(job, 'pending', 'working');
  const stateText = { running: 'Running', cancelled: 'Cancelled', done: 'Finished', interrupted: 'Interrupted', paused: 'Paused' }[job.state] || job.state;
  panel.dataset.state = job.state;
  $('#job-title').textContent = running ? 'Downloading media…' : `Download ${stateText.toLowerCase()}`;
  const sourceName = (job.sources || job.source).split(',').map(id => scraperSettings?.sources.find(s => s.id === id)?.name || id).join(' → ');
  // The funnel: which source's pass runs, and how many games wait for the ones below.
  const chain = (job.sources || job.source).split(',').filter(Boolean);
  const passText = chain.length > 1 && running ? ` · pass ${job.pass + 1} of ${chain.length}: ${scraperSettings?.sources.find(s => s.id === chain[job.pass])?.name || chain[job.pass]}${job.waiting ? ` · ${job.waiting.toLocaleString()} game${job.waiting === 1 ? '' : 's'} waiting for the next source` : ''}` : '';
  $('#job-summary').textContent = `${METHOD_NAMES[job.mode]} · ${sourceName} · ${total.toLocaleString()} games${passText}` + (job.message ? ` · ${job.message}` : '');
  drawPasses(job, running);
  countTo($('#stat-done'), count(job, 'done')); countTo($('#stat-partial'), count(job, 'partial'));
  countTo($('#stat-kept'), count(job, 'skipped')); countTo($('#stat-attention'), count(job, 'unmatched', 'ambiguous', 'failed'));
  countTo($('#stat-files'), job.mode === 'pc' ? job.transferred.files : job.downloaded.files);
  $('#stat-files-label').textContent = `files · ${bytes(job.mode === 'pc' ? job.transferred.bytes : job.downloaded.bytes)}`;
  $('#job-identified').value = total ? identified / total * 100 : 0; $('#job-identified-text').textContent = `${identified.toLocaleString()} of ${total.toLocaleString()}`;
  const wanted = job.downloaded.files + job.tasks.queued + job.tasks.leased;
  const settled = count(job, 'done', 'partial', 'skipped', 'unmatched', 'ambiguous', 'failed');
  $('#job-downloaded').value = job.mode === 'pc' ? (wanted ? job.downloaded.files / wanted * 100 : 0) : (total ? settled / total * 100 : 0);
  $('#job-downloaded-text').textContent = `${job.downloaded.files.toLocaleString()} files · ${bytes(job.downloaded.bytes)}`;
  $('#job-transferred-row').hidden = job.mode !== 'pc';
  if (job.mode === 'pc') {
    $('#job-transferred').value = job.downloaded.files ? job.transferred.files / job.downloaded.files * 100 : 0;
    $('#job-transferred-text').textContent = `${job.transferred.files.toLocaleString()} files · ${bytes(job.transferred.bytes)}`;
  }
  $('#job-helper').hidden = job.mode !== 'pc' || !running;
  $('#job-helper-command').textContent = `python3 ps5-media-helper.py http://${location.host} ${job.id}`;
  $('#job-cancel').hidden = !running;
  $('#job-resume').hidden = running || (job.state === 'done' && !count(job, 'pending', 'failed'));
  const box = $('#job-problems-box'), problems = $('#job-problems');
  box.hidden = !job.problems.length;
  $('#job-problems-title').textContent = `Games that need you (${job.problems.length}) · choose a match, search, or skip`;
  if (box.open || !problems.childElementCount) drawProblems(job);
  if (!running && jobTimer) { clearInterval(jobTimer); jobTimer = null; library = { systems: [] }; }
  // The recap once the job has stopped, fetched again whenever what it did changes.
  const stamp = `${job.id}|${job.state}|${JSON.stringify(job.counts)}|${job.downloaded.files}|${job.transferred.files}`;
  if (running) $('#job-recap').hidden = true;
  else if (stamp !== recapStamp) { recapStamp = stamp; loadRecap(job.id); }
}

// The recap: per kind of media, then per system and game, what was downloaded, what was
// already there and what is missing (and why, when the game was not found).
const RECAP_FILTERS = [['all', 'All games'], ['got', 'Got new media'], ['missed', 'Missing something'], ['notfound', 'Not found'], ['had', 'Unchanged']];
async function loadRecap(id) {
  try { recap = (await api('/api/scraper/recap?id=' + encodeURIComponent(id), { signal: AbortSignal.timeout(30000) })).recap; } catch { return; }
  recapLimit = {}; drawRecap();
}
function kindName(id) { return id === 'details' ? 'Game details' : scraperSettings?.catalog.find(k => k.id === id)?.name || id; }
function recapMatches(game) {
  const notFound = ['unmatched', 'ambiguous', 'failed'].includes(game.state);
  if (recapKind && !(game.got.includes(recapKind) || game.missed.includes(recapKind) || game.had.includes(recapKind))) return false;
  const wanted = recapKind ? [recapKind] : null, has = list => wanted ? list.some(k => wanted.includes(k)) : list.length > 0;
  if (recapFilter === 'got' && !has(game.got)) return false;
  if (recapFilter === 'missed' && (!has(game.missed) || notFound)) return false;
  if (recapFilter === 'notfound' && !notFound) return false;
  if (recapFilter === 'had' && (game.got.length || notFound)) return false;
  const query = $('#recap-search').value.trim().toLowerCase();
  return !query || game.label.toLowerCase().includes(query) || game.matched.toLowerCase().includes(query);
}
function drawRecap() {
  const box = $('#job-recap');
  if (!recap || !recap.systems.length) { box.hidden = true; return; }
  box.hidden = false;
  const recapSources = (recap.sources || recap.source).split(',').map(id => scraperSettings?.sources.find(s => s.id === id)).filter(Boolean);
  const source = recapSources.length ? { name: recapSources.map(s => s.name).join(' or '), kinds: [...new Set(recapSources.flatMap(s => s.kinds))] } : null;
  // Per kind: counts and a bar; a click narrows the list to that kind.
  $('#recap-kinds').replaceChildren(...recap.kinds.map(kind => {
    const t = recap.totals[kind], all = t.got + t.had + t.missed || 1, tile = element('button', undefined, 'recap-kind'); tile.type = 'button';
    tile.setAttribute('aria-pressed', recapKind === kind);
    const title = element('span', undefined, 'kind-title'); title.append(svgIcon(KIND_ICONS[kind] || KIND_ICONS.cover), element('span', kindName(kind)));
    const counts = element('span', undefined, 'kind-counts');
    const n = (value, label, cls) => { const s = element('span', undefined, cls); s.append(element('b', value.toLocaleString()), document.createTextNode(' ' + label)); return s; };
    counts.append(n(t.got, 'new', 'n-got'), n(t.had, 'kept', 'n-had'), n(t.missed, 'missing', 'n-missed'));
    const bar = element('span', undefined, 'recap-bar');
    for (const [k, cls] of [['got', 'b-got'], ['had', 'b-had'], ['missed', 'b-missed']]) { const i = element('i', undefined, cls); i.style.width = `${t[k] / all * 100}%`; bar.append(i); }
    tile.append(title, counts, bar);
    tile.title = source && !source.kinds.includes(kind) ? `${source.name} does not offer ${kindName(kind).toLowerCase()}.` : `Show only ${kindName(kind).toLowerCase()}`;
    tile.addEventListener('click', () => { recapKind = recapKind === kind ? '' : kind; recapLimit = {}; drawRecap(); });
    return tile;
  }));
  $('#recap-filter').replaceChildren(...RECAP_FILTERS.map(([id, label]) => {
    const chip = element('button', label, 'chip'); chip.type = 'button'; chip.setAttribute('role', 'radio'); chip.setAttribute('aria-checked', recapFilter === id);
    chip.addEventListener('click', () => { recapFilter = id; recapLimit = {}; drawRecap(); });
    return chip;
  }));
  const open = new Set($$('.recap-system[open]').map(d => d.dataset.system));
  const groups = [];
  for (const system of recap.systems) {
    const games = system.games.filter(recapMatches);
    if (!games.length) continue;
    const details = element('details', undefined, 'recap-system'); details.dataset.system = system.id;
    if (open.has(system.id) || recap.systems.length === 1) details.open = true;
    const got = games.filter(g => g.got.length).length, missing = games.filter(g => g.missed.length).length;
    const summary = element('summary'); summary.append(systemIcon(system.id), element('strong', system.name), element('span', `${games.length.toLocaleString()} game${games.length === 1 ? '' : 's'} · ${got.toLocaleString()} with new media · ${missing.toLocaleString()} missing some`));
    const list = element('div', undefined, 'recap-games'), limit = recapLimit[system.id] || 100;
    const draw = () => {
      list.replaceChildren(...games.slice(0, limit).map(recapRow));
      if (games.length > limit) {
        const more = element('button', `Show ${Math.min(100, games.length - limit)} more of ${(games.length - limit).toLocaleString()}`, 'secondary recap-more'); more.type = 'button';
        more.addEventListener('click', () => { recapLimit[system.id] = limit + 100; drawRecap(); });
        list.append(more);
      }
    };
    details.addEventListener('toggle', () => { if (details.open && !list.childElementCount) draw(); }, { once: false });
    if (details.open) draw();
    details.append(summary, list); groups.push(details);
  }
  $('#recap-systems').replaceChildren(...(groups.length ? groups : [element('p', 'No game matches this view.', 'list-message')]));
}
function recapRow(game) {
  const row = element('div', undefined, 'recap-game'), name = element('div', undefined, 'game-name');
  name.append(element('span', game.label));
  // The name it was matched as, when it says more than the file's name without its tags.
  const bare = game.label.replace(/\s*[([][^)\]]*[)\]]/g, '').trim();
  if (game.matched && game.matched !== game.label && game.matched !== bare) name.append(element('small', `Matched as ${game.matched}`));
  const pills = element('div', undefined, 'pills'), kinds = recapKind ? [recapKind] : recap.kinds;
  for (const kind of kinds) {
    const state = game.got.includes(kind) ? 'got' : game.had.includes(kind) ? 'had' : game.missed.includes(kind) ? 'missed' : '';
    if (!state) continue;
    const pill = element('span', kindName(kind), `kind-pill ${state}`);
    pill.title = `${kindName(kind)}: ${{ got: 'downloaded by this job', had: 'already there, kept', missed: 'missing' }[state]}`;
    pills.append(pill);
  }
  row.append(name, pills);
  if (['unmatched', 'ambiguous', 'failed', 'partial'].includes(game.state) && game.message) row.append(element('p', game.message, 'note'));
  return row;
}
$('#recap-search').addEventListener('input', () => { recapLimit = {}; drawRecap(); });
// The funnel, one step a source: done, running (games checked of those that reached
// it) or waiting; and what each gave. A source with nothing to give is done in seconds.
function drawPasses(job, running) {
  const box = $('#job-passes'), passes = job.passes || [];
  box.hidden = passes.length < 2;
  if (box.hidden) return;
  box.replaceChildren(...passes.map((pass, i) => {
    const state = !running || i < job.pass ? 'done' : i === job.pass ? 'running' : 'waiting';
    const step = element('li', undefined, 'job-pass'); step.dataset.state = state;
    const head = element('div', undefined, 'pass-head'), name = scraperSettings?.sources.find(s => s.id === pass.source)?.name || pass.source;
    const number = element('span', state === 'done' ? '' : String(i + 1), 'pass-number'); if (state === 'done') number.append(uiIcon('check')); head.append(number, element('span', name));
    step.append(head);
    const gave = pass.files ? `${pass.files.toLocaleString()} found for ${pass.games.toLocaleString()} game${pass.games === 1 ? '' : 's'}` : 'nothing missing it has';
    if (state === 'running') {
      const bar = document.createElement('progress'); bar.max = Math.max(1, pass.reached); bar.value = pass.checked;
      step.append(element('p', `${pass.checked.toLocaleString()} of ${pass.reached.toLocaleString()} games checked · ${pass.files ? gave : 'nothing found yet'}`), bar);
    } else if (state === 'done') step.append(element('p', `${pass.checked.toLocaleString()} game${pass.checked === 1 ? '' : 's'} checked · ${gave}`));
    else step.append(element('p', pass.reached ? `${pass.reached.toLocaleString()} game${pass.reached === 1 ? '' : 's'} waiting for it so far` : 'Asked only for what the sources above do not have'));
    return step;
  }));
}
function drawProblems(job) {
  const problems = $('#job-problems'); problems.replaceChildren();
  for (const p of job.problems) {
    const row = element('div', undefined, 'problem-row'), text = element('div');
    text.append(element('strong', p.label), element('p', p.message, p.state === 'failed' ? 'inline-error' : 'muted'));
    const actions = element('div', undefined, 'problem-actions');
    const resolve = async (action, value) => {
      row.classList.add('resolving');
      try { const { job: next } = await api(`/api/scraper/resolve?id=${job.id}&item=${p.item}&action=${action}&value=${encodeURIComponent(value || '')}`, { method: 'POST', signal: AbortSignal.timeout(60000) }); drawJob(next); drawProblems(next); pollJob(); }
      catch (error) { row.classList.remove('resolving'); announce(error.message, true); }
    };
    if (p.candidates.length) {
      const pick = document.createElement('select'); pick.setAttribute('aria-label', `Match for ${p.label}`);
      for (const c of p.candidates) pick.add(new Option(c, c));
      const use = element('button', 'Use this match', 'primary compact'); use.addEventListener('click', () => resolve('choose', pick.value));
      actions.append(pick, use);
    }
    const search = document.createElement('input'); search.type = 'search'; search.placeholder = 'Search by name'; search.value = p.label; search.setAttribute('aria-label', `Search for ${p.label}`);
    search.addEventListener('keydown', event => { if (event.key === 'Enter') resolve('search', search.value); });
    const find = element('button', 'Search', 'secondary'); find.addEventListener('click', () => resolve('search', search.value));
    const skip = element('button', 'Skip', 'secondary'); skip.addEventListener('click', () => resolve('skip'));
    actions.append(search, find, skip); row.append(text, actions); problems.append(row);
  }
}
$('#job-problems-box').addEventListener('toggle', () => { if ($('#job-problems-box').open) loadJob(); });
async function loadJob() {
  try { const { job } = await api('/api/scraper/job'); drawJob(job); if (job && job.state === 'running') pollJob(); } catch {}
}
function pollJob() {
  if (jobTimer) return;
  jobTimer = setInterval(async () => {
    if (document.hidden) return;
    try { const { job } = await api('/api/scraper/job' + (jobShown ? '?id=' + jobShown : '')); drawJob(job); } catch {}
  }, 1500);
}
$('#job-cancel').addEventListener('click', async () => { try { await api('/api/scraper/cancel?id=' + jobShown, { method: 'POST' }); loadJob(); } catch (error) { announce(error.message, true); } });
$('#job-resume').addEventListener('click', async () => { try { drawJob((await api('/api/scraper/resume?id=' + jobShown, { method: 'POST' })).job); pollJob(); } catch (error) { announce(error.message, true); } });
$('#job-helper-copy').addEventListener('click', async () => { try { await navigator.clipboard.writeText($('#job-helper-command').textContent); announce('Command copied.'); } catch { announce('Select the command and copy it.'); } });
