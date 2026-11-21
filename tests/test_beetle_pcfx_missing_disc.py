"""Beetle PC-FX with a disc image that is not there (patches/beetle-pcfx/missing-disc.patch).

The core reads a .cue (and each track) into a MemoryStream from a FileStream. On the
console the file layer hands the core a handle for a .cue that does not exist, at
position 0 with size -1 (2026-11-20, v4.1.0-beta-4 and the build after it alike: the
crash registers held position 0 and data_buffer NULL). The MemoryStream asked realloc for
(uint64_t)-1 bytes, got NULL, and its first get_line read address 0: signal 11 where
RetroArch should have said it could not load the content.

The core's own Stream, FileStream and MemoryStream, exported at the pinned revision, are
built for the host against a file layer that answers just that, once as pinned and once
patched. Run tools/build-beetle-pcfx.sh first; no console or disc is needed.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / '.deps/beetle-pcfx-src'
PATCH = ROOT / 'patches/beetle-pcfx/missing-disc.patch'
REVISION = re.search(r'^revision=([0-9a-f]{40})', (ROOT / 'tools/build-beetle-pcfx.sh').read_text(),
                     re.M)[1]

STUB = r'''
#include <streams/file_stream.h>
/* The console's answer for a missing .cue: a handle at 0 whose size is -1. */
static char handle;
RFILE *filestream_open(const char *, unsigned, unsigned) { return (RFILE *)&handle; }
int filestream_close(RFILE *) { return 0; }
int64_t filestream_read(RFILE *, void *, int64_t) { return -1; }
int64_t filestream_write(RFILE *, const void *, int64_t) { return -1; }
int64_t filestream_seek(RFILE *, int64_t, int) { return -1; }
int64_t filestream_tell(RFILE *) { return 0; }
int64_t filestream_get_size(RFILE *) { return -1; }
'''

MAIN = r'''
#include <stdio.h>
#include "FileStream.h"
#include "MemoryStream.h"
int main(void)
{
   MemoryStream cue(new FileStream("/app0/content/pcfx/missing.cue", MODE_READ));
   std::string line;
   const int last = cue.get_line(line);
   printf("get_line %d, %zu bytes\n", last, line.size());
   return last == -1 && line.empty() ? 0 : 1;
}
'''


@unittest.skipUnless(shutil.which('clang++') and (SOURCE / '.git').exists(),
                     'clang++ and tools/build-beetle-pcfx.sh (the pinned source) are needed')
class MissingDisc(unittest.TestCase):
    def build(self, patched):
        work = Path(self.enterContext(tempfile.TemporaryDirectory(prefix='pcfx-disc-')))
        files = ['mednafen', 'libretro-common/include', 'libretro.cpp']
        archive = subprocess.run(['git', '-C', SOURCE, 'archive', REVISION, *files],
                                 check=True, capture_output=True).stdout
        subprocess.run(['tar', '-x', '-C', work], input=archive, check=True)
        if patched:
            subprocess.run(['patch', '-s', '-p1', '-d', work, '-i', PATCH], check=True)
        (work / 'stub.cpp').write_text(STUB)
        (work / 'main.cpp').write_text(MAIN)
        program = work / 'missing-disc'
        subprocess.run(['clang++', '-std=gnu++11', '-w', '-DWANT_32BPP', '-I', work / 'mednafen',
                        '-I', work / 'libretro-common/include', work / 'main.cpp', work / 'stub.cpp',
                        work / 'mednafen/Stream.cpp', work / 'mednafen/FileStream.cpp',
                        work / 'mednafen/MemoryStream.cpp', '-o', program],
                       check=True, capture_output=True)
        return subprocess.run([program], capture_output=True, text=True, timeout=30)

    def test_pinned_source_reads_address_zero(self):
        # The fault this patch is for: killed by a signal, not a failed load.
        self.assertLess(self.build(patched=False).returncode, 0)

    def test_patched_stream_is_empty(self):
        run = self.build(patched=True)
        self.assertEqual((run.returncode, run.stdout), (0, 'get_line -1, 0 bytes\n'))

    def test_patch_turns_a_failed_open_into_a_failed_load(self):
        text = PATCH.read_text()
        # The image remembers whether it opened, CDAccess_Open and CDIF_Open hand back
        # NULL for one that did not, and the core's loader fails the content on it.
        for needle in ('opened = ImageOpen(path, image_memcache);', 'if(!image->Opened())',
                       '   if(!cda)\n+      return NULL;', 'Could not open the disc image'):
            self.assertIn(needle, text)


if __name__ == '__main__':
    unittest.main()
