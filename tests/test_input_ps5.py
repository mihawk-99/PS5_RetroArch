"""Exercise PS5 callbacks and upstream input_state_wrap with native service mocks."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class InputPs5(unittest.TestCase):
    def test_joypad_bindings(self):
        with tempfile.TemporaryDirectory() as td:
            source = (ROOT / 'vendor/retroarch/input/input_driver.c').read_text()
            start = source.index('static int32_t input_state_wrap(')
            end = source.index('\nstatic int16_t input_joypad_axis(', start)
            parts = [source[start:end]]
            start = source.index('static int16_t input_joypad_axis(')
            end = source.index('\n/**', start)
            parts.append(source[start:end])
            start = source.index('static int16_t input_joypad_analog_axis(')
            end = source.index('\nvoid input_keyboard_line_append(', start)
            parts.append(source[start:end])
            Path(td, 'input_state_wrap.inc').write_text(
                'static input_driver_state_t input_driver_st;\n' + '\n'.join(parts))
            binary = str(Path(td) / 'input-test')
            subprocess.run(['c++', '-std=c++17', '-O1', '-g', '-fsanitize=address', '-fno-omit-frame-pointer',
                            '-pthread', '-Wl,--wrap=pthread_create', '-Wall', '-Wextra',
                            '-Ivendor/retroarch', '-Ivendor/retroarch/libretro-common/include',
                            '-I' + td, 'tests/input_ps5_test.cpp', '-o', binary],
                           cwd=ROOT, check=True)
            # LeakSanitizer cannot inspect threads inside the sandbox; retain ASan's
            # buffer checks, which cover the negotiated haptic PCM buffer size.
            env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
            subprocess.run([binary], cwd=ROOT, check=True, timeout=15, env=env)
