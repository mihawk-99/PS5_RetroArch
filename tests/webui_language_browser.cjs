/* The language picker against an isolated native WebUI fixture (tests/test_webui_language.py):
 * choosing a language translates the page, the console keeps it for every device, and the
 * picker fits a phone. No production console writes. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require(process.env.PLAYWRIGHT_PATH || 'playwright');
const base = process.env.WEBUI_TEST_URL;
const out = path.resolve(process.env.WEBUI_SCREENSHOTS || 'build/tmp/webui-language');
const strings = JSON.parse(fs.readFileSync('webui/i18n/strings.json', 'utf8'));
const french = JSON.parse(fs.readFileSync('webui/i18n/fr.json', 'utf8'));
const settled = page => page.waitForFunction(() => !document.querySelector('#browse-files').disabled);

// Visible text nodes still in English although the chosen catalog translates them.
function untranslated(page, catalog) {
  return page.evaluate(({ strings, catalog }) => {
    const known = new Set(strings), left = [];
    const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
    for (let node = walker.nextNode(); node; node = walker.nextNode()) {
      const text = node.nodeValue.replace(/\s+/g, ' ').trim(), parent = node.parentElement;
      if (!known.has(text) || catalog[text] === text || parent.closest('[data-no-translate], script, style')) continue;
      if (parent.closest('[hidden]') || !parent.getClientRects().length) continue;
      left.push(text);
    }
    return left;
  }, { strings, catalog });
}

(async () => {
  fs.mkdirSync(out, { recursive: true });
  const browser = await chromium.launch({ headless: true, executablePath: process.env.CHROMIUM_PATH || '/usr/bin/chromium' });
  try {
    const errors = [];
    const open = async (viewport, extra = {}) => {
      const context = await browser.newContext({ viewport, reducedMotion: 'reduce', ...extra });
      await context.route('https://api.github.com/**', route => route.fulfill({ json: [] }));
      const page = await context.newPage();
      page.on('pageerror', e => errors.push(e.message));
      await page.goto(base); await settled(page);
      return { context, page };
    };

    // Desktop: the header picker shows the flag and the language's own name.
    const desk = await open({ width: 1504, height: 1046 });
    let page = desk.page;
    const header = page.locator('#language-header .language-button');
    assert.equal(await page.locator('#language-header .language-name').innerText(), 'English');
    assert.match(await page.locator('#language-header .language-flag').getAttribute('src'), /flags\/us\.svg$/);
    await header.click();
    const options = page.locator('#language-header [role=option]');
    assert.equal(await options.count(), 16);
    assert.equal(await page.evaluate(() => [...document.querySelectorAll('#language-header [role=option] img')].every(i => i.complete && i.naturalWidth > 0)), true, 'every flag loads');
    await page.screenshot({ path: path.join(out, 'header-open-desktop.png') });
    await options.filter({ hasText: 'Français' }).click();
    await page.waitForFunction(() => document.documentElement.lang === 'fr');
    assert.equal(await page.locator('[data-page=overview] span:last-child').innerText(), 'Vue d’ensemble');
    assert.equal(await page.locator('#check-release').innerText(), 'Vérifier à nouveau');
    assert.equal(await page.locator('#language-header .language-name').innerText(), 'Français');
    assert.deepEqual(await untranslated(page, french), [], 'the overview is all French');
    // Text the scripts write later is translated too.
    await page.locator('[data-page=settings]').click();
    await page.waitForFunction(() => document.querySelector('#settings-heading').textContent === 'Réglages globaux de RetroArch');
    assert.equal(await page.locator('#page-title').innerText(), 'Réglages');
    // A count the script writes ("{0} settings").
    await page.waitForFunction(() => /^\d+ réglages$/.test(document.querySelector('#settings-count').textContent));
    assert.deepEqual(await untranslated(page, french), [], 'the settings page is all French');
    // Every other page, as the scripts fill it.
    for (const name of ['content', 'games', 'media', 'transfers']) {
      await page.locator(`[data-page=${name}]`).click();
      await page.waitForFunction(id => !document.getElementById(id).hidden, name);
      await page.waitForTimeout(500);
      assert.deepEqual(await untranslated(page, french), [], `the ${name} page is all French`);
    }
    await page.locator('[data-page=settings]').click();
    // The Settings footer lists every language, the chosen one marked.
    const tiles = page.locator('#language-settings [role=option]');
    assert.equal(await tiles.count(), 16);
    assert.equal(await page.locator('#language-settings [aria-selected=true]').getAttribute('data-code'), 'fr');
    await page.locator('#language-settings').scrollIntoViewIfNeeded();
    await page.screenshot({ path: path.join(out, 'settings-desktop-fr.png') });
    // Kept on the console: a browser that never chose (another device) opens in French.
    const other = await open({ width: 1280, height: 900 });
    await other.page.waitForFunction(() => document.documentElement.lang === 'fr', null, { timeout: 15000 });
    assert.equal(await other.page.locator('#check-release').innerText(), 'Vérifier à nouveau');
    // A choice made on one device reaches the other with its next status check.
    await tiles.filter({ hasText: '日本語' }).click();
    await page.waitForFunction(() => document.documentElement.lang === 'ja');
    assert.equal(await page.locator('#settings-heading').innerText(), 'RetroArch 全体の設定');
    await other.page.evaluate(() => reconnect());
    await other.page.waitForFunction(() => document.documentElement.lang === 'ja');
    await other.context.close();
    // Keyboard: the header list opens with the arrow key and chooses with Enter.
    await header.focus(); await page.keyboard.press('ArrowDown');
    await page.waitForFunction(() => document.activeElement?.dataset.code === 'ja');
    await page.keyboard.press('Home'); await page.keyboard.press('Enter');
    await page.waitForFunction(() => document.documentElement.lang === 'en');
    assert.equal(await page.locator('#settings-heading').innerText(), 'Global RetroArch settings');
    await desk.context.close();

    // Phone: the header keeps the flag alone, the list stays on screen, no sideways scroll.
    const phone = await open({ width: 390, height: 844 }, { isMobile: true, hasTouch: true });
    page = phone.page;
    assert.equal(await page.locator('#language-header .language-name').isVisible(), false);
    assert.equal(await page.locator('#language-header .language-flag').isVisible(), true);
    await page.locator('#language-header .language-button').click();
    const list = await page.locator('#language-header .language-list').boundingBox();
    assert.ok(list.x >= 0 && list.x + list.width <= 390, `the list fits the phone: ${JSON.stringify(list)}`);
    await page.screenshot({ path: path.join(out, 'header-open-phone.png') });
    await page.locator('#language-header [role=option]').filter({ hasText: 'Tiếng Việt' }).click();
    await page.waitForFunction(() => document.documentElement.lang === 'vi');
    await page.screenshot({ path: path.join(out, 'overview-phone-vi.png'), fullPage: true });
    await page.goto(base + '#settings'); await settled(page);
    await page.waitForFunction(() => document.documentElement.lang === 'vi');
    await page.locator('#language-settings').scrollIntoViewIfNeeded();
    await page.screenshot({ path: path.join(out, 'settings-phone-vi.png') });
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true, 'no sideways scroll');
    await page.locator('#language-settings [role=option]').filter({ hasText: 'English' }).click();
    await page.waitForFunction(() => document.documentElement.lang === 'en');
    await phone.context.close();

    assert.deepEqual(errors, []);
    console.log(`language picker: 16 languages, French and Japanese translated, followed across browsers; screenshots in ${out}`);
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exit(1); });
