/* Lucide contributors, ISC; Feather contributors, MIT. See assets/lucide/LICENSE. */
'use strict';
const WEBUI_ICONS = {
  "arrow-down": "<path d=\"M12 5v14\" />\n  <path d=\"m19 12-7 7-7-7\" />",
  "arrow-right": "<path d=\"M5 12h14\" />\n  <path d=\"m12 5 7 7-7 7\" />",
  "arrow-up-down": "<path d=\"m21 16-4 4-4-4\" />\n  <path d=\"M17 20V4\" />\n  <path d=\"m3 8 4-4 4 4\" />\n  <path d=\"M7 4v16\" />",
  "arrow-up": "<path d=\"m5 12 7-7 7 7\" />\n  <path d=\"M12 19V5\" />",
  "book-image": "<path d=\"m20 13.7-2.1-2.1a2 2 0 0 0-2.8 0L9.7 17\" />\n  <path d=\"M4 19.5v-15A2.5 2.5 0 0 1 6.5 2H19a1 1 0 0 1 1 1v18a1 1 0 0 1-1 1H6.5a1 1 0 0 1 0-5H20\" />\n  <circle cx=\"10\" cy=\"8\" r=\"2\" />",
  "book-open": "<path d=\"M12 5v16\" />\n  <path d=\"M20.001 19A2 2 0 0022 17V5a2 2 0 00-1.999-2L16 3.002A5 5 0 0012 5a5 5 0 00-4-2H4a2 2 0 00-2 2v12a2 2 0 001.999 2H8a5 5 0 014 2 5 5 0 014-2z\" />",
  "box": "<path d=\"M21 8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16Z\" />\n  <path d=\"m3.3 7 8.7 5 8.7-5\" />\n  <path d=\"M12 22V12\" />",
  "check": "<path d=\"M20 6 9 17l-5-5\" />",
  "chevron-down": "<path d=\"m6 9 6 6 6-6\" />",
  "chevron-left": "<path d=\"m15 18-6-6 6-6\" />",
  "chevron-right": "<path d=\"m9 18 6-6-6-6\" />",
  "chevron-up": "<path d=\"m18 15-6-6-6 6\" />",
  "circle-alert": "<circle cx=\"12\" cy=\"12\" r=\"10\" />\n  <line x1=\"12\" x2=\"12\" y1=\"8\" y2=\"12\" />\n  <line x1=\"12\" x2=\"12.01\" y1=\"16\" y2=\"16\" />",
  "clapperboard": "<path d=\"m12.296 3.464 3.02 3.956\" />\n  <path d=\"M20.2 6 3 11l-.9-2.4c-.3-1.1.3-2.2 1.3-2.5l13.5-4c1.1-.3 2.2.3 2.5 1.3z\" />\n  <path d=\"M3 11h18v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z\" />\n  <path d=\"m6.18 5.276 3.1 3.899\" />",
  "cloud-upload": "<path d=\"M12 13v8\" />\n  <path d=\"M4 14.899A7 7 0 1 1 15.71 8h1.79a4.5 4.5 0 0 1 2.5 8.242\" />\n  <path d=\"m8 17 4-4 4 4\" />",
  "database": "<ellipse cx=\"12\" cy=\"5\" rx=\"9\" ry=\"3\" />\n  <path d=\"M3 5V19A9 3 0 0 0 21 19V5\" />\n  <path d=\"M3 12A9 3 0 0 0 21 12\" />",
  "disc-3": "<circle cx=\"12\" cy=\"12\" r=\"10\" />\n  <path d=\"M6 12c0-1.7.7-3.2 1.8-4.2\" />\n  <circle cx=\"12\" cy=\"12\" r=\"2\" />\n  <path d=\"M18 12c0 1.7-.7 3.2-1.8 4.2\" />",
  "download": "<path d=\"M12 15V3\" />\n  <path d=\"M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4\" />\n  <path d=\"m7 10 5 5 5-5\" />",
  "external-link": "<path d=\"M15 3h6v6\" />\n  <path d=\"M10 14 21 3\" />\n  <path d=\"M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6\" />",
  "file-text": "<path d=\"M6 22a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h8a2.4 2.4 0 0 1 1.704.706l3.588 3.588A2.4 2.4 0 0 1 20 8v12a2 2 0 0 1-2 2z\" />\n  <path d=\"M14 2v5a1 1 0 0 0 1 1h5\" />\n  <path d=\"M10 9H8\" />\n  <path d=\"M16 13H8\" />\n  <path d=\"M16 17H8\" />",
  "flag": "<path d=\"M4 22V4a1 1 0 0 1 .4-.8A6 6 0 0 1 8 2c3 0 5 2 7.333 2q2 0 3.067-.8A1 1 0 0 1 20 4v10a1 1 0 0 1-.4.8A6 6 0 0 1 16 16c-3 0-5-2-8-2a6 6 0 0 0-4 1.528\" />",
  "folder-open": "<path d=\"m6 14 1.5-2.9A2 2 0 0 1 9.24 10H20a2 2 0 0 1 1.94 2.5l-1.54 6a2 2 0 0 1-1.95 1.5H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h3.9a2 2 0 0 1 1.69.9l.81 1.2a2 2 0 0 0 1.67.9H18a2 2 0 0 1 2 2v2\" />",
  "folder": "<path d=\"M20 20a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.9a2 2 0 0 1-1.69-.9L9.6 3.9A2 2 0 0 0 7.93 3H4a2 2 0 0 0-2 2v13a2 2 0 0 0 2 2Z\" />",
  "gallery-horizontal-end": "<path d=\"M2 7v10\" />\n  <path d=\"M6 5v14\" />\n  <rect width=\"12\" height=\"18\" x=\"10\" y=\"3\" rx=\"2\" />",
  "gamepad-2": "<line x1=\"6\" x2=\"10\" y1=\"11\" y2=\"11\" />\n  <line x1=\"8\" x2=\"8\" y1=\"9\" y2=\"13\" />\n  <line x1=\"15\" x2=\"15.01\" y1=\"12\" y2=\"12\" />\n  <line x1=\"18\" x2=\"18.01\" y1=\"10\" y2=\"10\" />\n  <path d=\"M17.32 5H6.68a4 4 0 0 0-3.978 3.59c-.006.052-.01.101-.017.152C2.604 9.416 2 14.456 2 16a3 3 0 0 0 3 3c1 0 1.5-.5 2-1l1.414-1.414A2 2 0 0 1 9.828 16h4.344a2 2 0 0 1 1.414.586L17 18c.5.5 1 1 2 1a3 3 0 0 0 3-3c0-1.545-.604-6.584-.685-7.258-.007-.05-.011-.1-.017-.151A4 4 0 0 0 17.32 5z\" />",
  "globe": "<circle cx=\"12\" cy=\"12\" r=\"10\" />\n  <path d=\"M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20\" />\n  <path d=\"M2 12h20\" />",
  "hard-drive": "<path d=\"M10 16h.01\" />\n  <path d=\"M2.212 11.577a2 2 0 0 0-.212.896V18a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2v-5.527a2 2 0 0 0-.212-.896L18.55 5.11A2 2 0 0 0 16.76 4H7.24a2 2 0 0 0-1.79 1.11z\" />\n  <path d=\"M21.946 12.013H2.054\" />\n  <path d=\"M6 16h.01\" />",
  "image": "<rect width=\"18\" height=\"18\" x=\"3\" y=\"3\" rx=\"2\" ry=\"2\" />\n  <circle cx=\"9\" cy=\"9\" r=\"2\" />\n  <path d=\"m21 15-3.086-3.086a2 2 0 0 0-2.828 0L6 21\" />",
  "images": "<path d=\"m22 11-1.296-1.296a2.4 2.4 0 0 0-3.408 0L11 16\" />\n  <path d=\"M4 8a2 2 0 0 0-2 2v10a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2\" />\n  <circle cx=\"13\" cy=\"7\" r=\"1\" fill=\"currentColor\" />\n  <rect x=\"8\" y=\"2\" width=\"14\" height=\"14\" rx=\"2\" />",
  "info": "<circle cx=\"12\" cy=\"12\" r=\"10\" />\n  <path d=\"M12 16v-4\" />\n  <path d=\"M12 8h.01\" />",
  "layout-dashboard": "<rect width=\"7\" height=\"9\" x=\"3\" y=\"3\" rx=\"1\" />\n  <rect width=\"7\" height=\"5\" x=\"14\" y=\"3\" rx=\"1\" />\n  <rect width=\"7\" height=\"9\" x=\"14\" y=\"12\" rx=\"1\" />\n  <rect width=\"7\" height=\"5\" x=\"3\" y=\"16\" rx=\"1\" />",
  "library-big": "<rect width=\"8\" height=\"18\" x=\"3\" y=\"3\" rx=\"1\" />\n  <path d=\"M7 3v18\" />\n  <path d=\"M20.4 18.9c.2.5-.1 1.1-.6 1.3l-1.9.7c-.5.2-1.1-.1-1.3-.6L11.1 5.1c-.2-.5.1-1.1.6-1.3l1.9-.7c.5-.2 1.1.1 1.3.6Z\" />",
  "list": "<path d=\"M3 5h.01\" />\n  <path d=\"M3 12h.01\" />\n  <path d=\"M3 19h.01\" />\n  <path d=\"M8 5h13\" />\n  <path d=\"M8 12h13\" />\n  <path d=\"M8 19h13\" />",
  "monitor": "<rect width=\"20\" height=\"14\" x=\"2\" y=\"3\" rx=\"2\" />\n  <line x1=\"8\" x2=\"16\" y1=\"21\" y2=\"21\" />\n  <line x1=\"12\" x2=\"12\" y1=\"17\" y2=\"21\" />",
  "moon": "<path d=\"M20.985 12.486a9 9 0 1 1-9.473-9.472c.405-.022.617.46.402.803a6 6 0 0 0 8.268 8.268c.344-.215.825-.004.803.401\" />",
  "panels-top-left": "<rect width=\"18\" height=\"18\" x=\"3\" y=\"3\" rx=\"2\" />\n  <path d=\"M3 9h18\" />\n  <path d=\"M9 21V9\" />",
  "pencil": "<path d=\"M21.174 6.812a1 1 0 0 0-3.986-3.987L3.842 16.174a2 2 0 0 0-.5.83l-1.321 4.352a.5.5 0 0 0 .623.622l4.353-1.32a2 2 0 0 0 .83-.497z\" />\n  <path d=\"m15 5 4 4\" />",
  "plus": "<path d=\"M5 12h14\" />\n  <path d=\"M12 5v14\" />",
  "save": "<path d=\"M15.2 3a2 2 0 0 1 1.4.6l3.8 3.8a2 2 0 0 1 .6 1.4V19a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z\" />\n  <path d=\"M17 21v-7a1 1 0 0 0-1-1H8a1 1 0 0 0-1 1v7\" />\n  <path d=\"M7 3v4a1 1 0 0 0 1 1h7\" />",
  "search": "<path d=\"m21 21-4.34-4.34\" />\n  <circle cx=\"11\" cy=\"11\" r=\"8\" />",
  "settings-2": "<path d=\"M14 17H5\" />\n  <path d=\"M19 7h-9\" />\n  <circle cx=\"17\" cy=\"17\" r=\"3\" />\n  <circle cx=\"7\" cy=\"7\" r=\"3\" />",
  "sun": "<circle cx=\"12\" cy=\"12\" r=\"4\" />\n  <path d=\"M12 2v2\" />\n  <path d=\"M12 20v2\" />\n  <path d=\"m4.93 4.93 1.41 1.41\" />\n  <path d=\"m17.66 17.66 1.41 1.41\" />\n  <path d=\"M2 12h2\" />\n  <path d=\"M20 12h2\" />\n  <path d=\"m6.34 17.66-1.41 1.41\" />\n  <path d=\"m19.07 4.93-1.41 1.41\" />",
  "tower-control": "<path d=\"M18.2 12.27 20 6H4l1.8 6.27a1 1 0 0 0 .95.73h10.5a1 1 0 0 0 .96-.73Z\" />\n  <path d=\"M8 13v9\" />\n  <path d=\"M16 22v-9\" />\n  <path d=\"m9 6 1 7\" />\n  <path d=\"m15 6-1 7\" />\n  <path d=\"M12 6V2\" />\n  <path d=\"M13 2h-2\" />",
  "type": "<path d=\"M12 4v16\" />\n  <path d=\"M4 7V5a1 1 0 0 1 1-1h14a1 1 0 0 1 1 1v2\" />\n  <path d=\"M9 20h6\" />",
  "upload": "<path d=\"M12 3v12\" />\n  <path d=\"m17 8-5-5-5 5\" />\n  <path d=\"M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4\" />",
  "video": "<path d=\"m16 13 5.223 3.482a.5.5 0 0 0 .777-.416V7.87a.5.5 0 0 0-.752-.432L16 10.5\" />\n  <rect x=\"2\" y=\"6\" width=\"14\" height=\"12\" rx=\"2\" />",
  "volume-2": "<path d=\"M11 4.702a.705.705 0 0 0-1.203-.498L6.413 7.587A1.4 1.4 0 0 1 5.416 8H3a1 1 0 0 0-1 1v6a1 1 0 0 0 1 1h2.416a1.4 1.4 0 0 1 .997.413l3.383 3.384A.705.705 0 0 0 11 19.298z\" />\n  <path d=\"M16 9a5 5 0 0 1 0 6\" />\n  <path d=\"M19.364 18.364a9 9 0 0 0 0-12.728\" />",
  "wallpaper": "<path d=\"M12 17v4\" />\n  <path d=\"M8 21h8\" />\n  <path d=\"m9 17 6.1-6.1a2 2 0 0 1 2.81.01L22 15\" />\n  <circle cx=\"8\" cy=\"9\" r=\"2\" />\n  <rect x=\"2\" y=\"3\" width=\"20\" height=\"14\" rx=\"2\" />",
  "x": "<path d=\"M18 6 6 18\" />\n  <path d=\"m6 6 12 12\" />",
  "zoom-in": "<circle cx=\"11\" cy=\"11\" r=\"8\" />\n  <line x1=\"21\" x2=\"16.65\" y1=\"21\" y2=\"16.65\" />\n  <line x1=\"11\" x2=\"11\" y1=\"8\" y2=\"14\" />\n  <line x1=\"8\" x2=\"14\" y1=\"11\" y2=\"11\" />"
};
const MEDIA_SYMBOLS = {
  'box': '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32"><path fill="#b896fa" d="m4 8 12-6 12 6-12 6Z"/><path fill="#7450cb" d="m4 8 12 6v16L4 23Z"/><path fill="#986de0" d="m16 14 12-6v15l-12 7Z"/><path fill="#eee1ff" d="m10 5 12 6v6l-4 2v-6L7 7Z"/></svg>',
  'disc-3': '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32"><circle cx="16" cy="16" r="14" fill="#79b7ee"/><path d="M16 2a14 14 0 0 1 14 14H16Z" fill="#b6e5ef"/><path d="M16 30A14 14 0 0 1 2 16h14Z" fill="#b9a2ed"/><circle cx="16" cy="16" r="6" fill="#e9edf6"/><circle cx="16" cy="16" r="2.8" fill="#636886"/><path d="M7 13a10 10 0 0 1 6-6" stroke="#fff" opacity=".8" stroke-width="2" stroke-linecap="round" fill="none"/></svg>'
};
const COLOR_ICONS = {
  "layout-dashboard": "assets/fluent/home.svg",
  "folder-open": "assets/fluent/document-folder.svg",
  "folder": "assets/fluent/document-folder.svg",
  "library-big": "assets/fluent/library.svg",
  "download": "assets/fluent/arrow-square-down.svg",
  "arrow-up-down": "assets/fluent/arrow-sync.svg",
  "settings-2": "assets/fluent/settings.svg",
  "cloud-upload": "assets/fluent/cloud.svg",
  "book-image": "assets/fluent/book.svg",
  "book-open": "assets/fluent/book-open.svg",
  "box": "assets/fluent/briefcase.svg",
  "image": "assets/fluent/image.svg",
  "images": "assets/fluent/content-view.svg",
  "panels-top-left": "assets/fluent/board.svg",
  "type": "assets/fluent/text-edit-style.svg",
  "disc-3": "assets/fluent/coin-multiple.svg",
  "wallpaper": "assets/fluent/image.svg",
  "video": "assets/fluent/video.svg",
  "clapperboard": "assets/fluent/video.svg",
  "database": "assets/fluent/database.svg",
  "file-text": "assets/fluent/document-text.svg",
  "list": "assets/fluent/text-bullet-list-square.svg",
  "monitor": "assets/fluent/laptop.svg",
  "globe": "assets/fluent/globe.svg",
  "pencil": "assets/fluent/edit.svg",
  "save": "assets/fluent/vault.svg",
  "gamepad-2": "assets/fluent/game-chat.svg",
  "hard-drive": "assets/fluent/database.svg",
  "circle-alert": "assets/fluent/warning.svg",
  "info": "assets/fluent/question-circle.svg",
  "check": "assets/fluent/checkmark-circle.svg",
  "sun": "assets/fluent/weather-sunny-low.svg",
  "volume-2": "assets/fluent/headphones.svg",
  "gallery-horizontal-end": "assets/fluent/content-view.svg",
  "tower-control": "assets/fluent/building.svg",
  "flag": "assets/fluent/flag.svg"
};
function assetIcon(src, className = '') {
  const img = document.createElement('img'); img.src = src; img.alt = ''; img.className = 'icon color-icon ' + className; img.setAttribute('aria-hidden','true'); return img;
}
function uiIcon(name, className = '') {
  if (MEDIA_SYMBOLS[name]) {
    const svg = new DOMParser().parseFromString(MEDIA_SYMBOLS[name], 'image/svg+xml').documentElement;
    svg.setAttribute('class', 'icon color-icon ' + className);
    svg.setAttribute('aria-hidden', 'true'); svg.setAttribute('focusable', 'false');
    return svg;
  }
  if (COLOR_ICONS[name]) return assetIcon(COLOR_ICONS[name],className);
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  for (const [key, value] of Object.entries({viewBox:'0 0 24 24', fill:'none', stroke:'currentColor', 'stroke-width':'1.75', 'stroke-linecap':'round', 'stroke-linejoin':'round', 'aria-hidden':'true', focusable:'false', class:'icon lucide ' + className})) svg.setAttribute(key, value);
  svg.innerHTML = WEBUI_ICONS[name] || WEBUI_ICONS['file-text'];
  return svg;
}
for (const node of document.querySelectorAll('[data-ui-icon]')) node.replaceWith(uiIcon(node.dataset.uiIcon, node.className));

const SYSTEM_ICONS = {
  "gba": "assets/systems/gba.webp",
  "gbc": "assets/systems/gbc.webp",
  "gb": "assets/systems/gb.webp",
  "n3ds": "assets/systems/n3ds.webp",
  "nds": "assets/systems/nds.webp",
  "n64": "assets/systems/n64.webp",
  "snes": "assets/systems/snes.webp",
  "fds": "assets/systems/fds.webp",
  "nes": "assets/systems/nes.webp",
  "gc": "assets/systems/gc.webp",
  "wii": "assets/systems/wii.webp",
  "ps2": "assets/systems/ps2.webp",
  "psp": "assets/systems/psp.webp",
  "psx": "assets/systems/psx.webp",
  "virtualboy": "assets/systems/virtualboy.webp",
  "pokemini": "assets/systems/pokemini.webp",
  "segacd": "assets/systems/segacd.webp",
  "sega32x": "assets/systems/sega32x.webp",
  "genesis": "assets/systems/genesis.webp",
  "mastersystem": "assets/systems/mastersystem.webp",
  "gamegear": "assets/systems/gamegear.webp",
  "sg-1000": "assets/systems/sg-1000.webp",
  "saturn": "assets/systems/saturn.webp",
  "dreamcast": "assets/systems/dreamcast.webp",
  "naomi": "assets/systems/naomi.webp",
  "naomi2": "assets/systems/naomi2.webp",
  "atomiswave": "assets/systems/atomiswave.webp",
  "c64": "assets/systems/c64.webp",
  "amiga": "assets/systems/amiga.webp",
  "atari2600": "assets/systems/atari2600.webp",
  "atari5200": "assets/systems/atari5200.webp",
  "atari7800": "assets/systems/atari7800.webp",
  "atarijaguar": "assets/systems/atarijaguar.webp",
  "atarilynx": "assets/systems/atarilynx.webp",
  "supergrafx": "assets/systems/supergrafx.webp",
  "pcfx": "assets/systems/pcfx.webp",
  "pcengine": "assets/systems/pcengine.webp",
  "ngpc": "assets/systems/ngpc.webp",
  "ngp": "assets/systems/ngp.webp",
  "wonderswancolor": "assets/systems/wonderswancolor.webp",
  "wonderswan": "assets/systems/wonderswan.webp",
  "3do": "assets/systems/3do.webp",
  "dos": "assets/systems/dos.webp",
  "scummvm": "assets/systems/scummvm.webp",
  "neogeocd": "assets/systems/neogeocd.webp",
  "msx": "assets/systems/msx.webp",
  "neogeo": "assets/systems/neogeo.webp",
  "cps": "assets/systems/cps.webp",
  "fbneo": "assets/systems/fbneo.webp",
  "mame": "assets/systems/mame.webp",
  "arcade": "assets/systems/arcade.webp"
};
function systemIcon(id) {
  const frame = document.createElement('span'); frame.className = 'system-icon'; frame.setAttribute('aria-hidden','true');
  if (SYSTEM_ICONS[id]) { const img = assetIcon(SYSTEM_ICONS[id]); img.loading = 'lazy'; frame.append(img); } else frame.append(uiIcon('library-big'));
  return frame;
}
