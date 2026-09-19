#!/usr/bin/env python3
"""Verify FTP writes in RetroArch's managed folders using disposable probe files.

Run after the title has applied its directory permissions. Reads console settings
from the ignored .env; never changes existing files or creates missing directories.
The machine-readable result stays under ignored klog/ for evidence distillation.
"""
import importlib.util
import io
import json
from pathlib import Path
import sys
import uuid

from ps5_ftp import connect, remove_if_present

ROOT = Path(__file__).resolve().parent.parent
DIRECTORIES = ('config', 'cores', 'content', 'system', 'savefiles', 'savestates', 'playlists')


def main():
    spec = importlib.util.spec_from_file_location('deploy', ROOT / 'tools/deploy-title.py')
    deploy = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(deploy)
    title = json.loads((ROOT / 'sce_sys/param.json').read_text())['titleId']
    base = f'/data/homebrew/{title}'
    token = uuid.uuid4().hex
    payload = b'PS5 RetroArch FTP directory permission test\n'
    report = {'command': 'python3 tools/check-ftp-write.py', 'directories': []}
    with connect(**deploy.load_settings()) as ftp:
        for name in DIRECTORIES:
            path = f'{base}/{name}/.retroarch-permission-test-{token}.tmp'
            result = {'directory': name, 'upload': False, 'readback': False, 'cleanup': False}
            try:
                ftp.storbinary(f'STOR {path}', io.BytesIO(payload))
                result['upload'] = True
                received = io.BytesIO()
                ftp.retrbinary(f'RETR {path}', received.write)
                result['readback'] = received.getvalue() == payload
            except Exception as error:
                result['error'] = f'{type(error).__name__}: {error}'
            finally:
                try:
                    remove_if_present(ftp, path)
                    result['cleanup'] = True
                except Exception as error:
                    result['cleanup_error'] = f'{type(error).__name__}: {error}'
            report['directories'].append(result)
        ftp.cwd(base)
        listing = []
        ftp.retrlines('LIST', listing.append)
        modes = {}
        for line in listing:
            fields = line.split(maxsplit=8)
            if len(fields) == 9:
                modes[fields[8].rstrip('/')] = fields[0]
        for result in report['directories']:
            result['mode'] = modes.get(result['directory'])
            result['passed'] = (result['upload'] and result['readback'] and result['cleanup']
                                and result['mode'] == 'drwxrwxrwx')
            print(f"{result['directory']}: mode={result['mode']} "
                  f"upload={result['upload']} readback={result['readback']} "
                  f"cleanup={result['cleanup']} {'PASS' if result['passed'] else 'FAIL'}")
    report['passed'] = all(row['passed'] for row in report['directories'])
    directory = ROOT / 'klog'
    directory.mkdir(exist_ok=True)
    target = directory / f'ftp-write-{token}.json'
    target.write_text(json.dumps(report, indent=2) + '\n')
    print(f'report: {target.relative_to(ROOT)}')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
