/* The Download Media tab in Chromium (tests/test_scraper.py starts the server and a fake
 * libretro source): the method is asked first and remembered, the library is scraped on
 * the PS5 with live stats, the ambiguous game is resolved from the tab, and covers show
 * on the Games tab. */
const assert = require('node:assert/strict');
const { chromium } = require(process.env.PLAYWRIGHT_PATH || 'playwright');
(async () => {
  const browser = await chromium.launch({ headless: true, executablePath: process.env.CHROMIUM_PATH || '/usr/bin/chromium' });
  try {
    const page = await browser.newPage();
    const errors = []; page.on('pageerror', e => errors.push(e.message));
    let releaseResponse = [];
    await page.route('https://api.github.com/**', route => route.fulfill({ json: releaseResponse }));
    // Sign in from Add Game before the Download Media page initializes its state.
    await page.route('**/api/scraper/settings', async route => {
      const response = await route.fetch(), settings = await response.json();
      settings.sources.find(s => s.id === 'screenscraper').available = true;
      await route.fulfill({json: settings});
    });
    await page.route('**/api/scraper/account?source=screenscraper', route => route.fulfill({json:{signed_in:true,user:'browser fixture'}}));
    await page.route('**/version.json', async route => {
      const response = await route.fetch(), version = await response.json();
      await route.fulfill({json:{...version,release:'v0.6.7-alpha.6'}});
    });
    await page.goto(process.env.WEBUI_TEST_URL + '/#games');
    for (const [tag,state,icon] of [['v0.6.7-alpha.6','current','check'],['v1.0.0-beta.1','update','info']]) {
      releaseResponse = [{tag_name:tag,published_at:'2026-10-01T13:00:00Z',draft:false}];
      await page.locator('#check-release').click();
      await page.waitForFunction(state => document.querySelector('.release-bar').dataset.state === state, state);
      assert.equal(await page.locator('.release-icon').evaluate((node,name) => node.classList.contains('color-icon') && node.getAttribute('src') === uiIcon(name).getAttribute('src'), icon),true);
    }
    releaseResponse = []; await page.unroute('**/version.json');
    await page.locator('#add-game-open').click();
    await page.locator('[data-add-tab=media]').click();
    // Exercise image decoding under the server's CSP, not just URL/status checks.
    const mediaIcons = await page.locator('#add-media-types .icon').evaluateAll(async icons => {
      return Promise.all(icons.map(async icon => {
        if (icon instanceof HTMLImageElement) {
          try { await icon.decode(); return icon.naturalWidth > 0; } catch { return false; }
        }
        return icon instanceof SVGSVGElement && icon.children.length > 0 && icon.getBoundingClientRect().width > 0;
      }));
    });
    assert.equal(mediaIcons.length,10);
    assert.ok(mediaIcons.every(Boolean),'every media selector icon renders under the production CSP');
    assert.equal(await page.locator('#add-download').getAttribute('aria-pressed'),'true');
    assert.equal(await page.locator('#add-scrapers').isVisible(),true);
    assert.match(await page.locator('#add-submit').innerText(),/Add game & download media/);
    await page.locator('.add-source', {hasText:'ScreenScraper'}).getByRole('button', {name:'Sign in',exact:true}).click();
    await page.locator('#sign-in-user').fill('browser fixture');
    await page.locator('#sign-in-password').fill('fixture');
    await page.locator('#sign-in-submit').click();
    await page.locator('#sign-in-submit').filter({hasText:'Done'}).waitFor();
    assert.equal(await page.locator('#sign-in-error').innerText(),'');
    await page.locator('#sign-in-submit').click();
    await page.locator('#add-cancel').click();
    await page.unroute('**/api/scraper/settings');
    await page.unroute('**/api/scraper/account?source=screenscraper');
    // Opened straight on the tab (the state it reads is declared before it runs).
    await page.goto(process.env.WEBUI_TEST_URL + '/#media');
    await page.waitForFunction(() => document.querySelectorAll('.kind-tile').length === 10);
    await page.waitForFunction(() => !document.querySelector('#restart-notice').hidden);
    assert.match(await page.locator('#restart-notice').innerText(), /Close PS5 RetroArch on the console/);
    await page.goto(process.env.WEBUI_TEST_URL + '/#games');
    await page.waitForFunction(() => document.querySelectorAll('.game-card').length === 5);
    await page.locator('a[data-page=media]').click();
    await page.waitForFunction(() => location.hash === '#media' && document.querySelectorAll('.kind-tile').length === 10);
    assert.match(await page.locator('#scrape-method-hint').innerText(), /This PC → PS5: this PC downloads, then transfers to the PS5/);
    assert.equal(await page.locator('#scrape-start').isDisabled(), true, 'no start until a method is chosen');
    // The source's kinds can be chosen; the others say they are coming.
    assert.equal(await page.locator('.kind-tile[aria-pressed="true"]').count(), 3);
    assert.equal(await page.locator('.kind-tile:disabled').count(), 6);
    await page.locator('.kind-tile', { hasText: 'Logo' }).click();
    assert.equal(await page.locator('.kind-tile[aria-pressed="true"]').count(), 4);
    // Game details: offered, but not by libretro (pictures only).
    assert.equal(await page.locator('.details-tile').isDisabled(), true);
    assert.match(await page.locator('.details-tile').innerText(), /Game details[\s\S]*Not from libretro thumbnails/);
    await page.locator('#kinds-recommended').click();
    assert.equal(await page.locator('.kind-tile[aria-pressed="true"]').count(), 3);
    await page.locator('.segmented label:has(input[value="ps5"])').click();
    assert.match(await page.locator('#scrape-summary').innerText(), /3 kinds of media · 5 games · libretro thumbnails · downloaded on the PS5/);
    // What happens to media already there: said plainly, missing-only by default.
    assert.match(await page.locator('#scrape-summary').innerText(), /only what’s missing$/);
    assert.match(await page.locator('#scrape-existing-hint').innerText(), /Only media your games don’t have yet is downloaded/);
    await page.locator('label:has(#scrape-overwrite)').click();
    assert.match(await page.locator('#scrape-existing-hint').innerText(), /downloaded again and replaces the files/);
    assert.match(await page.locator('#scrape-summary').innerText(), /replacing what you have$/);
    await page.locator('label:has(#scrape-keep)').click();
    await page.locator('#scrape-start').click();
    await page.waitForFunction(() => /finished/.test(document.querySelector('#job-title').textContent), null, { timeout: 60000 });
    await page.waitForFunction(() => document.querySelector('#stat-done').textContent === '2' && document.querySelector('#stat-attention').textContent === '2');
    assert.equal(await page.locator('#stat-partial').innerText(), '1');
    // The ambiguous game: its region's candidate offered first, chosen from the tab.
    await page.locator('#job-problems-box summary').click();
    const row = page.locator('.problem-row', { hasText: 'Chrono Trigger Special Edition' });
    assert.equal(await row.locator('select').inputValue(), 'Chrono Trigger (USA)');
    await row.getByRole('button', { name: 'Use this match' }).click();
    await page.waitForFunction(() => document.querySelector('#stat-attention').textContent === '1', null, { timeout: 60000 });
    // The recap: per kind, then each game's media, the missing ones and why.
    await page.waitForFunction(() => !document.querySelector('#job-recap').hidden && /4 new/.test(document.querySelector('.recap-kind')?.textContent || ''), null, { timeout: 30000 });
    assert.match(await page.locator('.recap-kind', { hasText: 'Title screen' }).innerText(), /3 new[\s\S]*0 kept[\s\S]*2 missing/);
    const metroid = page.locator('.recap-game', { hasText: 'Super Metroid' });
    assert.equal(await metroid.locator('.kind-pill.got').count(), 2);
    assert.equal(await metroid.locator('.kind-pill.missed').innerText(), 'Title screen');
    await page.locator('#recap-filter .chip', { hasText: 'Not found' }).click();
    assert.equal(await page.locator('.recap-game').count(), 1);
    assert.match(await page.locator('.recap-game').innerText(), /Totally Unknown Homebrew[\s\S]*No match at libretro/);
    await page.locator('#recap-filter .chip', { hasText: 'All games' }).click();
    await page.locator('.recap-kind', { hasText: 'Title screen' }).click();
    await page.locator('#recap-search').fill('chrono');
    assert.equal(await page.locator('.recap-game').count(), 2);
    assert.equal(await page.locator('.recap-game .kind-pill').count(), 2, 'one kind shown when one is picked');
    await page.goto(process.env.WEBUI_TEST_URL + '/#games');
    await page.waitForFunction(() => {
      const covers = [...document.querySelectorAll('.game-cover img:not(.color-icon)')];
      return covers.length === 4 && covers.every(img => img.complete && img.naturalWidth > 0);
    });
    // A card opens the game: one row a kind of media, missing ones greyed, each replaceable.
    await page.locator('.game-card', { hasText: 'Donkey Kong Country 2' }).locator('.game-cover').click();
    const sheet = page.locator('#game-sheet');
    await sheet.waitFor({ state: 'visible' });
    await page.waitForFunction(() => document.querySelectorAll('#game-sheet .sheet-thumb').length === 10);
    assert.equal(await page.locator('.media-row-item:not(.missing)').count(), 1);
    assert.equal(await page.locator('.media-row-item').count(), 1);
    assert.equal(await page.locator('.media-row-item[data-kind="cover"]:not(.missing) .media-stage').count(), 1);
    assert.match(await page.locator('#sheet-media-count').innerText(), /3 of 10 types/);
    await page.locator('#game-sheet .sheet-thumb', { hasText: 'Fan art' }).click();
    const fanart = page.locator('.media-row-item[data-kind="fanart"]');
    assert.equal(await fanart.locator('button').innerText(), 'Add…');
    // A type no frontend shows is refused on the page; a PNG is sent and shown at once.
    await fanart.locator('input[type=file]').setInputFiles({ name: 'art.gif', mimeType: 'image/gif', buffer: Buffer.from('GIF89a') });
    assert.match(await fanart.locator('.state').innerText(), /takes PNG, JPG, JPEG files/);
    const png = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==', 'base64');
    await fanart.locator('input[type=file]').setInputFiles({ name: 'my fan art.png', mimeType: 'image/png', buffer: png });
    await page.waitForFunction(() => !document.querySelector('.media-row-item[data-kind="fanart"]').classList.contains('missing'));
    assert.match(await page.locator('.media-row-item[data-kind="fanart"] .state').innerText(), /PNG · .* · your file, kept when downloading again/);
    assert.match(await page.locator('#sheet-media-count').innerText(), /4 of 10 types/);
    await page.locator('#sheet-nav button[data-tab="details"]').click();
    await page.locator('#sheet-details input[name="developer"]').fill('My developer');
    await page.locator('#sheet-nav button[data-tab="media"]').click();
    await page.locator('#sheet-nav button[data-tab="details"]').click();
    assert.equal(await page.locator('#sheet-details input[name="developer"]').inputValue(), 'My developer');
    await page.locator('#sheet-details button[type="submit"]').click();
    await page.getByText('Details saved.', { exact: true }).waitFor();
    await page.locator('#sheet-nav button[data-tab="files"]').click();
    await page.locator('.sheet-file-group[data-kind="rom"] a[href]').waitFor();
    const saveGroup = page.locator('.sheet-file-group[data-kind="save"]');
    page.once('dialog', dialog => dialog.accept());
    await saveGroup.locator('input[type="file"]').setInputFiles({ name: 'progress.srm', mimeType: 'application/octet-stream', buffer: Buffer.from('native save fixture') });
    await page.getByText('In-game save stored successfully.', { exact: true }).waitFor();
    const downloadUrl = await saveGroup.locator('a').getAttribute('href');
    assert.equal(await (await page.request.get(process.env.WEBUI_TEST_URL + downloadUrl)).text(), 'native save fixture');
    const stateGroup = page.locator('.sheet-file-group[data-kind="state"]');
    await page.waitForFunction(() => document.querySelector('.state-preview img')?.naturalWidth > 0);
    await stateGroup.locator('.state-preview-image').click();
    await page.locator('.lightbox').waitFor(); await page.keyboard.press('Escape');
    await stateGroup.locator('select').selectOption('');
    await stateGroup.locator('input[type="number"]').fill('3');
    page.once('dialog', dialog => dialog.accept());
    await stateGroup.locator('input[type="file"]').setInputFiles({ name: 'progress.state', mimeType: 'application/octet-stream', buffer: Buffer.from('native state fixture') });
    await page.getByText('RetroArch savestate stored successfully.', { exact: true }).waitFor();
    assert.match(await stateGroup.locator('select').innerText(), /\.state3/);
    await page.locator('#sheet-nav button[data-tab="media"]').click();
    await page.keyboard.press('Escape');
    await sheet.waitFor({ state: 'hidden' });
    assert.match(await page.locator('.game-card', { hasText: 'Donkey Kong Country 2' }).locator('.media-badges').innerText(), /Fan art/);
    // The cards' picture: any of four kinds, remembered; a game without it shows the kind's drawing.
    await page.locator('label:has(input[name="games-view"][value="box3d"])').click();
    await page.waitForFunction(() => document.querySelector('#games-grid').dataset.view === 'box3d');
    assert.equal(await page.locator('.game-cover.none').count(), 5);
    await page.reload();
    await page.waitForFunction(() => document.querySelectorAll('.game-card').length === 5);
    assert.equal(await page.locator('input[name="games-view"][value="box3d"]').isChecked(), true);
    await page.locator('label:has(input[name="games-view"][value="cover"])').click();
    await page.waitForFunction(() => {
      const covers = [...document.querySelectorAll('.game-cover img:not(.color-icon)')];
      return covers.length === 4 && covers.every(img => img.complete && img.naturalWidth > 0);
    });
    // The checkbox still selects without opening.
    await page.locator('.game-card', { hasText: 'Donkey Kong Country 2' }).locator('.game-select').click();
    assert.equal(await sheet.isVisible(), false);
    await page.locator('.game-card', { hasText: 'Donkey Kong Country 2' }).locator('.game-select').click();
    await page.locator('#add-game-open').click();
    await page.locator('#add-system').selectOption('snes');
    await page.locator('#add-file').setInputFiles({name:'Browser Homebrew.sfc', mimeType:'application/octet-stream', buffer:Buffer.from('synthetic game')});
    await page.locator('[data-add-tab=details]').click();
    await page.locator('#add-name').fill('My Browser Homebrew');
    await page.locator('#add-developer').fill('Homebrew author');
    await page.locator('[data-add-tab=media]').click();
    await page.locator('#add-download').click();
    assert.equal(await page.locator('#add-download').getAttribute('aria-pressed'),'false');
    assert.match(await page.locator('#add-submit').innerText(),/Add to library/);
    await page.locator('#add-media-file').setInputFiles({name:'cover.png', mimeType:'image/png', buffer:Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4////fwAJ+wP9KobjigAAAABJRU5ErkJggg==','base64')});
    await page.locator('[data-add-tab=details]').click();
    assert.equal(await page.locator('#add-developer').inputValue(),'Homebrew author');
    await page.locator('#add-submit').click();
    await page.locator('#add-game').waitFor({state:'hidden'});
    await page.locator('#sheet-title').filter({hasText:'My Browser Homebrew'}).waitFor();
    await page.locator('.media-row-item:not(.missing)').waitFor();
    assert.equal(await page.locator('.media-row-item:not(.missing)').count(),1);
    await page.locator('#sheet-close').click();
    assert.equal(await page.locator('.game-card').count(),6);
    assert.ok(await page.locator('.sidebar nav .color-icon').count() >= 6);
    // Color artwork uses bundled assets; real flags and every known system resolve.
    assert.equal(await page.locator('.game-platform .system-icon img').count(),6);
    const artwork = await page.evaluate(() => ({ systems:Object.values(SYSTEM_ICONS), flags:['us','eu','jp','kr'].map(f => iconNode('flag-'+f).getAttribute('src')) }));
    assert.equal(artwork.systems.length,50);
    for (const src of [...artwork.systems,...artwork.flags]) assert.equal((await page.request.get(process.env.WEBUI_TEST_URL+'/'+src)).status(),200,src);
    assert.equal((await page.request.get(process.env.WEBUI_TEST_URL+'/assets/systems/../../retroarch.cfg')).status(),404);
    // Adding with downloads stays in a live progress view and opens fresh artwork.
    await page.locator('#add-game-open').click();
    await page.locator('#add-system').selectOption('snes');
    await page.locator('#add-file').setInputFiles({name:'Super Metroid Download Check.sfc',mimeType:'application/octet-stream',buffer:Buffer.from('synthetic download-flow backup')});
    await page.locator('[data-add-tab=details]').click();
    await page.locator('#add-name').fill('Super Metroid (Japan, USA) (En,Ja)');
    await page.locator('[data-add-tab=media]').click();
    assert.equal(await page.locator('#add-download').getAttribute('aria-pressed'),'true');
    assert.match(await page.locator('#add-submit').innerText(),/Add game & download media/);
    // Keep the running phase visible even on a fast local fixture.
    let polls = 0, lastAddJob;
    await page.route('**/api/scraper/job?id=*',async route => {
      const response = await route.fetch(), data = await response.json(); lastAddJob = data.job;
      if (data.job && polls++ < 2) data.job.state = 'running';
      await route.fulfill({json:data});
    });
    await page.locator('#add-submit').click();
    // It shows once the backup, its details and the download start are through (the page
    // allows the start 60 s); a timeout says what the dialog said instead.
    await page.locator('#add-activity').waitFor({state:'visible',timeout:90000}).catch(async error => {
      throw new Error(`Add Game never showed its downloads: ${await page.locator('#add-status').innerText()} (${error.message.split('\n')[0]})`); });
    assert.equal(await page.locator('#game-sheet').isVisible(),false);
    await page.locator('#add-submit').filter({hasText:'Open game'}).waitFor({timeout:30000});
    assert.ok(await page.locator('#add-live-media .add-live-item').count() > 0,JSON.stringify(lastAddJob));
    if (process.env.WEBUI_ADD_SCREENSHOTS) {
      await page.screenshot({path:process.env.WEBUI_ADD_SCREENSHOTS+'-desktop.png'});
      await page.setViewportSize({width:390,height:844});
      await page.screenshot({path:process.env.WEBUI_ADD_SCREENSHOTS+'-mobile.png'});
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1),true);
      await page.setViewportSize({width:1280,height:720});
    }
    await page.locator('#add-submit').click();
    await page.locator('#add-game').waitFor({state:'hidden'});
    await page.locator('#sheet-title').filter({hasText:'Super Metroid'}).waitFor();
    // The overlay draws its rows from a second request after the title: waited for, not raced.
    await page.locator('.media-row-item:not(.missing)').first().waitFor({timeout:10000}).catch(() => {});
    assert.ok(await page.locator('.media-row-item:not(.missing)').count() > 0,'downloaded artwork is visible without reloading');
    await page.locator('#sheet-close').click();
    await page.unroute('**/api/scraper/job?id=*');
    // Sorting/filtering the complete library, across systems, before the 300-card limit.
    const game = (name, details = {}) => ({ name, label: name, key: name, path: `/app0/content/${name}.zip`, media: [], ...details });
    const library = { systems: [
      { id: 'snes', name: 'Super Nintendo', games: [game('Zelda', { released: '1991', rating: '0.9', genre: 'Adventure', developer: 'Nintendo', publisher: 'Nintendo' }), game('Alpha 10'), game('Éclair', { released: '1994-04-18', rating: '0.8' })] },
      { id: 'nes', name: 'Nintendo', games: [game('Alpha 2', { released: '1987-01', rating: '0', media: ['cover', 'screenshot'] }), game('1942', { released: '1985-12-11', rating: '0.5' }), game('!Unknown', { released: 'unknown', rating: 'unrated' })] }
    ] };
    const libraryRoute = route => route.fulfill({ json: library });
    await page.route('**/api/library', libraryRoute);
    await page.locator('#refresh-games').click();
    await page.locator('.game-info strong').filter({hasText: '!Unknown'}).waitFor();
    const titles = () => page.locator('.game-info strong').allTextContents();
    assert.deepEqual(await titles(), ['!Unknown', '1942', 'Alpha 2', 'Alpha 10', 'Éclair', 'Zelda']);
    await page.locator('.game-card', { hasText: 'Alpha 2' }).locator('.game-select').check();
    const letter = value => page.locator(`#games-letters button[data-letter="${value}"]`);
    await letter('A').click();
    assert.deepEqual(await titles(), ['Alpha 2', 'Alpha 10']);
    await page.locator('#games-direction').click();
    assert.deepEqual(await titles(), ['Alpha 10', 'Alpha 2']);
    assert.equal(await page.locator('.game-card', { hasText: 'Alpha 2' }).locator('.game-select').isChecked(), true);
    await page.locator('#games-system').selectOption('snes');
    assert.deepEqual(await titles(), ['Alpha 10']);
    await page.locator('#games-search').fill('2');
    assert.deepEqual(await titles(), []);
    assert.match(await page.locator('#games-grid').innerText(), /No game matches/);
    await page.locator('#games-search').fill('');
    await page.locator('#games-system').selectOption('');
    await page.locator('#games-missing').check();
    assert.deepEqual(await titles(), ['Alpha 10']);
    await page.locator('#games-missing').uncheck();
    await letter('E').click();
    assert.deepEqual(await titles(), ['Éclair']);
    await letter('#').click();
    assert.deepEqual(await titles(), ['1942', '!Unknown']);
    await letter('').click();
    await page.locator('#games-sort').selectOption('released');
    assert.deepEqual(await titles(), ['Éclair', 'Zelda', 'Alpha 2', '1942', '!Unknown', 'Alpha 10']);
    await page.locator('#games-direction').click();
    assert.deepEqual(await titles(), ['1942', 'Alpha 2', 'Zelda', 'Éclair', '!Unknown', 'Alpha 10']);
    await page.locator('#games-sort').selectOption('rating');
    assert.deepEqual(await titles(), ['Alpha 2', '1942', 'Éclair', 'Zelda', '!Unknown', 'Alpha 10']);
    await page.locator('#games-direction').click();
    assert.deepEqual(await titles(), ['Zelda', 'Éclair', '1942', 'Alpha 2', '!Unknown', 'Alpha 10']);
    for (const field of ['genre', 'developer', 'publisher']) {
      await page.locator('#games-sort').selectOption(field);
      assert.equal((await titles())[0], 'Zelda', `${field}: missing details stay last`);
    }
    await page.locator('#games-sort').selectOption('system');
    assert.deepEqual(await titles(), ['Alpha 10', 'Éclair', 'Zelda', '!Unknown', '1942', 'Alpha 2']);
    await page.reload();
    await page.waitForFunction(() => document.querySelectorAll('.game-card').length === 6);
    assert.equal(await page.locator('#games-sort').inputValue(), 'system');
    assert.match(await page.locator('#games-direction').innerText(), /Descending/);
    await page.locator('#games-sort').selectOption('name');
    await page.locator('#games-direction').click();
    if (process.env.WEBUI_GAMES_SCREENSHOTS) await page.screenshot({ path: process.env.WEBUI_GAMES_SCREENSHOTS + '-desktop.png', fullPage: true });
    await page.setViewportSize({ width: 390, height: 844 });
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth + 1), true, 'Games has no sideways scroll on a phone');
    await letter('Z').focus(); await page.keyboard.press('Enter');
    assert.deepEqual(await titles(), ['Zelda']);
    if (process.env.WEBUI_GAMES_SCREENSHOTS) await page.screenshot({ path: process.env.WEBUI_GAMES_SCREENSHOTS + '-phone.png', fullPage: true });
    await letter('').click();
    library.systems[0].games.push(...Array.from({ length: 301 }, (_, i) => game(`Bulk ${i}`)));
    await page.locator('#refresh-games').click();
    await page.waitForFunction(() => document.querySelectorAll('.game-card').length === 300);
    await page.locator('#games-direction').click();
    assert.equal((await titles())[0], 'Zelda', 'sort before slicing the first 300');
    await page.locator('.games-more').click();
    assert.equal(await page.locator('.game-card').count(), 307);
    await letter('A').click(); await letter('').click();
    assert.equal(await page.locator('.game-card').count(), 300, 'filter resets pagination');
    assert.match(await page.locator('#games-results').innerText(), /307 of 307 games/);
    await page.unroute('**/api/library', libraryRoute);
    // The method is remembered on the console.
    await page.goto(process.env.WEBUI_TEST_URL + '/#media');
    await page.waitForFunction(() => document.querySelector('input[name="scrape-method"][value="ps5"]').checked);
    assert.match(await page.locator('#scrape-method-hint').innerText(), /downloads everything itself/);
    await page.setViewportSize({ width: 390, height: 844 });
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth + 1), true, 'no sideways scroll on a phone');
    assert.deepEqual(errors, []);
    console.log('PASS: method asked then remembered, media tiles, system scraped on the PS5 with live stats, ambiguous game resolved, recap by kind and game, covers shown, the game overlay with its media rows and an upload, phone width');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exit(1); });
