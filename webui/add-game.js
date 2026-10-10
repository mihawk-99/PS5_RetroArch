/* Add Game shares the library, metadata, media store and scraper used by every frontend. */
'use strict';
let addGame = null;
const ADD_FIELDS = [['name','Game name'],['developer','Developer'],['publisher','Publisher'],['released','Release date'],['genre','Genre'],['players','Players'],['rating','Rating (%)'],['description','Description']];
function addTab(name) {
  if (addGame?.busy) return;
  $$('[data-add-tab]').forEach(b => b.setAttribute('aria-pressed', b.dataset.addTab === name));
  $$('[data-add-panel]').forEach(p => { p.hidden = p.dataset.addPanel !== name; });
}
function addDownloadsEnabled() { return $('#add-download').getAttribute('aria-pressed') === 'true'; }
function setAddDownloads(enabled) {
  $('#add-download').setAttribute('aria-pressed', enabled); $('#add-download-state').textContent = enabled ? 'Enabled' : 'Enable';
  $('#add-scrapers').hidden = !enabled;
  $('#add-submit').replaceChildren(uiIcon(enabled ? 'download' : 'plus'), document.createTextNode(enabled ? 'Add game & download media' : 'Add to library'));
}
function addMessage(text, error = false) { $('#add-status').textContent = text; $('#add-status').classList.toggle('inline-error', error); }
function closeAdd() {
  if (!addGame || addGame.busy) return;
  if ((addGame.file || addGame.dirty) && !confirm(i18n.t(addGame.uploaded ? 'The game backup is already in your library. Close without finishing the remaining details and media?' : 'Discard this new game draft?'))) return;
  disposeAdd(); $('#add-game').close();
}
function disposeAdd() { if (addGame) { for (const item of addGame.media.values()) URL.revokeObjectURL(item.url); addGame.searchController?.abort(); } addGame = null; }
function addFile(file) {
  if (!file || !addGame || addGame.busy || addGame.uploaded) return;
  if (!file.size || file.size > uploadLimit) { addMessage('Choose a non-empty backup of at most ' + bytes(uploadLimit) + '.', true); return; }
  addGame.file = file;
  $('#add-file-name').textContent = file.name; $('#add-file-hint').textContent = bytes(file.size) + ' · click to choose another file';
  if (!$('#add-name').value || $('#add-name').value === addGame.suggestedName) {
    addGame.suggestedName = file.name.replace(/\.[^.]+$/, '').replace(/[_]+/g, ' '); $('#add-name').value = addGame.suggestedName;
  }
  $('#add-search').value = $('#add-name').value; addDestination(); addMessage('Ready to add. Details and media are optional.');
}
function addDestination() {
  const system = addGame.systems.find(s => s.id === $('#add-system').value);
  $('#add-system-icon').replaceChildren(systemIcon(system?.id));
  $('#add-file').accept = system ? [...new Set(system.extensions.split('|'))].map(x => '.' + x).join(',') : '';
  $('#add-destination').textContent = system && addGame.file ? 'Destination: /app0/content/' + system.name + '/' + addGame.file.name : 'Select the system this game belongs to.';
}
async function openAddGame() {
  if (addGame) return;
  addGame = {file:null, media:new Map(), selected:'cover', sources:['libretro'], systems:[], settings:null, busy:false, dirty:false, uploaded:null, launchboxId:''};
  const context = addGame;
  $('#add-game').showModal(); addTab('file'); $('#add-results').replaceChildren(); $('#add-match').textContent = ''; $('#add-search-status').textContent = ''; $('#add-search').value = ''; $('#add-search-button').disabled = false;
  $('#add-file-name').textContent = 'Choose a game backup'; $('#add-file-hint').textContent = 'Drop one file here, or browse your device';
  setAddDownloads(true); $('#add-layout').hidden = false; $('#add-activity').hidden = true; $('#add-submit').hidden = false; $('#add-progress').hidden = true; $('#add-submit').disabled = true;
  $('#add-system').disabled = $('#add-drop').disabled = false; $('#add-fetch-details').checked = true;
  addMessage('Loading your installed systems…');
  $('#add-fields').replaceChildren(...ADD_FIELDS.map(([key, title]) => {
    const label = element('label', title, key === 'name' || key === 'description' ? 'wide-field' : '');
    const input = document.createElement(key === 'description' ? 'textarea' : 'input'); input.id = 'add-' + key;
    input.maxLength = key === 'description' ? 12000 : 512; input.required = key === 'name';
    if (key === 'rating') { input.type = 'number'; input.min = 0; input.max = 100; input.step = .1; }
    if (key === 'released') input.placeholder = 'YYYY-MM-DD';
    input.addEventListener('input', () => { addGame.dirty = true; }); label.append(input); return label;
  }));
  try {
    const [options, settings] = await Promise.all([api('/api/library/add-options'), api('/api/scraper/settings')]);
    if (addGame !== context) return;
    context.systems = options.systems; context.settings = settings;
    $('#add-system').replaceChildren(new Option('Choose a system', ''), ...options.systems.map(s => new Option(s.name,s.id)));
    if (options.systems.some(s => s.id === $('#games-system').value)) $('#add-system').value = $('#games-system').value;
    $('#add-region').value = settings.region || 'us'; $('#add-language').value = settings.language || 'en';
    if (!$('#add-language').value) $('#add-language').value = 'en';
    drawAddMedia(); drawAddSources(); addDestination(); $('#add-submit').disabled = !options.systems.length;
    addMessage(options.systems.length ? 'Choose a system and a game backup to begin.' : 'No supported cores are installed. Install a core before adding a game.');
  } catch (error) { if (addGame === context) addMessage(error.message + ' Close and reopen Add Game to retry.', true); }
}
function drawAddSources() {
  const context = addGame, box = $('#add-sources'); box.replaceChildren();
  const ordered = [...context.sources.map(id => context.settings.sources.find(s => s.id === id)), ...context.settings.sources.filter(s => !context.sources.includes(s.id))].filter(Boolean);
  for (const source of ordered) {
    const row = element('div',undefined,'add-source'); row.append(iconNode('source-' + source.id));
    const label = element('label'), check = document.createElement('input'); check.type = 'checkbox'; check.checked = context.sources.includes(source.id);
    check.disabled = !source.available || (source.account && !source.signed_in); label.append(check, document.createTextNode(source.name)); row.append(label);
    check.addEventListener('change', () => { context.sources = check.checked ? [...context.sources,source.id] : context.sources.filter(id => id !== source.id); drawAddSources(); });
    if (source.available && source.account && !source.signed_in) {
      const login = element('button','Sign in','text-button'); login.type = 'button'; login.addEventListener('click', () => openSignIn(source)); row.append(login);
    } else if (!source.available) row.append(element('small','Not available yet','muted'));
    if (check.checked) for (const direction of [-1,1]) {
      const move = element('button',undefined,'icon-button'); move.type = 'button'; move.append(uiIcon(direction < 0 ? 'chevron-up' : 'chevron-down'));
      const index = context.sources.indexOf(source.id); move.disabled = index + direction < 0 || index + direction >= context.sources.length;
      move.setAttribute('aria-label', 'Ask ' + source.name + (direction < 0 ? ' earlier' : ' later'));
      move.addEventListener('click', () => { [context.sources[index],context.sources[index+direction]] = [context.sources[index+direction],context.sources[index]]; drawAddSources(); }); row.append(move);
    }
    box.append(row);
  }
}
function drawAddMedia() {
  const context = addGame, kind = context.settings.catalog.find(k => k.id === context.selected), chosen = context.media.get(kind.id);
  const preview = $('#add-media-preview'); preview.replaceChildren();
  const stage = element('div',undefined,'add-artwork');
  if (chosen && !['video','manual'].includes(kind.id)) { const img = document.createElement('img'); img.src = chosen.url; img.alt = kind.name; stage.append(img); }
  else stage.append(uiIcon(KIND_ICONS[kind.id]));
  const side = element('div'); side.append(element('h4',kind.name),element('p',chosen ? chosen.file.name + ' · ' + bytes(chosen.file.size) : kind.description,'muted sheet-hint'));
  const choose = element('button',chosen ? 'Replace file…' : 'Choose file…','secondary'); choose.type = 'button';
  choose.addEventListener('click', () => { $('#add-media-file').accept = kind.accepts.map(x => '.' + x).join(','); $('#add-media-file').click(); });
  const actions = element('div',undefined,'sheet-actions'); actions.append(choose);
  if (chosen) { const remove = element('button','Remove','text-button'); remove.type = 'button'; remove.addEventListener('click', () => { URL.revokeObjectURL(chosen.url); context.media.delete(kind.id); drawAddMedia(); }); actions.append(remove); }
  side.append(actions); preview.append(stage,side);
  stage.addEventListener('dragover',e => e.preventDefault()); stage.addEventListener('drop',e => { e.preventDefault(); chooseAddMedia(e.dataTransfer.files[0]); });
  $('#add-media-types').replaceChildren(...context.settings.catalog.map(k => {
    const button = element('button',undefined,'sheet-thumb'); button.type = 'button'; button.setAttribute('aria-pressed',k.id === context.selected);
    button.append(uiIcon(KIND_ICONS[k.id]), element('span',k.name), element('small',context.media.has(k.id) ? 'Ready' : 'Optional'));
    button.addEventListener('click', () => { context.selected = k.id; drawAddMedia(); }); return button;
  }));
}
function chooseAddMedia(file) {
  if (!file || addGame.busy) return;
  const kind = addGame.settings.catalog.find(k => k.id === addGame.selected), ext = file.name.split('.').pop().toLowerCase();
  if (!file.size || file.size > 1024 ** 3 || !kind.accepts.includes(ext)) { addMessage('Choose ' + kind.accepts.join(', ').toUpperCase() + ' media of at most 1 GiB.',true); return; }
  const old = addGame.media.get(kind.id); if (old) URL.revokeObjectURL(old.url);
  addGame.media.set(kind.id,{file,url:URL.createObjectURL(file)}); addGame.dirty = true; drawAddMedia();
}
$('#add-search-form').addEventListener('submit', async e => {
  e.preventDefault(); const context = addGame, system = $('#add-system').value, query = $('#add-search').value.trim();
  if (!system || query.length < 2) { $('#add-search-status').textContent = 'Choose a system and enter at least two letters.'; return; }
  context.searchController?.abort(); context.searchController = new AbortController();
  const controller = context.searchController; $('#add-search-button').disabled = true;
  $('#add-search-status').textContent = 'Searching LaunchBox… The first search downloads its database to your console and may take a few minutes.'; $('#add-results').replaceChildren();
  try {
    const data = await api('/api/library/launchbox?' + new URLSearchParams({system,q:query}), {method:'POST',signal:controller.signal});
    if (addGame !== context || controller !== context.searchController || $('#add-system').value !== system) return;
    $('#add-search-status').textContent = data.results.length ? 'Choose a match to fill the details below.' : 'No matches. Try another title, or enter your details below.';
    for (const match of data.results) {
      const row = element('div',undefined,'add-result'), text = element('div');
      text.append(element('strong',match.name),element('small',[match.released,match.developer].filter(Boolean).join(' · ')),element('p',match.description,'add-result-description'));
      const choose = element('button','Use these details','secondary'); choose.type = 'button';
      choose.addEventListener('click', () => {
        if (context.dirty && !confirm(i18n.t('Replace the current details with this LaunchBox match? You can edit them afterward.'))) return;
        for (const [key] of ADD_FIELDS) $('#add-' + key).value = key === 'rating' && match[key] ? Math.round(Number(match[key])*1000)/10 : match[key] || '';
        context.launchboxId = match.id; context.dirty = true; $('#add-match').textContent = 'LaunchBox match: ' + match.name;
        $('#add-results').replaceChildren(); $('#add-search-status').textContent = ''; $('#add-name').focus();
      }); row.append(text,choose); $('#add-results').append(row);
    }
  } catch (error) { if (addGame === context && controller === context.searchController) $('#add-search-status').textContent = error.name === 'AbortError' ? 'Search cancelled.' : error.message; }
  finally { if (addGame === context && controller === context.searchController) $('#add-search-button').disabled = false; }
});
function uploadNewGameFile(url, file, context) {
  return new Promise((resolve,reject) => {
    const request = new XMLHttpRequest(); context.upload = request; request.open('PUT',url); request.setRequestHeader('X-RetroArch-Token',token);
    request.upload.addEventListener('progress',e => { if (e.lengthComputable) { $('#add-progress').value = e.loaded/e.total*100; addMessage('Sending ' + file.name + ' · ' + Math.round(e.loaded/e.total*100) + '%'); } });
    request.addEventListener('load', () => { let data = {}; try { data = JSON.parse(request.responseText); } catch {} request.status === 201 ? resolve(data) : reject(new Error(data.error || 'Upload failed. Check the library before retrying.')); });
    request.addEventListener('error', () => reject(new Error('Connection lost. Check the library before retrying; the upload may have completed.')));
    request.addEventListener('abort', () => reject(new Error('Upload cancelled. Check the library if it had already reached 100%.'))); request.send(file);
  });
}
async function watchAddDownloads(context, query) {
  context.watching = true; context.detached = false;
  $('#add-layout').hidden = true; $('#add-activity').hidden = false; $('#add-submit').hidden = true;
  $('#add-progress').hidden = true; $('#add-cancel').disabled = false; $('#add-cancel').textContent = 'Continue in Download Media';
  $('#add-activity-game').textContent = $('#add-name').value;
  $('#add-live-media').replaceChildren(); $('#add-activity-progress').removeAttribute('value');
  let job = context.downloadJob, mediaStamp = '', mediaCount = 0;
  try {
    for (;;) {
      if (context.detached) return 'downloads';
      const running = job.state === 'running';
      const sources = (job.sources || job.source).split(','), source = sources[job.pass || 0];
      const sourceName = context.settings.sources.find(s => s.id === source)?.name || source;
      $('#add-activity-title').textContent = running ? 'Downloading your game’s media' : job.state === 'done' ? 'Your game has been added' : 'Your game is saved; downloads need attention';
      $('#add-activity-detail').textContent = `${sourceName} · ${job.downloaded.files} files downloaded · ${bytes(job.downloaded.bytes)}` + (job.message ? ' · ' + job.message : '');
      addMessage(running ? 'Finding and downloading media…' : 'Choose what to do next.');
      try {
        context.pollController = new AbortController();
        const signal = AbortSignal.any([context.pollController.signal, AbortSignal.timeout(10000)]);
        const info = await api('/api/library/game?' + query, {signal});
        const present = info.kinds.filter(k => k.present); mediaCount = present.length;
        const stamp = present.map(k => k.id + ':' + k.changed).join(',');
        if (stamp !== mediaStamp) {
          mediaStamp = stamp; $('#add-live-media').replaceChildren(...present.map(kind => {
            const item = element('div',undefined,'add-live-item');
            if (['video','manual'].includes(kind.id)) item.append(uiIcon(KIND_ICONS[kind.id]));
            else { const image = document.createElement('img'); image.src = '/api/library/media?' + query + '&kind=' + kind.id + '&v=' + kind.changed; image.alt = kind.name; image.addEventListener('error', () => image.replaceWith(uiIcon(KIND_ICONS[kind.id]))); item.append(image); }
            item.append(element('span',kind.name)); return item;
          }));
        }
        if (!running) break;
        await new Promise(resolve => setTimeout(resolve,1000));
        if (context.detached) return 'downloads';
        job = (await api('/api/scraper/job?id=' + encodeURIComponent(job.id), {signal})).job;
        if (!job) throw new Error('Download job unavailable.');
        context.downloadJob = job;
      } catch (error) {
        if (context.detached) return 'downloads';
        $('#add-activity-detail').textContent = 'Connection interrupted. Reconnecting… Your game is already saved.';
        await new Promise(resolve => setTimeout(resolve,2000));
        try { job = (await api('/api/scraper/job?id=' + encodeURIComponent(context.downloadJob.id), {signal:AbortSignal.timeout(10000)})).job || context.downloadJob; } catch {}
      }
    }
    $('#add-activity-progress').value = 100;
    const attention = job.state !== 'done' || count(job,'ambiguous','unmatched','failed');
    $('#add-activity-hint').textContent = attention ? 'Some downloads need your help. Review matches or retry in Download Media. Your game and any downloaded artwork are saved.' : mediaCount ? `${mediaCount} media types are ready. Open your game to see them—no page refresh needed.` : 'No media was found. You can review the download or add your own artwork from the game overlay.';
    $('#add-submit').hidden = false; $('#add-submit').disabled = false; $('#add-submit').replaceChildren(uiIcon('gamepad-2'),document.createTextNode('Open game'));
    $('#add-cancel').textContent = attention ? 'Review downloads' : 'Download details';
    return await new Promise(resolve => { context.finishChoice = resolve; });
  } finally { context.watching = false; context.finishChoice = null; context.pollController = null; }
}
$('#add-submit').addEventListener('click', async () => {
  const context = addGame; if (context?.finishChoice) { context.finishChoice('game'); return; } if (!context || context.busy) return;
  const system = context.systems.find(s => s.id === $('#add-system').value);
  if (!system || !context.file) { addTab('file'); addMessage('Choose a system and a game backup.',true); return; }
  for (const [key] of ADD_FIELDS) if (!$('#add-' + key).checkValidity()) { addTab('details'); $('#add-' + key).reportValidity(); return; }
  if (!$('#add-name').value.trim()) { addTab('details'); $('#add-name').focus(); addMessage('Give this game a name.',true); return; }
  const download = addDownloadsEnabled();
  if (download && !context.sources.length) { addTab('media'); addMessage('Choose at least one download source.',true); return; }
  const metadata = {partial:'true',launchbox_id:context.launchboxId};
  for (const [key] of ADD_FIELDS) { const value = $('#add-' + key).value.trim(); if (value) metadata[key] = key === 'rating' ? String(Number(value)/100) : value; }
  context.busy = true; context.searchController?.abort();
  for (const control of $$('#add-game input, #add-game select, #add-game textarea, #add-game button')) { control.dataset.wasDisabled = control.disabled; control.disabled = true; }
  $('#add-cancel').disabled = false; $('#add-cancel').textContent = 'Cancel upload'; $('#add-progress').hidden = false;
  try {
    if (!context.uploaded) context.uploaded = await uploadNewGameFile('/api/library/add?' + new URLSearchParams({system:system.id,filename:context.file.name}),context.file,context);
    const game = context.uploaded, query = new URLSearchParams({system:game.system,game:game.key,path:game.path});
    context.upload = null; $('#add-cancel').disabled = true; addMessage('Saving game details…');
    await api('/api/library/game?' + query,{method:'POST',body:JSON.stringify(metadata)});
    for (const [kind,item] of context.media) if (!item.stored) {
      $('#add-cancel').disabled = false;
      await uploadNewGameFile('/api/library/media?' + query + '&' + new URLSearchParams({kind,type:item.file.name.split('.').pop().toLowerCase()}),item.file,context); item.stored = true;
    }
    context.upload = null; $('#add-cancel').disabled = true;
    let destination = 'game';
    if (download) {
      addMessage('Starting media downloads…');
      const kinds = [...new Set(context.settings.sources.filter(s => context.sources.includes(s.id)).flatMap(s => s.kinds))];
      const started = context.downloadJob ? {job:context.downloadJob} : await api('/api/scraper/start?' + new URLSearchParams({mode:'ps5',sources:context.sources.join(','),kinds:kinds.join(','),details:$('#add-fetch-details').checked ? '1':'0',region:$('#add-region').value,language:$('#add-language').value,overwrite:'0'}),{method:'POST',body:game.system + '\t' + game.path,signal:AbortSignal.timeout(60000)});
      context.downloadJob = started.job;
      destination = await watchAddDownloads(context, query);
    }
    disposeAdd(); $('#add-game').close(); await loadGames();
    const addedSystem = library.systems.find(s => s.id === game.system), added = addedSystem?.games.find(g => g.path === game.path);
    if (destination === 'downloads') { location.hash = 'media'; return; }
    if (added) await openGame(addedSystem,added);
    announce('Game added.');

  } catch (error) {
    $('#add-layout').hidden = false; $('#add-activity').hidden = true; $('#add-submit').hidden = false;
    addMessage((context.uploaded ? 'Game backup added. ' : '') + error.message + (context.uploaded ? ' Retry to finish the remaining setup.' : ''),true);
  } finally {
    context.busy = false; context.upload = null;
    for (const control of $$('#add-game [data-was-disabled]')) { control.disabled = control.dataset.wasDisabled === 'true'; delete control.dataset.wasDisabled; }
    $('#add-cancel').textContent = 'Cancel'; $('#add-progress').hidden = true;
    if (addGame === context && context.uploaded) { $('#add-system').disabled = $('#add-drop').disabled = true; $('#add-submit').textContent = 'Finish setup'; }
  }
});
$('#add-game-open').addEventListener('click',openAddGame);
$$('[data-add-tab]').forEach(b => b.addEventListener('click', () => addTab(b.dataset.addTab)));
$('#add-close').addEventListener('click',closeAdd);
$('#add-cancel').addEventListener('click', () => {
  if (addGame?.finishChoice) addGame.finishChoice('downloads');
  else if (addGame?.watching) { addGame.detached = true; addGame.pollController?.abort(); }
  else if (addGame?.busy) addGame.upload?.abort(); else closeAdd();
});
$('#add-game').addEventListener('cancel',e => { e.preventDefault(); closeAdd(); });
$('#add-drop').addEventListener('click', () => $('#add-file').click());
$('#add-drop').addEventListener('dragover',e => e.preventDefault());
$('#add-drop').addEventListener('drop',e => { e.preventDefault(); if (e.dataTransfer.files.length !== 1) addMessage('Choose one game backup. Use Content for complete disc sets or folders.',true); else addFile(e.dataTransfer.files[0]); });
$('#add-file').addEventListener('change',e => { addFile(e.target.files[0]); e.target.value = ''; });
$('#add-media-file').addEventListener('change',e => { chooseAddMedia(e.target.files[0]); e.target.value = ''; });
$('#add-system').addEventListener('change', () => { addGame.launchboxId = ''; addGame.searchController?.abort(); $('#add-match').textContent = ''; $('#add-results').replaceChildren(); addDestination(); });
$('#add-download').addEventListener('click', () => setAddDownloads(!addDownloadsEnabled()));
$('#sign-in').addEventListener('close',async () => { const context = addGame; if (context) { try { const settings = await api('/api/scraper/settings'); if (addGame === context) { context.settings = settings; drawAddSources(); } } catch (error) { addMessage(error.message,true); } } });
