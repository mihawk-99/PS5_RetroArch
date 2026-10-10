#!/usr/bin/env python3
"""The WebUI's English text, for its translations (webui/i18n/).

    tools/webui-strings.py            write webui/i18n/strings.json
    tools/webui-strings.py --check    exit 1 when strings.json is stale or a language
                                      catalog misses a string or a placeholder

The page is written in English; webui/i18n.js shows each known English text in the
chosen language. The texts are the page's (index.html: text and the placeholder,
title, aria-label and alt attributes) and its scripts' string literals that a person
reads. A template literal's ${...} parts become {0}, {1}... in the key, matched back
at run time.
"""
from __future__ import annotations

import json
import re
import sys
from html.parser import HTMLParser
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WEBUI = ROOT / 'webui'
SCRIPTS = ('app.js', 'add-game.js', 'settings-guide.js')
OUT = WEBUI / 'i18n' / 'strings.json'
ATTRIBUTES = ('placeholder', 'title', 'aria-label', 'alt')
# Never translated: names, paths and identifiers.
KEEP = {'RetroArch', 'PS5', 'RetroArch · PS5', 'ScreenScraper', 'LaunchBox', 'libretro', 'EmuMovies',
        'PS5 RetroArch', 'GitHub', 'primary compact', 'use strict', 'AbortError', 'KiB', 'MiB', 'GiB',
        'TiB', 's commit), so no lane waits on another lane', 'Destination: /app0/content/'}


def normal(text: str) -> str:
    return ' '.join(text.split())


def readable(text: str) -> bool:
    """Text a person reads: words, not code, selectors, keys, paths or markup."""
    if not re.search(r'[A-Za-z]{2}', text) or text in KEEP:
        return False
    if re.search(r'^[#.\[:/]|^\w+(-\w+)+$|^[a-z_]+$|^[a-z]+[A-Z]\w*$|=>|\bfunction\b|querySelector|'
                 r'</?[a-z]|https?://|\.(js|css|json|png|svg|txt|cfg|zip|elf)\b|^\$|\\[nt]|^[A-Z_]{3,}$|'
                 r'^(GET|POST|PUT|DELETE)\b|\bapi/', text):
        return False
    # Selectors and media queries ('input[name="x"]', "(prefers-reduced-motion: reduce)").
    if re.search(r'\w\[\w+=|^\([a-z-]+:', text):
        return False
    # Class lists ("muted alert-part-note"): lowercase words, one hyphenated.
    if re.fullmatch(r'[a-z0-9-]+( [a-z0-9-]+)+', text) and '-' in text:
        return False
    # One lowercase word is a key or a CSS value ("hidden", "running"), not a sentence.
    return ' ' in text or text[:1].isupper()


class Page(HTMLParser):
    def __init__(self):
        super().__init__()
        self.skip = 0
        self.texts: set[str] = set()

    def handle_starttag(self, tag, attrs):
        if tag in ('script', 'style', 'code', 'pre'):
            self.skip += 1
        for key, value in attrs:
            if key in ATTRIBUTES and value and readable(normal(value)):
                self.texts.add(normal(value))

    def handle_endtag(self, tag):
        if tag in ('script', 'style', 'code', 'pre'):
            self.skip = max(0, self.skip - 1)

    def handle_data(self, data):
        text = normal(data)
        if not self.skip and readable(text):
            self.texts.add(text)


LITERAL = re.compile(r"'((?:[^'\\\n]|\\.)*)'|\"((?:[^\"\\\n]|\\.)*)\"|`((?:[^`\\]|\\.)*)`", re.S)


def unescape(text: str) -> str:
    return re.sub(r"\\(.)", lambda m: {'n': '\n', 't': '\t'}.get(m[1], m[1]), text)


def template_key(body: str) -> str:
    """`Signed in as ${user}` -> 'Signed in as {0}' (nested braces counted)."""
    out, index, i = [], 0, 0
    while i < len(body):
        if body.startswith('${', i):
            depth, j = 1, i + 2
            while j < len(body) and depth:
                depth += {'{': 1, '}': -1}.get(body[j], 0)
                j += 1
            out.append('{%d}' % index)
            index += 1
            i = j
        else:
            out.append(body[i])
            i += 1
    return ''.join(out)


def script_strings(text: str) -> set[str]:
    found = set()
    for single, double, template in LITERAL.findall(text):
        if template:
            if '<' in template:  # markup built in a template: its parts are elsewhere
                continue
            key = normal(unescape(template_key(template)))
            # A template that is all placeholders and punctuation says nothing.
            if re.fullmatch(r'[\s{}0-9·:,.%()/-]*', key.replace('{', ' ').replace('}', ' ')):
                continue
        else:
            key = normal(unescape(single or double))
        if readable(key.replace('{0}', 'x').replace('{1}', 'x')):
            found.add(key)
    return found


def collect() -> list[str]:
    page = Page()
    page.feed((WEBUI / 'index.html').read_text())
    strings = set(page.texts)
    for name in SCRIPTS:
        strings |= script_strings((WEBUI / name).read_text())
    return sorted(strings)


def placeholders(text: str) -> list[str]:
    return sorted(re.findall(r'\{\d+\}', text))


def main(argv: list[str]) -> int:
    strings = collect()
    if '--check' not in argv:
        OUT.parent.mkdir(exist_ok=True)
        OUT.write_text(json.dumps(strings, ensure_ascii=False, indent=1) + '\n')
        print(f'{len(strings)} strings in {OUT.relative_to(ROOT)}')
        return 0
    failures = []
    if not OUT.is_file() or json.loads(OUT.read_text()) != strings:
        failures.append(f'{OUT.relative_to(ROOT)} is stale: run tools/webui-strings.py')
    for catalog in sorted((WEBUI / 'i18n').glob('*.json')):
        if catalog == OUT:
            continue
        translations = json.loads(catalog.read_text())
        missing = [s for s in strings if s not in translations]
        if missing:
            failures.append(f'{catalog.name}: {len(missing)} missing, first {missing[0]!r}')
        for key, value in translations.items():
            # A language may leave out a placeholder it has no use for (an English plural
            # "s"), never add one.
            if key in strings and not set(placeholders(value)) <= set(placeholders(key)):
                failures.append(f'{catalog.name}: placeholders differ in {key!r}')
    for failure in failures:
        print('FAIL', failure)
    print(f'{len(strings)} strings; {len(failures)} problems')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
