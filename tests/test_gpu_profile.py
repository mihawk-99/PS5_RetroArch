"""Reject incomplete timing evidence and check deadline-proxy accounting."""

import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location('gpu_profile', ROOT / 'tools/analyze-gpu-profile.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def fixture():
    values = [16666667, 50000, 100000, 4500000, 20000, 3500000, 8466667, 30000]
    fields = ['interval'] + list(MODULE.PHASES)
    header = 'frame\t' + '\t'.join(name + '_ns' for name in fields)
    header += '\tpresent_interval_ns\tpresent_calls\n'
    body = ''.join('\t'.join(map(str, [i] + [value * (i + 1) for value in values]
                                + [16666667 * (i + 1), 1])) + '\n' for i in range(2))
    return '# frames=2 elapsed_ns=50000001 capacity_reached=0 api_failures=0\n' + header + body


class GpuProfile(unittest.TestCase):
    def test_partition_and_completion_cadence(self):
        result = MODULE.analyze(fixture())
        self.assertAlmostEqual(result['fps'], 40, places=5)
        self.assertAlmostEqual(result['phases']['interval']['p99_ms'], 33.333334)
        self.assertEqual(result['presentation']['completion_intervals_over_20ms'], 1)
        self.assertEqual(result['presentation']['estimated_extra_60hz_intervals'], 1)
        self.assertEqual(result['slowest_frames'][0]['frame'], 1)

    def test_rejects_truncation_failed_calls_and_bad_partition(self):
        for broken in [fixture().replace('frames=2', 'frames=3'),
                       fixture().replace('api_failures=0', 'api_failures=1'),
                       fixture().replace('capacity_reached=0', 'capacity_reached=1'),
                       fixture().replace('\t50000\t', '\t50001\t'),
                       fixture().replace('elapsed_ns=50000001', 'elapsed_ns=50000002'),
                       fixture().replace('\t1\n', '\t2\n')]:
            with self.subTest(broken=broken):
                with self.assertRaises(ValueError):
                    MODULE.analyze(broken)
