"""Bundle the pinned source's licence files and embedded dependency notices."""
from pathlib import Path
import re
import sys

source, output = map(Path, sys.argv[1:])
parts = ['SwanStation and bundled dependencies\n'
         'PS5 portability changes adapted from AngelXex / darkxex, SwanStationPS5 1.3.4.\n'
         'https://github.com/darkxex/SwanStationPS5/tree/1.3.4\n'
         'Port adaptations: GPL-3.0-or-later.\n']
files = ['LICENSE', 'dep/libchdr/LICENSE.txt', 'dep/libchdr/deps/lzma-25.01/LICENSE',
         'dep/xxhash/LICENSE', 'dep/xbyak/COPYRIGHT', 'dep/openbios/LICENSE.txt']
for name in files:
    parts.append('\n=== ' + name + ' ===\n' + (source/name).read_text())
# Some bundled single-file libraries keep their complete licence in a comment.
# Retain those blocks verbatim, including alternative public-domain licences.
seen = set()
for folder in ['dep/stb', 'dep/glad', 'dep/vulkan-loader', 'dep/libretro-common',
               'dep/libchdr']:
    for path in sorted((source/folder).rglob('*')):
        if path.suffix not in ('.c', '.cpp', '.h', '.hpp'):
            continue
        text = path.read_text(errors='replace')
        for match in re.finditer(r'/\*.*?\*/', text, re.S):
            block = match[0]
            if not any(phrase in block.lower() for phrase in
                       ['permission is hereby granted', 'redistribution and use',
                        'this software is available under 2 licenses']):
                continue
            if block in seen:
                continue
            seen.add(block)
            parts.append('\n=== ' + str(path.relative_to(source)) + ' ===\n' + block + '\n')
output.write_text('\n'.join(parts))
