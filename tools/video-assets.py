#!/usr/bin/env python3
"""Fetch pinned offline effects; stage, audit dependency closure and package safely."""
import argparse
import hashlib
import json
import re
import shutil
import tarfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TABLE = ROOT / 'tooling/video-assets/collections.json'
FIXTURES = ROOT / 'tooling/video-assets/development-fixtures.json'
TEXT_SUFFIXES = {'.cfg', '.filt', '.slangp', '.slang', '.glsl', '.h', '.inc', '.params', '.txt'}


def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def config(text):
    # RetroArch config entries are single-line; the first occurrence wins.
    result = {}
    for line in text.splitlines():
        m = re.match(r'^[ \t]*([\w]+)[ \t]*=[ \t]*(.*)', line)
        if not m:
            continue
        raw = m[2]
        # Match upstream config_file_extract_value (including unterminated quotes).
        value = raw[1:].split('"', 1)[0] if raw.startswith('"') else raw.split('#', 1)[0].split()
        if isinstance(value, list):
            value = value[0] if value else ''
        result.setdefault(m[1], value)
    return result


def references(path, text):
    # Strip source comments, retaining config #reference and preprocessor includes.
    source = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    for m in re.finditer(r'^[ \t]*#[ \t]*(?:reference|include)[ \t]+(?:"([^"]+)"|([^\s]+))', source, re.M):
        yield m[1] or m[2]
    if path.suffix in {'.slangp', '.params', '.cfg'}:
        values = config(text)
        texture_keys = set(values.get('textures', '').split(';'))
        for key, value in values.items():
            if (re.fullmatch(r'shader\d+', key) or key in texture_keys or
                    re.fullmatch(r'overlay\d+(?:_desc\d+)?_overlay', key) or
                    (path.suffix in {'.slangp', '.params'} and re.search(r'\.(?:png|jpg|jpeg|tga|bmp)$', value, re.I))):
                if value:
                    yield value


def dependency_errors(folder):
    errors = []
    base = folder.resolve()
    for path in sorted(folder.rglob('*')):
        if path.suffix not in TEXT_SUFFIXES or not path.is_file():
            continue
        text = path.read_text(errors='replace')
        for value in references(path, text):
            # RetroArch permits context-dependent optional parameter references.
            # Check that the template's bundled family exists; which members are
            # applied depends on the running core, driver and rotation.
            if re.search(r'\$[A-Z0-9_-]+\$', value):
                pattern = re.sub(r'\$[A-Z0-9_-]+\$', '*', value)
                matches = [p.resolve() for p in path.parent.glob(pattern) if p.is_file()]
                if not matches or any(not p.is_relative_to(base) for p in matches):
                    errors.append(f'{path.relative_to(folder)} -> unresolved template {value}')
                continue
            dep = (path.parent / value.replace('\\', '/')).resolve()
            if not dep.is_relative_to(base) or not dep.is_file():
                errors.append(f'{path.relative_to(folder)} -> {value}')
    return errors


def forbidden(folder):
    fixtures = json.loads(FIXTURES.read_text())
    signatures = {x['sha256'] for x in fixtures}
    # Also cover future fixtures, even if their filenames change on staging.
    fixture_root = ROOT / 'test_overlays'
    signatures.update(sha(p) for p in fixture_root.rglob('*') if p.is_file())
    errors = []
    for p in folder.rglob('*'):
        if not p.is_file():
            continue
        name = p.relative_to(folder).as_posix()
        if 'test_overlays' in name.casefold() or sha(p) in signatures:
            errors.append('development fixture: '+name)
        if p.suffix.lower() in TEXT_SUFFIXES and 'test_overlays' in p.read_text(errors='replace').casefold():
            errors.append('development reference: '+name)
    return errors


def fetch(item):
    archive = ROOT / '.deps/downloads' / (item['name']+'-'+item['revision']+'.tar.gz')
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        temporary = archive.with_suffix('.download')
        urllib.request.urlretrieve('https://codeload.github.com/'+item['repository']+'/tar.gz/'+item['revision'], temporary)
        if sha(temporary) != item['sha256']:
            temporary.unlink()
            raise ValueError('download digest mismatch: '+item['name'])
        temporary.replace(archive)
    if sha(archive) != item['sha256']:
        raise ValueError('archive digest mismatch: '+item['name'])
    parent = ROOT / '.deps/video-assets'
    tree = parent / (item['name']+'-'+item['revision'])
    # Extract each stage from the verified archive, never trust modified cache files.
    if tree.exists():
        shutil.rmtree(tree)
    with tarfile.open(archive) as stream:
        stream.extractall(parent, filter='data')
    return tree


def stage(folder):
    manifest = json.loads(TABLE.read_text())
    inventory = []
    for item in manifest['collections']:
        source = fetch(item)
        dest = folder / item['destination']
        if dest.exists():
            shutil.rmtree(dest)
        shutil.copytree(source, dest, ignore=shutil.ignore_patterns('.git*'))
        for relative, reason in manifest['exclusions'].get(item['name'], {}).items():
            (dest / relative).unlink()
        for relative, replacements in manifest.get('corrections', {}).get(item['name'], {}).items():
            path = dest / relative
            text = path.read_text()
            for old, new in replacements.items():
                if old not in text:
                    raise ValueError('stale asset correction: '+relative)
                text = text.replace(old, new)
            path.write_text(text)
        files = [p for p in dest.rglob('*') if p.is_file()]
        inventory.append({**item, 'files':len(files), 'bytes':sum(p.stat().st_size for p in files),
                          'presets':sum(p.suffix == '.slangp' for p in files),
                          'exclusions':manifest['exclusions'].get(item['name'], {}),
                          'corrections':manifest.get('corrections', {}).get(item['name'], {})})
    filters = folder / 'filters'
    if filters.exists():
        shutil.rmtree(filters)
    filters.mkdir(parents=True)
    for path in (ROOT / 'vendor/retroarch/gfx/video_filters').glob('*.filt'):
        shutil.copy2(path, filters / path.name)
    shutil.copy2(ROOT / 'vendor/retroarch/COPYING', filters / 'COPYING')
    inventory.append({'name':'RetroArch built-in CPU filters','source':'RetroArch pinned by tools/fetch-retroarch.sh',
                      'destination':'filters','implementations':27,'configurations':len(list(filters.glob('*.filt'))),
                      'license':'GPL-3.0-or-later; bundled source notices retained'})
    (folder / 'video-assets.json').write_text(json.dumps(inventory, indent=2)+'\n')
    check(folder)
    print('Video assets staged:', json.dumps(inventory, indent=2))


def check(folder):
    if not (folder / 'video-assets.json').is_file():
        raise ValueError('missing video-assets.json')
    manifest = json.loads(TABLE.read_text())
    inventory = {item['name']: item for item in json.loads((folder / 'video-assets.json').read_text())}
    for item in manifest['collections']:
        collection = folder / item['destination']
        if not collection.is_dir():
            raise ValueError('missing collection: '+item['name'])
        recorded = inventory.get(item['name'], {})
        files = [p for p in collection.rglob('*') if p.is_file()]
        if (any(recorded.get(key) != item[key] for key in ('revision', 'sha256', 'destination'))
                or recorded.get('files') != len(files)
                or recorded.get('bytes') != sum(p.stat().st_size for p in files)):
            raise ValueError('collection inventory mismatch: '+item['name'])
    if any(p.suffix.lower() in {'.so', '.dll', '.dylib'} for p in (folder / 'filters').rglob('*')):
        raise ValueError('CPU filters must use the built-in registry, not shared libraries')
    for path in ['filters/Normal2x.filt','shaders/shaders_slang/nearest.slangp',
                 'shaders/shaders_slang/bezel/Mega_Bezel/Presets/MBZ__3__STD.slangp',
                 'shaders/shaders_slang/bezel/koko-aio/koko-aio-ng.slangp']:
        if not (folder / path).is_file():
            raise ValueError('missing required asset: '+path)
    errors = forbidden(folder)
    for name in ('shaders','overlays','filters'):
        errors.extend(dependency_errors(folder / name))
    if errors:
        raise ValueError('\n'.join(errors))
    print('Video asset dependencies and development-fixture exclusion PASS')


def package(folder, output):
    check(folder)
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for p in sorted(folder.rglob('*')):
            if p.is_file():
                z.write(p, folder.name+'/'+p.relative_to(folder).as_posix())
    # Audit the actual extracted archive, independently of the staged tree.
    import tempfile
    with tempfile.TemporaryDirectory(prefix='retroarch-package-') as temp:
        with zipfile.ZipFile(output) as z:
            z.extractall(temp)
        check(Path(temp) / folder.name)
    print('Package verified:',output,sha(output))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('stage','check','package'))
    parser.add_argument('folder', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.action == 'package':
        if not args.output:
            parser.error('--output is required')
        package(args.folder,args.output)
    else:
        globals()[args.action](args.folder)


if __name__ == '__main__':
    main()
