"""Prepare verified, pinned upstream source in this core's generated build directory."""
import hashlib
import json
import shutil
import subprocess
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / 'tooling/swanstation'
WORK = ROOT / 'build/cores/swanstation'
pins = json.loads((PORT / 'sources.json').read_text())
cache = ROOT / '.deps/downloads'
cache.mkdir(parents=True, exist_ok=True)


def fetch(url, target, digest):
    if not target.exists():
        pending = target.with_suffix(target.suffix + '.download')
        urllib.request.urlretrieve(url, pending)
        if hashlib.sha256(pending.read_bytes()).hexdigest() != digest:
            raise ValueError('download digest mismatch: ' + str(target))
        pending.replace(target)
    if hashlib.sha256(target.read_bytes()).hexdigest() != digest:
        raise ValueError('cached input digest mismatch: ' + str(target))


archive = cache / ('swanstation-' + pins['revision'] + '.zip')
fetch(pins['url'], archive, pins['sha256'])
info = cache / 'swanstation_libretro.info'
fetch('https://raw.githubusercontent.com/libretro/libretro-core-info/' +
      pins['info_revision'] + '/swanstation_libretro.info', info, pins['info_sha256'])
WORK.mkdir(parents=True, exist_ok=True)
source = WORK / 'source'
if source.exists():
    assert source.resolve().is_relative_to(WORK.resolve()) and source != WORK
    shutil.rmtree(source)
source.mkdir()
with zipfile.ZipFile(archive) as package:
    prefix = 'swanstation-' + pins['revision'] + '/'
    for item in package.infolist():
        assert item.filename.startswith(prefix)
        relative = Path(item.filename[len(prefix):])
        assert not relative.is_absolute() and '..' not in relative.parts
        if not relative.parts:
            continue
        target = source / relative
        if item.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(package.read(item))
subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-d', str(source),
                '-i', str(PORT / 'native.patch')], check=True)
(WORK / 'source-revisions.json').write_text(json.dumps(pins, indent=2) + '\n')
