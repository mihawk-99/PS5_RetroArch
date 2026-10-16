"""Exercise the frontend's actual read function when capture produces no samples."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class MicrophoneRead(unittest.TestCase):
    def compile(self, args):
        result = subprocess.run(args, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_frontend_empty_capture_does_not_spin(self):
        """Run the frontend's real read function with a capture that makes no progress."""
        compiler = shutil.which('clang++') or shutil.which('g++')
        c_compiler = shutil.which('clang') or shutil.which('cc')
        tree = ROOT / 'build/ra-conf'
        if not compiler or not c_compiler or not (tree / 'audio/audio_driver.c').exists():
            self.skipTest('requires a configured frontend tree and host compilers')
        source = (tree / 'audio/audio_driver.c').read_text()
        begin = source.index('int microphone_driver_read(')
        end = source.index('\nbool microphone_driver_get_effective_params(', begin)
        function = source[begin:end]
        self.assertIn('patches/series, 0133', function)
        program = r'''
#include <audio/microphone_driver.h>
#include <retro_miscellaneous.h>
#include <cassert>
#include <cstring>
#include <algorithm>
#define RUNLOOP_FLAG_PAUSED 1
#define RUNLOOP_FLAG_FASTMOTION 2
#define RUNLOOP_FLAG_SLOWMOTION 4
#define AUDIO_CHUNK_SIZE_NONBLOCKING 512
#define RARCH_ERR(...) ((void)0)
#define retro_assert assert
static microphone_driver_state_t mic_driver_st{};
static unsigned flags = 0, flushes = 0;
static unsigned runloop_get_flags() { return flags; }
static bool state_manager_frame_is_reversed() { return false; }
static size_t microphone_driver_flush(microphone_driver_state_t *, retro_microphone_t *, size_t)
{ ++flushes; return 0; }
static bool alive(const void *,const void *) { return true; }
static int backend_read(void *,void *,void *,size_t) { return 0; }
'''+function+r'''
int main() {
    microphone_driver_t driver{};
    driver.read = backend_read; driver.mic_alive = alive;
    mic_driver_st.driver = &driver;
    mic_driver_st.driver_context = &driver;
    mic_driver_st.flags = MICROPHONE_DRIVER_FLAG_ACTIVE;
    mic_driver_st.input_frames = &driver;
    retro_microphone_t mic{};
    mic.flags = MICROPHONE_FLAG_ACTIVE | MICROPHONE_FLAG_ENABLED;
    mic.microphone_context = &driver;
    mic.outgoing_samples = fifo_new(128);
    int16_t out[8]; std::fill_n(out,8,123);
    assert(microphone_driver_read(&mic,out,8)==8 && flushes==1);
    assert(std::all_of(out,out+8,[](int16_t x){return x==0;}));
    flags=RUNLOOP_FLAG_PAUSED; std::fill_n(out,8,123);
    assert(microphone_driver_read(&mic,out,8)==8 && flushes==1);
    assert(std::all_of(out,out+8,[](int16_t x){return x==0;}));
    flags=0;
    int16_t queued[8]={1,2,3,4,5,6,7,8};
    fifo_write(mic.outgoing_samples,queued,sizeof(queued));
    assert(microphone_driver_read(&mic,out,8)==8 && flushes==1);
    assert(std::equal(out,out+8,queued));
    fifo_free(mic.outgoing_samples);
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / 'frontend.cpp').write_text(program)
            includes = ['-I'+str(tree), '-I'+str(tree / 'libretro-common/include')]
            self.compile([c_compiler,*includes,'-c',str(tree / 'libretro-common/queues/fifo_queue.c'),
                '-o',str(folder / 'fifo.o')])
            self.compile([compiler,'-std=c++17',*includes,str(folder / 'frontend.cpp'),
                str(folder / 'fifo.o'),'-o',str(folder / 'test')])
            subprocess.run([str(folder / 'test')],check=True,timeout=5)

if __name__ == '__main__':
    unittest.main()
