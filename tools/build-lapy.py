#!/usr/bin/env python3
"""Build the pinned external Lapy service with the console-qualified patches."""
import hashlib
import io
import os
from pathlib import Path
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
REV = '5b8397b9f2b5f12a7bc2f9c8745a00d1c2dd01ad'
LOG_SHA = '394af67d0f8b60b3335deb53396e52855ea2daa50ca914a456ea7663f48900c6'


def main():
    source = ROOT / 'build/lapy'
    source.mkdir(parents=True, exist_ok=True)
    logger = ROOT.parent / 'prospero-win-main/native/ps5log'
    if hashlib.sha256((logger / 'ps5log.h').read_bytes()).hexdigest() != LOG_SHA:
        raise RuntimeError('Lapy logger differs from the qualified input')
    archive = subprocess.check_output([
        'git', '-C', str(ROOT.parent / 'PS5-Lapy-JB-Daemon'), 'archive', REV])
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        tar.extractall(source, filter='data')
    # Carry the exact logger source alongside upstream/patch sources for releases.
    with tarfile.open(source / 'ps5log-source.tar.gz', 'w:gz') as tar:
        tar.add(logger / 'ps5log.h', arcname='ps5log/ps5log.h')
        tar.add(ROOT.parent / 'prospero-win-main/LICENSE', arcname='ps5log/LICENSE')
    patches = [ROOT / 'platform/lapy' / (name + '.patch') for name in (
        'stdout-mirror', 'donor-wait', 'donor-release', 'service-lifecycle',
        'deterministic-build', 'resident-log')]
    patches += [ROOT / 'evidence/network-elevation/daemon-sdk-and-log.patch',
                ROOT / 'evidence/installed-paths/daemon-readiness.patch']
    for patch in patches:
        subprocess.run(['patch', '--batch', '-p1', '-i', str(patch)],
                       cwd=source, check=True)
    subprocess.run(['python3', str(source / 'tools/build_owned_daemon.py'),
                    '--sdk', str(ROOT / '.deps/native/ps5-payload-sdk'),
                    '--logging-client', str(logger), '--title', 'PPSA99169',
                    '--service', '--require-client-result'], check=True,
                   env=dict(os.environ, PS5_CLANG=os.environ.get('PS5_CLANG', 'clang')))
    elf = source / 'build/owned_root_daemon-service-verified-client/lapy-root-daemon.elf'
    dynamic = subprocess.check_output(['readelf', '-dW', str(elf)], text=True)
    if 'libkernel_web.sprx' not in dynamic or 'libkernel_sys.sprx' in dynamic:
        raise RuntimeError('Lapy payload violates the loader import contract')


if __name__ == '__main__':
    main()
