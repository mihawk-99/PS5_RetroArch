"""Native path/catalog and game-handoff contracts; no PS5 interaction."""
from pathlib import Path
import os
import shlex
import struct
import zlib
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PORT = ROOT
PUGIXML = ROOT / 'build/pugixml-ps5/src/src'


def system_path_pipeline():
    """Compile the actual startup normalization, not a duplicate of its logic."""
    source = PORT / 'build/es-de/src'
    filesystem = (source / 'es-core/src/utils/FileSystemUtil.cpp').read_text()
    strings = (source / 'es-core/src/utils/StringUtil.cpp').read_text()
    systems = (source / 'es-app/src/SystemData.cpp').read_text()

    def function(text, signature):
        start = text.index('        ' + signature)
        end = text.index('\n        }', start) + len('\n        }')
        return text[start:end]

    start = systems.index('            path = Utils::String::replace(path, "%ROMPATH%", rompath);')
    end = systems.index('            // Check that the ROM directory', start)
    return r'''
#include "SmbCatalog.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#define LOG(level) std::cerr
namespace Utils {
namespace String {
''' + function(strings, 'std::string replace(') + r'''
}
namespace FileSystem {
std::string getHomePath() { return "/test/home"; }
std::string getSystemHomeDirectory() { return "/test/home"; }
std::string expandHomePath(const std::string&, bool = false);
''' + '\n'.join(function(filesystem, signature) for signature in (
        'std::string getGenericPath(', 'bool exists(', 'bool isDirectory(',
        'std::string getParent(', 'std::string expandHomePath(',
        'std::string resolveRelativePath(')) + r'''
}}
std::string systemPath(std::string path, const std::string& rompath) {
''' + systems[start:end] + r'''
return path;
}
'''


class NativeSmb(unittest.TestCase):
    def test_nonblocking_cache(self):
        with tempfile.TemporaryDirectory() as td:
            exe = str(Path(td)/'cache')
            subprocess.run(['c++','-std=c++17','-pthread','-g','-fsanitize=address,undefined',
                            '-fno-pie','-no-pie','-I'+str(PORT/'frontends/es-de/ps5/include'),
                            str(PORT/'tests/esde_smb_prefetch_test.cpp'),'-o',exe],check=True)
            subprocess.run([exe],check=True,timeout=15)

    def test_frontend_without_optional_smb_transport(self):
        with tempfile.TemporaryDirectory() as td:
            source = Path(td)/'check.c'
            source.write_text('''#define PS5_GAME_SMB_TEST 1
#include "ps5_game.c"
#include <assert.h>
int sceSystemServiceLoadExec(const char* path,const char* const* argv) { (void)path; (void)argv; return -1; }
int main(void) { struct ps5_game game={0};
strcpy(game.content,"smb://host/share/demo.zip");
assert(ps5_game_check(&game)!=0); return 0; }''')
            exe = str(Path(td)/'check')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(PORT/'src'),
                            str(source),str(PORT/'src/ps5_library.c'),'-o',exe],check=True)
            subprocess.run([exe],check=True,timeout=5)

    def test_decoder_and_frame_budgets(self):
        from test_esde_freeimage import SMALL_WEBP, STB
        if not (STB/'stb_image.h').exists(): self.skipTest('Run tools/build-esde.sh first')
        flags = os.environ.get('ESDE_HOST_WEBP_FLAGS')
        if flags is None:
            result = subprocess.run(['pkg-config','--cflags','--libs','libwebp'],capture_output=True,text=True)
            if result.returncode: self.skipTest('libwebp development files required')
            flags = result.stdout
        def png(width,height,rows):
            def chunk(kind,data): return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
            return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',width,height,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b'')
        with tempfile.TemporaryDirectory() as td:
            td=Path(td)
            (td/'small.png').write_bytes(png(2,2,bytes([0,255,0,0,255,0,255,0,128,0,0,0,255,255,255,255,255,0])))
            (td/'large.png').write_bytes(png(4096,8,(b'\0'+bytes([255,0,0,255])*4096)*8))
            (td/'small.webp').write_bytes(SMALL_WEBP)
            exe=str(td/'decode')
            subprocess.run(['c++','-std=c++17','-pthread','-O1','-g','-fsanitize=address,undefined',
                            '-fno-pie','-no-pie','-I'+str(PORT/'frontends/es-de/ps5/include'),'-isystem',str(STB),
                            str(PORT/'tests/esde_smb_decode_test.cpp'),
                            str(PORT/'frontends/es-de/ps5/smb_images.cpp'),
                            str(PORT/'frontends/es-de/ps5/freeimage_stb.cpp'),'-o',exe,*shlex.split(flags)],check=True)
            subprocess.run([exe,str(td/'small.png'),str(td/'large.png'),str(td/'small.webp')],check=True,timeout=20)

    def test_upstream_grid_window(self):
        grid=(PORT/'build/es-de/src/es-core/src/components/primary/GridComponent.h').read_text()
        start=grid.index('        const int rows',grid.index('template <typename T> void GridComponent<T>::update'))
        end=grid.index('        SmbImages::window(paths, visible);',start)
        code = '''#include "SmbImages.h"
#include <cassert>
#include <cmath>
#include <set>
struct Result { std::vector<int> indices; size_t visible; };
Result window(int mCursor,int mColumns,float mVisibleRows,float mScrollPos,int count) {
 struct Entry { struct { std::string imagePath; } data; };
 std::vector<Entry> mEntries(count);
 for(int i=0;i<count;++i) mEntries[i].data.imagePath="smb://host/share/"+std::to_string(i);
 auto size=[&] { return count; };
''' + grid[start:end] + '''
 return {indices,visible};
}
int main() {
 for (const auto c : {std::array<int,6>{0,0,70,21,35,0}, {28,2,70,21,42,7},
                     {69,7,70,21,28,42}, {1,0,2,2,2,0}, {22,0,23,22,23,0}}) {
  auto r=window(c[0],7,3,c[1],c[2]);
  assert(r.indices.front()==c[0] && r.visible==size_t(c[3]) && r.indices.size()==size_t(c[4]));
  std::set<int> unique(r.indices.begin(),r.indices.end()); assert(unique.size()==r.indices.size());
  for(int i:r.indices) assert(i>=c[5] && i<c[2]);
 }
}
'''
        with tempfile.TemporaryDirectory() as td:
            source=Path(td)/'grid.cpp';source.write_text('#include <array>\n'+code)
            exe=str(Path(td)/'grid')
            subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(PORT/'frontends/es-de/ps5/include'),str(source),'-o',exe],check=True)
            subprocess.run([exe],check=True,timeout=5)

    def test_system_startup_preserves_smb_scheme(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            catalog = folder / 'catalog.xml'
            catalog.write_text('''<smbCatalog version="1" root="smb://host/share">
<file path="smb://host/share/pcengine/Game #1 (JP) 100%.zip" size="100"/>
<file path="smb://host/share/pcenginecd/Disc.chd" size="200"/>
</smbCatalog>''')
            source = folder / 'startup.cpp'
            source.write_text(system_path_pipeline() + r'''
int main() {
 using namespace Utils::FileSystem;
 for (const std::string name : {"pcengine", "pcenginecd"}) {
  const auto root = "smb://host/share/" + name;
  assert(systemPath(root, "") == root);
  assert(systemPath("%ROMPATH%/" + name, "smb://host/share/") == root);
  assert(exists(systemPath(root, "")) && isDirectory(systemPath(root, "")));
 }
 const auto game = resolveRelativePath("./Game #1 (JP) 100%.zip",
                                      systemPath("smb://host/share/pcengine", ""), false);
 assert(game == "smb://host/share/pcengine/Game #1 (JP) 100%.zip" && exists(game));
 assert(!exists(systemPath("smb://host/share/missing", "")));
 assert(!exists(systemPath("smb://other/share/pcengine", "")));
 assert(!exists(systemPath("smb://host/share/../escape", "")));
 assert(systemPath("/app0//content/pcengine", "") == "/app0/content/pcengine");
 assert(systemPath("~/ROMs/pcengine", "") == "/test/home/ROMs/pcengine");
}
''')
            exe = folder / 'startup'
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-D__PROSPERO__', f'-DPS5_SMB_CATALOG="{catalog}"',
                            '-I'+str(PORT / 'frontends/es-de/ps5/include'), '-I'+str(PUGIXML),
                            str(source), str(PUGIXML/'pugixml.cpp'), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_catalog_exact_paths_and_namespace_boundaries(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            catalog = folder / 'catalog.xml'
            catalog.write_text('''<smbCatalog version="1" root="smb://host/share">
<file path="smb://host/share/pcengine/Game #1 (JP) 100%.zip" size="100"/>
<file path="smb://host/share/pcengine/images/Game.png" size="20"/>
<file path="smb://other/share/escape" size="3"/>
<media game="smb://host/share/pcengine/Game #1 (JP) 100%.zip" kind="covers"
 path="smb://host/share/pcengine/images/Game.png"/>
</smbCatalog>''')
            source = folder / 'test.cpp'
            source.write_text(r'''
#include "SmbCatalog.h"
#include <cassert>
int main() {
 using namespace SmbCatalog;
 assert(enabled());
 const std::string game="smb://host/share/pcengine/Game #1 (JP) 100%.zip";
 assert(size(game)==100 && directory("smb://host/share/pcengine"));
 assert(exists(game) && !exists("smb://other/share/escape"));
 assert(media(game,"covers")=="smb://host/share/pcengine/images/Game.png");
 assert(media(game,"videos").empty());
 assert(canonical("smb://host/share/pcengine/./A/../Game.zip") ==
        "smb://host/share/pcengine/Game.zip");
 assert(canonical("smb://host/share/../other/secret").empty());
 assert(canonical("smb://user:password@host/share/file").empty());
 assert(canonical(game)==game);
 assert(!exists("/app0/content/pcengine/Game.zip"));
 assert(!exists("smb://host/share/pcengine/missing.zip"));
}
''')
            exe = folder / 'test'
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            f'-DPS5_SMB_CATALOG="{catalog}"',
                            '-I'+str(PORT / 'frontends/es-de/ps5/include'), '-I'+str(PUGIXML),
                            str(source), str(PUGIXML/'pugixml.cpp'), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_remote_handoff_validates_file_and_keeps_local_guards(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            frontend = folder / 'es-de.bin'
            frontend.write_bytes(b'frontend')
            source = folder / 'test.c'
            source.write_text(r'''
#define PS5_GAME_SMB_TEST 1
#include "ps5_game.c"
#include <assert.h>
static int remote_flags;
int ps5_smb_stat(const char* path, int64_t* size) {
 (void)size; assert(!strncmp(path,"smb://",6)); return remote_flags;
}
int sceSystemServiceLoadExec(const char* path,const char* const* argv) {
 (void)path; (void)argv; return -1;
}
int main(int argc, char** argv) {
 assert(argc==2);
 struct ps5_game game={0}, restored={0}; int kind;
 snprintf(game.frontend,sizeof(game.frontend),"%s/es-de.bin",argv[1]);
 const char* command="/app0/eboot.bin \"smb://host/share/pcengine/Game #1 (JP) 100%.zip\"";
 assert(ps5_game_parse_command(command,&game)==0);
 remote_flags=1; assert(ps5_game_check(&game)==0);
 char request[2048]; snprintf(request,sizeof(request),"%s/request",argv[1]);
 assert(ps5_game_write(request,&game,0)==0);
 assert(ps5_game_read(request,&restored,&kind)==0 && !strcmp(game.content,restored.content));
 remote_flags=3; assert(ps5_game_check(&game)!=0); /* directory */
 remote_flags=0; assert(ps5_game_check(&game)!=0); /* disconnected/missing/other server */
 remote_flags=1; strcpy(game.core,"/untrusted/core.so"); assert(ps5_game_check(&game)!=0);
 game.core[0]=0; strcpy(game.content,"/nonexistent/local.zip"); assert(ps5_game_check(&game)!=0);
 strcpy(game.content,"https://host/game.zip"); assert(ps5_game_check(&game)!=0);
 strcpy(game.content,"smb://host/share/game\nother"); assert(ps5_game_check(&game)!=0);
}
''')
            exe = folder / 'test'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I'+str(PORT / 'src'), str(source), str(PORT / 'src/ps5_library.c'),
                            '-o', str(exe)], check=True)
            subprocess.run([str(exe), str(folder)], check=True)

if __name__ == '__main__': unittest.main()
