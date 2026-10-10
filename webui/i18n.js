/* The WebUI in the reader's language. The page is written in English; each English text
 * it shows (webui/i18n/strings.json, tools/webui-strings.py) is shown from the chosen
 * language's catalog (webui/i18n/<code>.json), whole texts only: a game's name or a
 * file's never matches. A text with {0}, {1}... matches the values put in it. The choice
 * is kept on the console (/api/preferences), so every device opening the WebUI uses it. */
'use strict';
const LANGUAGES = [
  ['en', 'English', 'English', 'us'], ['es', 'Español', 'Spanish', 'es'], ['fr', 'Français', 'French', 'fr'],
  ['de', 'Deutsch', 'German', 'de'], ['it', 'Italiano', 'Italian', 'it'], ['pt-BR', 'Português (Brasil)', 'Portuguese', 'br'],
  ['ru', 'Русский', 'Russian', 'ru'], ['ja', '日本語', 'Japanese', 'jp'], ['ko', '한국어', 'Korean', 'kr'],
  ['zh-CN', '简体中文', 'Chinese (Simplified)', 'cn'], ['zh-TW', '繁體中文', 'Chinese (Traditional)', 'tw'],
  ['vi', 'Tiếng Việt', 'Vietnamese', 'vn'], ['tr', 'Türkçe', 'Turkish', 'tr'], ['pl', 'Polski', 'Polish', 'pl'],
  ['nl', 'Nederlands', 'Dutch', 'nl'], ['id', 'Bahasa Indonesia', 'Indonesian', 'id'],
].map(([code, name, english, flag]) => ({ code, name, english, flag }));

const i18n = (() => {
  let language = 'en', table = null, exact = new Map(), byWord = new Map(), leading = [];
  let unsaved = null; // A choice the console has not stored yet (offline when it was made).
  const english = new WeakMap(), shown = new WeakMap(), attributes = new WeakMap();
  const ATTRIBUTES = ['placeholder', 'title', 'aria-label', 'alt'];
  const SKIP = 'script, style, code, pre, textarea, [data-no-translate]';
  const listeners = [];
  let observer = null, applying = false, request = 0;

  const normal = text => text.replace(/\s+/g, ' ').trim();
  const escape = text => text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');

  // A catalog: exact texts, and those with placeholders as patterns, indexed by first word.
  function load(catalog) {
    table = catalog; exact = new Map(); byWord = new Map(); leading = [];
    for (const [key, value] of Object.entries(catalog || {})) {
      if (!/\{\d+\}/.test(key)) { exact.set(key, value); continue; }
      // A value glued to the text before it, or ending the text, may be empty (an
      // English plural "s", an optional note): "{0} settings{1}" reads "2 settings".
      const order = [], parts = key.split(/(\{\d+\})/);
      const source = parts.map((part, i) => {
        const m = /^\{(\d+)\}$/.exec(part);
        if (!m) return escape(part);
        order.push(+m[1]);
        const glued = i > 0 && /\S$/.test(parts.slice(0, i).join('')), last = parts.slice(i + 1).join('') === '';
        return glued || last ? '(.*?)' : '(.+?)';
      }).join('');
      const pattern = { regex: new RegExp('^' + source + '$', 's'), order, value };
      const first = key.split(' ')[0];
      if (/^\{\d+\}/.test(first)) leading.push(pattern);
      else { if (!byWord.has(first)) byWord.set(first, []); byWord.get(first).push(pattern); }
    }
  }
  // An English text in the chosen language, or null when the catalog has none.
  function lookup(text, depth = 0) {
    if (!table) return null;
    const found = exact.get(text);
    if (found !== undefined) return found;
    for (const pattern of [...(byWord.get(text.split(' ')[0]) || []), ...leading]) {
      const match = pattern.regex.exec(text);
      if (!match) continue;
      return pattern.value.replace(/\{(\d+)\}/g, (_, n) => {
        const value = match[pattern.order.indexOf(+n) + 1] ?? '';
        return (depth < 1 && lookup(value, depth + 1)) || value;
      });
    }
    // Sentences joined by " · " or ". ": each in turn.
    for (const joint of [' · ', '. ']) {
      if (depth < 1 && text.includes(joint)) {
        const pieces = text.split(joint), out = pieces.map(piece => lookup(piece, depth + 1));
        if (out.some(Boolean)) return pieces.map((piece, i) => out[i] || piece).join(joint);
      }
    }
    return null;
  }
  // A string the page's scripts show outside the page (dialogs, prompts).
  function t(text) { return (language !== 'en' && lookup(normal(text))) || text; }

  function translateNode(node) {
    const value = node.nodeValue;
    // The page changed the text since it was translated: that is its new English.
    if (!english.has(node) || shown.get(node) !== value) english.set(node, value);
    const source = english.get(node);
    if (!/[A-Za-z]/.test(source)) return;
    let text = source;
    if (language !== 'en') {
      const core = normal(source), found = core && lookup(core);
      if (found) text = source.slice(0, source.length - source.trimStart().length) + found +
        source.slice(source.trimEnd().length);
    }
    shown.set(node, text);
    if (text !== value) node.nodeValue = text;
  }
  function translateAttributes(element) {
    let originals = attributes.get(element);
    for (const name of ATTRIBUTES) {
      const value = element.getAttribute(name);
      if (value === null) continue;
      if (!originals) attributes.set(element, originals = {});
      if (!originals[name] || originals[name].shown !== value) originals[name] = { english: value };
      const found = language !== 'en' && lookup(normal(originals[name].english));
      const text = found || originals[name].english;
      originals[name].shown = text;
      if (text !== value) element.setAttribute(name, text);
    }
  }
  function translate(root) {
    if (!root) return;
    if (root.nodeType === Node.TEXT_NODE) {
      if (!root.parentElement?.closest(SKIP)) translateNode(root);
      return;
    }
    if (root.nodeType !== Node.ELEMENT_NODE || root.closest(SKIP)) return;
    translateAttributes(root);
    const walker = document.createTreeWalker(root, NodeFilter.SHOW_ELEMENT | NodeFilter.SHOW_TEXT, {
      acceptNode: node => node.nodeType === Node.ELEMENT_NODE && node.matches(SKIP) ? NodeFilter.FILTER_REJECT : NodeFilter.FILTER_ACCEPT });
    for (let node = walker.nextNode(); node; node = walker.nextNode()) {
      if (node.nodeType === Node.TEXT_NODE) translateNode(node);
      else translateAttributes(node);
    }
  }
  // Text the scripts write later is translated as it appears.
  function observe() {
    if (observer) return;
    observer = new MutationObserver(records => {
      if (applying) return;
      applying = true;
      try {
        for (const record of records) {
          if (record.type === 'characterData') translate(record.target);
          else if (record.type === 'attributes') { if (!record.target.closest(SKIP)) translateAttributes(record.target); }
          else for (const node of record.addedNodes) translate(node);
        }
      } finally { applying = false; }
    });
    observer.observe(document.documentElement, { subtree: true, childList: true, characterData: true,
      attributes: true, attributeFilter: ATTRIBUTES });
  }
  async function save(code) {
    try { await api('/api/preferences?language=' + encodeURIComponent(code), { method: 'POST' }); unsaved = null; }
    catch { unsaved = code; }
  }
  // Show the page in a language; "save" also keeps it on the console for every device.
  async function use(code, { save: keep = false } = {}) {
    const known = LANGUAGES.find(l => l.code === code) || LANGUAGES[0], mine = ++request;
    let catalog = null;
    if (known.code !== 'en') {
      try { catalog = await (await fetch(`i18n/${known.code}.json`, { cache: 'no-cache' })).json(); }
      catch { return false; }
      if (mine !== request) return false; // A later choice won.
    }
    language = known.code; load(catalog);
    document.documentElement.lang = known.code;
    try { localStorage.setItem('ps5-language', known.code); } catch { /* This visit still uses it. */ }
    applying = true;
    try { translate(document.body); } finally { applying = false; }
    observe();
    for (const listener of listeners) listener(known);
    if (keep) await save(known.code);
    return true;
  }
  // The console's choice, read with its status: another device may have changed it.
  function follow(code) {
    if (unsaved) save(unsaved);
    else if (code && code !== language && LANGUAGES.some(l => l.code === code)) use(code);
  }
  // The language this browser used last, shown before the console answers.
  function remembered() { try { return localStorage.getItem('ps5-language'); } catch { return null; } }
  return { use, follow, t, remembered, get language() { return language; }, onChange: f => listeners.push(f) };
})();

// A language picker: each language behind its flag and its own name. "compact" (the
// header) shows the flag alone on a narrow screen; "inline" (Settings) lists every
// language at once instead of behind a button.
function languagePicker(host, { compact = false, inline = false } = {}) {
  const box = document.createElement('div');
  box.className = 'language-picker' + (compact ? ' compact' : '') + (inline ? ' inline' : '');
  box.setAttribute('data-no-translate', '');
  const list = document.createElement('ul');
  list.className = 'language-list'; list.setAttribute('role', 'listbox');
  list.id = 'language-list-' + Math.random().toString(36).slice(2, 8);
  let button = null, flag = null, name = null;
  if (!inline) {
    button = document.createElement('button'); button.type = 'button'; button.className = 'language-button';
    button.setAttribute('aria-haspopup', 'listbox'); button.setAttribute('aria-expanded', 'false');
    button.setAttribute('aria-controls', list.id);
    flag = document.createElement('img'); flag.className = 'language-flag'; flag.alt = ''; flag.width = 24; flag.height = 18;
    name = document.createElement('span'); name.className = 'language-name';
    const chevron = document.createElement('span'); chevron.className = 'language-chevron'; chevron.setAttribute('aria-hidden', 'true');
    button.append(flag, name, chevron);
    list.hidden = true;
  }
  for (const language of LANGUAGES) {
    const item = document.createElement('li');
    item.setAttribute('role', 'option'); item.dataset.code = language.code; item.tabIndex = -1;
    item.lang = language.code;
    const image = document.createElement('img');
    image.src = `assets/flags/${language.flag}.svg`; image.alt = ''; image.width = 24; image.height = 18;
    const label = document.createElement('span'); label.className = 'language-label';
    const own = document.createElement('strong'); own.textContent = language.name;
    const english = document.createElement('small'); english.textContent = language.english; english.lang = 'en';
    label.append(own, english); item.append(image, label);
    item.addEventListener('click', () => choose(language.code));
    item.addEventListener('keydown', event => {
      const items = [...list.children], at = items.indexOf(item);
      const step = { ArrowDown: 1, ArrowRight: 1, ArrowUp: -1, ArrowLeft: -1 }[event.key];
      if (step) { event.preventDefault(); items[(at + step + items.length) % items.length].focus(); }
      else if (event.key === 'Home') { event.preventDefault(); items[0].focus(); }
      else if (event.key === 'End') { event.preventDefault(); items[items.length - 1].focus(); }
      else if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); choose(language.code); }
      else if (event.key === 'Escape' && button) { close(); button.focus(); }
      else if (event.key === 'Tab' && button) close();
    });
    list.append(item);
  }
  function show(language) {
    if (button) {
      flag.src = `assets/flags/${language.flag}.svg`; name.textContent = language.name;
      button.setAttribute('aria-label', `${i18n.t('Language')}: ${language.name}`);
    }
    list.setAttribute('aria-label', i18n.t('Language'));
    for (const item of list.children) {
      const selected = item.dataset.code === language.code;
      item.setAttribute('aria-selected', String(selected));
      if (inline) item.tabIndex = selected ? 0 : -1;
    }
  }
  function open() {
    list.hidden = false; button.setAttribute('aria-expanded', 'true');
    // Open towards the side with room: the button sits at the left of a phone's header.
    list.style.left = list.style.right = '';
    if (list.getBoundingClientRect().left < 8) { list.style.left = '0'; list.style.right = 'auto'; }
    (list.querySelector('[aria-selected="true"]') || list.firstChild).focus();
  }
  function close() { if (!button) return; list.hidden = true; button.setAttribute('aria-expanded', 'false'); }
  async function choose(code) {
    if (button) { close(); button.focus(); }
    await i18n.use(code, { save: true });
  }
  if (button) {
    button.addEventListener('click', () => (list.hidden ? open() : close()));
    button.addEventListener('keydown', event => {
      if (event.key === 'ArrowDown' || event.key === 'ArrowUp') { event.preventDefault(); open(); }
    });
    document.addEventListener('click', event => { if (!box.contains(event.target)) close(); });
    box.append(button);
  }
  box.append(list);
  host.append(box);
  i18n.onChange(show);
  show(LANGUAGES.find(l => l.code === i18n.language) || LANGUAGES[0]);
  return box;
}
