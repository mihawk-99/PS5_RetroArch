#!/usr/bin/env python3
"""Validate and summarize a buffered PS5 GPU profile (CPU wall time, nanoseconds)."""

import argparse
import csv
import io
import json
import math
import re
from pathlib import Path

PHASES = ('outside', 'texture', 'prepare', 'end', 'submit', 'present', 'wait')


def describe(values):
    ordered = sorted(values)
    return {
        'mean_ms': sum(ordered) / len(ordered) / 1e6,
        'p99_ms': ordered[math.ceil(len(ordered) * .99) - 1] / 1e6,
        'max_ms': ordered[-1] / 1e6,
    }


def analyze(text):
    lines = text.splitlines()
    meta = next((line for line in lines if line.startswith('# frames=')), '')
    header = next((i for i, line in enumerate(lines) if line.startswith('frame\t')), None)
    if not meta or header is None:
        raise ValueError('missing completed profile header')
    metadata = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', meta)}
    rows = list(csv.DictReader(io.StringIO('\n'.join(lines[header:])), delimiter='\t'))
    if not rows or len(rows) != metadata['frames']:
        raise ValueError('frame count differs from completed profile header')
    if metadata['capacity_reached'] or metadata['api_failures']:
        raise ValueError('capture reached capacity or recorded a Vulkan API failure')
    records = [{k: int(v) for k, v in row.items()} for row in rows]
    for i, row in enumerate(records):
        if row['frame'] != i or row['interval_ns'] <= 0:
            raise ValueError('invalid frame sequence or interval')
        if any(value < 0 for value in row.values()):
            raise ValueError('negative measurement')
        if sum(row[phase + '_ns'] for phase in PHASES) != row['interval_ns']:
            raise ValueError('phase partition does not equal the frame interval')
        if row['present_calls'] != 1 or row['present_interval_ns'] <= 0:
            raise ValueError('one successful synchronous presentation per frame is required')
    elapsed = sum(row['interval_ns'] for row in records)
    if elapsed != metadata['elapsed_ns']:
        raise ValueError('elapsed time differs from completed profile header')
    phases = {name: describe([r[name + '_ns'] for r in records])
              for name in ('interval',) + PHASES + ('present_interval',)}
    gaps = [r['present_interval_ns'] for r in records]
    return {
        'frames': len(records), 'seconds': elapsed / 1e9,
        'fps': len(records) * 1e9 / elapsed, 'phases': phases,
        'presentation': {
            'completion_intervals_over_20ms': sum(gap > 20000000 for gap in gaps),
            'completion_intervals_over_25ms': sum(gap > 25000000 for gap in gaps),
            'estimated_extra_60hz_intervals': sum(max(0, int(gap * 60 / 1e9 + .5) - 1)
                                                for gap in gaps),
            'limitation': 'CPU times at successful synchronous vkQueuePresentKHR return; '
                          'not hardware vblank counters or scanout timestamps. '
                          'Extra 60 Hz intervals are rounded estimates, not exact missed-vblank counts.',
        },
        'slowest_frames': sorted(records, key=lambda row: row['interval_ns'], reverse=True)[:10],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    args = parser.parse_args()
    try:
        result = analyze(args.capture.read_text())
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.exit(1, f'GPU profile invalid: {error}\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
