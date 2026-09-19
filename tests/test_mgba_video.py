"""mGBA XBGR must become libretro XRGB without changing the native buffer."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class MgbaVideo(unittest.TestCase):
    def test_native_primary_colours_to_libretro(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'colours.c'
            source.write_text('''#include <assert.h>
#include "tooling/mgba/ps5-video.h"
int main(void) {
    assert(ps5_mgba_xrgb(0x000000ff) == 0x00ff0000);
    assert(ps5_mgba_xrgb(0x0000ff00) == 0x0000ff00);
    assert(ps5_mgba_xrgb(0x00ff0000) == 0x000000ff);
    assert(ps5_mgba_xrgb(0xab123456) == 0xab563412);
    return 0;
}
''')
            binary = Path(directory) / 'colours'
            subprocess.run(['cc', '-I', str(ROOT), str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
