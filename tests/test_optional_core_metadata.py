"""The metadata generator must import with the optional core both off and on."""
import importlib.util
import os
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent


def catalogs(environment):
    with patch.dict(os.environ, environment, clear=True):
        spec = importlib.util.spec_from_file_location(
            'optional_metadata', ROOT / 'tools/generate-core-metadata.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module.CORES


class OptionalCoreMetadata(unittest.TestCase):
    def test_default_catalog_excludes_swanstation(self):
        self.assertNotIn('swanstation', [core[0] for core in catalogs({})])

    def test_enabled_catalog_registers_swanstation_once(self):
        cores = catalogs({'PS5_WITH_SWANSTATION': '1'})
        enabled = [core for core in cores if core[0] == 'swanstation']
        self.assertEqual(len(enabled), 1)
        self.assertEqual(enabled[0][1], 'SwanStation')
        self.assertEqual(enabled[0][2],
                         'build/cores/swanstation/source/src/libretro/libretro_core_options.h')
