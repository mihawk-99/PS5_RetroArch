#!/usr/bin/env python3
"""Inspect a locally built diagnostic; never contacts the console.

PS5_MEMORY_DIAGNOSTICS=1 bash tools/verify.sh
python3 tools/check-memory-diagnostics.py > build/memory-diagnostics-inspection.json
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def inspect():
    elf = ROOT / 'build/llvm-pie.elf'
    symbols = subprocess.check_output(['llvm-nm', '-C', str(elf)], text=True)
    required = ('__wrap_malloc', '__wrap_calloc', '__wrap_realloc', '__wrap_free',
                '__wrap_posix_memalign', 'ps5::memory::init(', 'ps5::memory::failure(',
                'ps5::memory::tick(', 'memory_create(', 'memory_destroy(', 'memory_idle(',
                'ps5_memory_xmb_context', 'ps5_memory_xmb_stage', 'ps5_memory_xmb_node')
    missing = [name for name in required if name not in symbols]
    if missing:
        raise SystemExit('Diagnostic symbol missing: ' + ', '.join(missing))
    xmb_object = ROOT / 'build/ra/obj/menu_drivers_xmb.c.o'
    xmb_symbols = subprocess.check_output(['llvm-nm', str(xmb_object)], text=True)
    hooks = ('ps5_memory_xmb_context', 'ps5_memory_xmb_stage', 'ps5_memory_xmb_node')
    if any(not re.search(r'\bU ' + hook + r'$', xmb_symbols, re.M) for hook in hooks):
        raise SystemExit('Configured XMB object does not call every diagnostic hook')
    identity = re.search(r'build identity: ([a-f0-9]{64})',
                         (ROOT / 'build/title_build_identity.h').read_text()).group(1)
    hashes = {}
    for relative in ('build/llvm-pie.elf', 'build/title.map', 'dist/PPSA99169/eboot.bin'):
        hashes[relative] = hashlib.sha256((ROOT / relative).read_bytes()).hexdigest()
    archives = json.loads((ROOT / 'build/memory-diagnostic-inputs/archives.json').read_text())
    for name, expected in archives.items():
        actual = hashlib.sha256((ROOT / 'build/memory-diagnostic-inputs' / name).read_bytes()).hexdigest()
        if actual != expected:
            raise SystemExit('Driver snapshot digest mismatch: ' + name)
    return {'kind': 'host-only-diagnostic-inspection', 'passed': True,
            'build_identity': identity, 'symbols_present': list(required),
            'frontend_xmb_hook_references': list(hooks),
            'artifacts_sha256': hashes, 'driver_archives_sha256': archives,
            'console_test': 'pending; uploads authorized, launch requires owner intervention'}


if __name__ == '__main__':
    print(json.dumps(inspect(), indent=2))
