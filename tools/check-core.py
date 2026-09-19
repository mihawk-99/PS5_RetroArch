#!/usr/bin/env python3
"""Check a PS5 libretro shared ELF without confusing it with a host library."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

REQUIRED = {
    'retro_api_version', 'retro_init', 'retro_deinit', 'retro_run',
    'retro_get_system_info', 'retro_get_system_av_info', 'retro_load_game',
    'retro_load_game_special', 'retro_unload_game', 'retro_reset',
    'retro_set_environment', 'retro_set_video_refresh', 'retro_set_audio_sample',
    'retro_set_audio_sample_batch', 'retro_set_input_poll', 'retro_set_input_state',
    'retro_set_controller_port_device', 'retro_serialize_size', 'retro_serialize',
    'retro_unserialize', 'retro_get_region', 'retro_get_memory_data',
    'retro_get_memory_size', 'retro_cheat_reset', 'retro_cheat_set',
}
ALLOWED_IMPORTS = {'libkernel_web.sprx', 'libSceLibcInternal.sprx',
                   'libScePosixForWebKit.sprx'}


def inspect(path):
    data = Path(path).read_bytes()
    if len(data) < 64 or data[:8] != b'\x7fELF\x02\x01\x01\x09':
        raise ValueError('expected ELF64 little-endian FreeBSD/PS5 ABI, not a host library')
    kind, machine = struct.unpack_from('<HH', data, 16)
    if kind != 3 or machine != 62:
        raise ValueError('expected x86-64 ET_DYN shared object')
    dynamic = subprocess.check_output(['readelf', '-dW', str(path)], text=True)
    needed = re.findall(r'\(NEEDED\).*\[([^\]]+)\]', dynamic)
    if 'libkernel_web.sprx' not in needed or set(needed) - ALLOWED_IMPORTS:
        raise ValueError(f'unexpected PS5 core imports: {needed}')
    symbols = subprocess.check_output(['readelf', '--dyn-syms', '-W', str(path)], text=True)
    exported, undefined = set(), set()
    for line in symbols.splitlines():
        fields = line.split()
        if len(fields) < 8 or not fields[0].endswith(':'):
            continue
        if fields[6] == 'UND':
            undefined.add(fields[7])
        elif fields[3:6] == ['FUNC', 'GLOBAL', 'DEFAULT']:
            exported.add(fields[7])
    missing = REQUIRED - exported
    if missing:
        raise ValueError(f'missing dynamic libretro exports: {sorted(missing)}')
    phoff = struct.unpack_from('<Q', data, 32)[0]
    phsize, phcount = struct.unpack_from('<HH', data, 54)
    if phsize != 56 or phoff + phsize * phcount > len(data):
        raise ValueError('invalid program header table')
    loads = []
    for i in range(phcount):
        ptype, flags, offset, address, _, filesz, memsz, align = struct.unpack_from(
            '<IIQQQQQQ', data, phoff + i * phsize)
        if ptype != 1:
            continue
        if (align < 0x4000 or flags & 3 == 3 or offset % 0x4000 != address % 0x4000
                or filesz > memsz or offset + filesz > len(data)):
            raise ValueError('invalid PS5 16 KiB load segment')
        loads.append({'flags': flags, 'alignment': align})
    if not loads:
        raise ValueError('no loadable segments')
    return {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data),
            'elf': 'ELF64 x86-64 ET_DYN FreeBSD/PS5', 'needed': needed,
            'libretro_exports': sorted(REQUIRED), 'undefined_symbols': sorted(undefined),
            'load_segments': loads, 'abi_passed': True,
            'console_loading_verified': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('core', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    try:
        report = inspect(args.core)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'core ABI check failed: {error}\n')
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(f'core ABI PASS: {args.core.name}; {len(REQUIRED)} exports; '
          f'kernel_web; sha256={report["sha256"]}')


if __name__ == '__main__':
    main()
