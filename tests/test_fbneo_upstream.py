"""Exercise pinned upstream colours and compare the native decoder's bounds escape."""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
REV = '6bb3167a044e19e7106a5110d5531aa9c6afa96f'
SHA = '7d4cacbd55e74c5cd973fbe63d1c75b8dbe5c29b61593d8185be60483d29c892'


class FBNeoUpstream(unittest.TestCase):
    def test_pixel_functions_and_decoder_bounds(self):
        archive = ROOT / '.deps/downloads' / f'fbneo-{REV}.tar.gz'
        if not archive.exists():
            self.skipTest('pinned FBNeo archive unavailable; run make fbneo first')
        self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(), SHA)
        with tempfile.TemporaryDirectory() as td, tarfile.open(archive) as tar:
            work = Path(td)
            def source(name):
                return tar.extractfile(f'FBNeo-{REV}/{name}').read().decode()
            video = source('src/burner/libretro/libretro.cpp')
            video_path = work / 'src/burner/libretro/libretro.cpp'
            video_path.parent.mkdir(parents=True)
            video_path.write_text(video)
            subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(
                ROOT / 'patches/fbneo/native-video-and-metadata.patch')], cwd=work,
                check=True, stdout=subprocess.DEVNULL)
            metadata = re.search(r'void retro_get_system_info\([^}]+\}',
                                 video_path.read_text()).group()
            (work / 'libretro.h').write_text(source('src/burner/libretro/libretro-common/include/libretro.h'))
            (work / 'metadata.cpp').write_text(
                '#include <cstdio>\n#include <cstdlib>\n#include <cstring>\n#include <cassert>\n'
                '#include "libretro.h"\n#define APP_TITLE "FinalBurn Neo"\n'
                '#define INCLUDE_CHD_SUPPORT\n#define GIT_VERSION "GIT6bb3167"\n'
                'const unsigned nBurnVer = 0x010003;\n' + metadata + r'''
int main() {
    retro_system_info info = {};
    for (unsigned i = 0; i < 10000; ++i) {
        retro_get_system_info(&info);
        assert(!strcmp(info.library_name, "FinalBurn Neo"));
        assert(strstr(info.library_version, "GIT6bb3167"));
        assert(info.need_fullpath && info.block_extract);
        assert(!strcmp(info.valid_extensions, "zip|7z|cue|ccd|chd"));
    }
}
''')
            subprocess.run(['c++', '-O1', '-fsanitize=address,undefined',
                            str(work / 'metadata.cpp'), '-o', str(work / 'metadata')], check=True)
            subprocess.run([str(work / 'metadata')], check=True,
                           env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0',
                                'UBSAN_OPTIONS': 'halt_on_error=1'})
            functions = '\n'.join(re.search(
                r'static UINT32 __cdecl ' + name + r'\([^}]+\}', video).group()
                for name in ['HighCol16', 'HighCol32'])
            (work / 'video.cpp').write_text(
                '#include <cassert>\n#include <cstdint>\n#include <cstring>\nusing UINT32 = uint32_t;\nusing INT32 = int32_t;\n#define __cdecl\n' + functions + '\n'
                f'#include "{ROOT}/tooling/fbneo/ps5-video.h"\n' + r'''
int main() {
    for (unsigned r = 0; r < 256; ++r)
        for (unsigned g = 0; g < 256; ++g)
            for (unsigned b = 0; b < 256; ++b) {
                uint32_t native = HighCol32(r, g, b, 0);
                /* The upload copies these bytes into a B8G8R8A8 image, so
                   libretro's little-endian XRGB8888 word is blue, green, red. */
                uint8_t bytes[4];
                memcpy(bytes, &native, 4);
                assert(bytes[0] == b && bytes[1] == g && bytes[2] == r && bytes[3] == 0);
                uint16_t packed = HighCol16(r, g, b, 0);
                assert(packed == ((r >> 3) << 11 | (g >> 2) << 5 | b >> 3));
            }
}
''')
            subprocess.run(['c++', '-O2', str(work / 'video.cpp'), '-o', str(work / 'video')], check=True)
            subprocess.run([str(work / 'video')], check=True)
            rel = Path('src/burn/snd')
            (work / rel).mkdir(parents=True)
            for filename in ['mpeg_audio.cpp', 'mpeg_audio.h']:
                (work / rel / filename).write_text(source(str(rel / filename)))
            # Only platform-independent declarations are stubbed. Decoder and tables
            # are the actual pinned source, before and after applying the port patch.
            stub = '#include <cstdint>\n#include <cstring>\n#include <cstdlib>\n#include <cassert>\nusing UINT8 = uint8_t;\n#define SCAN_VAR(x) ((void)0)\n'
            (work / rel / 'burnint.h').write_text(stub)
            original = work / 'original'
            original.mkdir()
            for filename in ['mpeg_audio.cpp', 'mpeg_audio.h', 'burnint.h']:
                (original / filename).write_bytes((work / rel / filename).read_bytes())
            subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(
                ROOT / 'patches/fbneo/native-audio-bounds.patch')], cwd=work, check=True,
                stdout=subprocess.DEVNULL)
            (work / 'audio.cpp').write_text(stub +
                '#define mpeg_audio Original\n#include "original/mpeg_audio.h"\n#undef mpeg_audio\n#undef FBNEO_SOUND_MPEG_AUDIO_H\n'
                '#include "src/burn/snd/mpeg_audio.h"\n#include <string>\n' + r'''
int main() {
    // Valid mono MPEG layer 2 header; zero allocation bands decode to silence.
    for (unsigned mode = 0; mode < 2; ++mode) {
    const std::string bits = mode == 0
        ? "111111111111" "110" "1" "1000" "00" "0" "0" "11" "00" "0000"
        : "111111111111" "010" "0" "0001" "00" "00" "11" "00" "000" "0";
    uint8_t data[512] = {};
    for (unsigned i = 0; i < bits.size(); ++i)
        if (bits[i] == '1') data[i / 8] |= 1 << (7 - i % 8);
    unsigned passed = 0, failed = 0;
    for (int limit = 0; limit <= 4096; ++limit) {
        Original reference(data, mode ? Original::AMM : Original::L2, false, 0);
        mpeg_audio native(data, mode ? mpeg_audio::AMM : mpeg_audio::L2, false, 0);
        short a[2304], b[2304];
        memset(a, 0x5a, sizeof(a)); memset(b, 0x5a, sizeof(b));
        int ap = 0, bp = 0, an = -1, bn = -1, ar = -1, br = -1, ac = -1, bc = -1;
        bool ok = reference.decode_buffer(ap, limit, a, an, ar, ac);
        assert(native.decode_buffer(bp, limit, b, bn, br, bc) == ok);
        assert(ap == bp && an == bn && ar == br && ac == bc);
        assert(memcmp(a, b, sizeof(a)) == 0);
        if (ok) { ++passed; assert(an == (mode ? 96 : 1152) && ar == (mode ? 22050 : 44100) && ac == 1); }
        else ++failed;
    }
    assert(passed > 0 && failed > 32);
    }
}
''')
            flags = ['c++', '-std=c++11', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            subprocess.run(flags + ['-Dmpeg_audio=Original', '-c', str(original / 'mpeg_audio.cpp'),
                                    '-o', str(work / 'original.o')], check=True)
            subprocess.run(flags + ['-fno-exceptions', '-c', str(work / rel / 'mpeg_audio.cpp'),
                                    '-o', str(work / 'native.o')], check=True)
            subprocess.run(flags + [str(work / 'audio.cpp'), str(work / 'original.o'),
                                    str(work / 'native.o'), '-o', str(work / 'audio')], check=True)
            # LeakSanitizer cannot attach inside the sandbox; bounds/UB checks stay on.
            subprocess.run([str(work / 'audio')], check=True,
                           env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0',
                                'UBSAN_OPTIONS': 'halt_on_error=1'})

    def test_catalogue_bulk_storage_failure_and_reload(self):
        archive = ROOT / '.deps/downloads' / f'fbneo-{REV}.tar.gz'
        if not archive.exists():
            self.skipTest('pinned FBNeo archive unavailable; run make fbneo first')
        self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(), SHA)
        with tempfile.TemporaryDirectory() as td, tarfile.open(archive) as tar:
            work = Path(td)
            for rel in ['src/burn/burn.cpp', 'src/burner/libretro/libretro.cpp']:
                path = work / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(tar.extractfile(f'FBNeo-{REV}/{rel}').read())
            for patch in ['native-video-and-metadata.patch', 'native-catalogue-storage.patch']:
                subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i',
                                str(ROOT / 'patches/fbneo' / patch)], cwd=work,
                               check=True, stdout=subprocess.DEVNULL)
            source = (work / 'src/burn/burn.cpp').read_text()
            catalogue = source[source.index('// PS5 catalogue storage:'):
                               source.index('extern "C" INT32 BurnLibInit()')]
            (work / 'catalogue.cpp').write_text(r'''
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#define MAX_PATH 260
using UINT32 = uint32_t;
using INT32 = int32_t;
struct Driver { char *szShortName, *szFullNameA; wchar_t *szFullNameW; };
static Driver drivers[30000];
static Driver *pDriver[30000];
static UINT32 nBurnDrvCount = 30000;
static char **pszShortName, **pszFullNameA;
static wchar_t **pszFullNameW;
static void *allocated[2];
static size_t sizes[2], small_bytes;
static unsigned calls, fail_call;
void *limited_calloc(size_t n, size_t size) {
    ++calls;
    if (calls == fail_call) return nullptr;
    const size_t bytes = n * size;
    // Model the existing wrapper: >=1 MiB bypasses the small native heap.
    if (bytes < 1024 * 1024 && small_bytes + bytes > 1024 * 1024) return nullptr;
    void *p = calloc(n, size); assert(p);
    for (unsigned i = 0; i < 2; ++i) if (!allocated[i]) {
        allocated[i] = p; sizes[i] = bytes;
        if (bytes < 1024 * 1024) small_bytes += bytes;
        return p;
    }
    abort();
}
void checked_free(void *p) {
    if (!p) return;
    for (unsigned i = 0; i < 2; ++i) if (allocated[i] == p) {
        if (sizes[i] < 1024 * 1024) small_bytes -= sizes[i];
        allocated[i] = nullptr; free(p); return;
    }
    abort();
}
#define calloc limited_calloc
#define free checked_free
''' + catalogue + r'''
#undef calloc
#undef free
int main() {
    char short_name[] = "short", full_name[] = "Full title";
    wchar_t wide_name[] = L"Wide title";
    for (unsigned i = 0; i < 30000; ++i) {
        drivers[i] = {short_name, full_name, wide_name}; pDriver[i] = &drivers[i];
    }
    for (unsigned fail = 0; fail <= 2; ++fail) {
        for (unsigned repeat = 0; repeat < 3; ++repeat) {
            calls = 0; fail_call = fail;
            assert(BurnGameListInit() == (fail ? 1 : 0));
            for (unsigned i = 0; i < 30000; ++i) {
                assert(!strcmp(drivers[i].szShortName, short_name));
                assert(!strcmp(drivers[i].szFullNameA, full_name));
                if (fail) assert(drivers[i].szShortName == short_name);
                else { assert(drivers[i].szShortName != short_name); pszShortName[i][0] = 'X'; }
            }
            assert(calls == 2);
            BurnGameListExit(); BurnGameListExit();
            assert(!allocated[0] && !allocated[1] && small_bytes == 0);
            for (unsigned i = 0; i < 30000; ++i)
                assert(drivers[i].szShortName == short_name && drivers[i].szFullNameA == full_name
                       && drivers[i].szFullNameW == wide_name);
        }
    }
    // Reject too-long catalogue fields without allocation or mutation.
    char oversized[101]; memset(oversized, 'x', 100); oversized[100] = 0;
    drivers[123].szShortName = oversized; calls = 0;
    assert(BurnGameListInit() == 1 && calls == 0);
    BurnGameListExit();
    drivers[123].szShortName = short_name;
    nBurnDrvCount = 0;
    assert(BurnGameListInit() == 0 && calls == 0);
    BurnGameListExit();
}
''')
            for unicode in [False, True]:
                subprocess.run(['c++', '-std=c++11', '-O1', '-g', '-fsanitize=address,undefined']
                               + (['-D_UNICODE'] if unicode else [])
                               + [str(work / 'catalogue.cpp'), '-o', str(work / 'catalogue')], check=True)
                subprocess.run([str(work / 'catalogue')], check=True,
                               env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0',
                                    'UBSAN_OPTIONS': 'halt_on_error=1'})
