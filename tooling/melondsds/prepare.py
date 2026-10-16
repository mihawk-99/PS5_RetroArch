"""Extract pinned Git objects, apply port patches, and generate CMake overrides.

Only generated build/cores/melondsds sources are replaced. Cached repositories
are never checked out or cleaned; user checkouts are not inputs to this build.
"""
import json
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / 'tooling/melondsds'
WORK = ROOT / 'build/cores/melondsds'
pins = json.loads((PORT/'sources.json').read_text())
cache_root = ROOT / '.deps/melondsds-cache'
cache_root.mkdir(parents=True, exist_ok=True)
WORK.mkdir(parents=True, exist_ok=True)

def git(cache, *args, **kwargs):
    return subprocess.run(['git', '-C', str(cache), *args], check=True, **kwargs)

overrides = ['set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "" FORCE)',
             f'set(FETCHCONTENT_BASE_DIR "{WORK}/deps" CACHE PATH "" FORCE)']
for name, (url, revision) in pins.items():
    assert re.fullmatch(r'[a-z0-9-]+', name) and re.fullmatch(r'[0-9a-f]{40}', revision)
    cache = cache_root / name
    if not (cache / 'HEAD').exists():
        subprocess.run(['git', 'init', '--bare', '--quiet', str(cache)], check=True)
    present = subprocess.run(['git', '-C', str(cache), 'cat-file', '-e', revision+'^{commit}'],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if present.returncode:
        git(cache, 'fetch', '--quiet', '--depth=1', url, revision)
    got = git(cache, 'rev-parse', revision+'^{commit}', capture_output=True, text=True).stdout.strip()
    assert got == revision
    target = WORK / ('source' if name == 'melondsds' else 'deps/'+name+'-src')
    assert target.resolve().is_relative_to(WORK.resolve()) and target != WORK
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    archive = subprocess.Popen(['git', '-C', str(cache), 'archive', revision], stdout=subprocess.PIPE)
    try:
        subprocess.run(['tar', '-xf', '-', '-C', str(target)], stdin=archive.stdout, check=True)
    finally:
        archive.stdout.close()
    assert archive.wait() == 0
    if name != 'melondsds':
        # FetchContent uses upper-case names, including any hyphens.
        overrides.append(f'set(FETCHCONTENT_SOURCE_DIR_{name.upper()} "{target}" CACHE PATH "" FORCE)')
        overrides.append(f'set({name.upper().replace("-", "_")}_REPOSITORY_TAG "{revision}" CACHE STRING "" FORCE)')

for patch, target in [('native-core.patch', WORK/'source'),
                      ('touchpad-stylus.patch', WORK/'source'),
                      ('native-jit.patch', WORK/'deps/melonds-src')]:
    subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-d', str(target),
                    '-i', str(PORT/patch)], check=True)
(WORK/'dependencies.cmake').write_text('\n'.join(overrides)+'\n')
(WORK/'source-revisions.json').write_text(json.dumps(pins, indent=2)+'\n')
