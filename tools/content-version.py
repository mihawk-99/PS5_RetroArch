#!/usr/bin/env python3
"""The title's contentVersion for a release tag (issue #77).

Stores (ProsperoStore, through the ps5-homebrew-catalog) read contentVersion from
sce_sys/param.json at the release tag and offer an update only when theirs is higher
than the installed one, so every release commits its own: vMAJOR.MINOR.PATCH, with any
suffix, is MM.mmm.ppp, and it must be higher than every earlier tag's. Up to
v4.2.0-beta-4 every release shipped 01.000.000.

  python3 tools/content-version.py expected <tag>   the value to commit for <tag>
  python3 tools/content-version.py check <tag>      refuse a release build without it
                                                    (tools/build-title.sh runs this)
"""
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PARAM = 'sce_sys/param.json'
FORMAT = re.compile(r'\d{2}\.\d{3}\.\d{3}')


def expected(tag):
    m = re.match(r'v?(\d+)\.(\d+)\.(\d+)', tag)
    if not m or int(m[1]) > 99 or int(m[2]) > 999 or int(m[3]) > 999:
        raise ValueError(f'{tag}: not vMAJOR.MINOR.PATCH within NN.NNN.NNN')
    return f'{int(m[1]):02}.{int(m[2]):03}.{int(m[3]):03}'


def key(version):
    return tuple(int(part) for part in version.split('.'))


def tagged_versions(exclude):
    """contentVersion as committed at every v* tag but `exclude`."""
    tags = subprocess.run(['git', '-C', ROOT, 'tag', '--list', 'v*'], capture_output=True,
                          text=True, check=True).stdout.split()
    versions = {}
    for tag in tags:
        if tag == exclude:
            continue
        shown = subprocess.run(['git', '-C', ROOT, 'show', f'{tag}:{PARAM}'], capture_output=True,
                               text=True)
        if shown.returncode == 0:
            versions[tag] = json.loads(shown.stdout).get('contentVersion', '')
    return versions


def check(tag, current, earlier):
    """What stops `tag` shipping with contentVersion `current`, given the earlier tags'."""
    want = expected(tag)
    problems = []
    if current != want:
        problems.append(f'{PARAM} contentVersion is {current}; release {tag} ships {want}: '
                        'set it and commit')
    reused = sorted(f'{t} ({v})' for t, v in earlier.items() if FORMAT.fullmatch(v) and key(v) >= key(want))
    if reused:
        problems.append(f'{want} is not higher than {", ".join(reused)}: a store offers only a higher one')
    return problems


def main(argv):
    if len(argv) != 3 or argv[1] not in ('expected', 'check'):
        raise SystemExit(__doc__)
    tag = argv[2]
    if argv[1] == 'expected':
        print(expected(tag))
        return 0
    current = json.loads((ROOT / PARAM).read_text()).get('contentVersion', '')
    problems = check(tag, current, tagged_versions(tag))
    for problem in problems:
        print(f'error: {problem}', file=sys.stderr)
    if not problems:
        print(f'==> [title] contentVersion {current} for {tag}')
    return 2 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
