"""Execute upstream async-image conversion and platform default selectors.

File decoding/task scheduling are stubs; real task initialization and decoded
pixel conversion must preserve the renderer's channel contract.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from tests.test_xmb_safe_lists import patched, probe, ROOT


class ThumbnailDefaults(unittest.TestCase):
    def run_c(self, program):
        with tempfile.TemporaryDirectory() as td:
            source = Path(td, 'probe.c')
            source.write_text(program)
            binary = Path(td, 'probe')
            subprocess.run(['cc', '-std=gnu11', '-O1', '-g',
                            '-Ivendor/retroarch', '-Ivendor/retroarch/libretro-common/include',
                            str(source), '-o', str(binary)], cwd=ROOT, check=True)
            subprocess.run([str(binary)], check=True, timeout=15)

    def test_async_thumbnail_channel_order(self):
        task = patched('tasks/task_image.c')
        image = patched('libretro-common/formats/image_texture.c')
        structs = task[task.index('enum image_status_enum'):task.index('static int cb_image_upload_generic')]
        self.run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <formats/image.h>
#include "tasks/task_file_transfer.h"
static retro_task_t *queued;
retro_task_t *task_init(void) { return calloc(1, sizeof(retro_task_t)); }
bool task_queue_push(retro_task_t *t) { queued=t; return true; }
static void task_file_load_handler(retro_task_t *t) { (void)t; }
static void task_image_load_free(retro_task_t *t) { (void)t; }
static int cb_nbio_image_thumbnail(void *p,size_t n) { (void)p;(void)n;return 0; }
enum image_type_enum image_texture_get_type(const char *p) { (void)p;return IMAGE_TYPE_PNG; }
''' + structs + '\n' + '\n'.join([
            probe.function(image, 'image_texture_set_color_shifts'),
            probe.function(image, 'image_texture_color_convert'),
            probe.function(task, 'cb_image_upload_generic'),
            probe.function(task, 'task_push_image_load')]) + r'''
int main(void) {
  for(unsigned rgba=0;rgba<2;rgba++) {
    assert(task_push_image_load("test.state.png",rgba,0,NULL,NULL));
    nbio_handle_t *nbio=queued->state;
    struct nbio_image_handle *im=nbio->data;
    /* The decoder outputs ARGB words. Include red, blue and partial alpha. */
    uint32_t pixels[]={0xffff0000,0xff0000ff,0x7f123456};
    im->ti.pixels=pixels; im->ti.width=3;im->ti.height=1;
    im->processing_final_state=IMAGE_PROCESS_END;
    assert(cb_image_upload_generic(nbio,0)==0);
    assert(pixels[0]==(rgba?0xff0000ff:0xffff0000));
    assert(pixels[1]==(rgba?0xffff0000:0xff0000ff));
    assert(pixels[2]==(rgba?0x7f563412:0x7f123456));
    assert(im->ti.supports_rgba==(bool)rgba);
    free(nbio->path);free(im);free(nbio);free(queued);
  }
}
''')

    def test_compiled_input_defaults(self):
        source = patched('configuration.c')
        funcs = [probe.function(source, 'config_get_default_' + name) for name in ('input', 'joypad')]
        enums = []
        for name, body in zip(('input', 'joypad'), funcs):
            values = sorted(set(re.findall(r'case (\w+):', body)))
            enums.append('enum ' + name + '_driver_enum {' + ','.join(values) + '};')
        self.run_c('#include <assert.h>\n#include <string.h>\n' + '\n'.join(enums) +
                   '\n#define INPUT_DEFAULT_DRIVER INPUT_NULL\n#define JOYPAD_DEFAULT_DRIVER JOYPAD_NULL\n' +
                   '\n'.join(funcs) + '\nint main(void) { assert(!strcmp(config_get_default_input(),"ps5"));'
                   'assert(!strcmp(config_get_default_joypad(),"ps5")); }')

    def test_reset_reannounces_connected_controller(self):
        source = patched('retroarch.c')
        start = source.index('      case CMD_EVENT_MENU_RESET_TO_DEFAULT_CONFIG:')
        end = source.index('      case CMD_EVENT_MENU_SAVE_CURRENT_CONFIG:', start)
        self.run_c('#include <assert.h>\nstatic int reset,refresh;\n'
                   'void *global_get_ptr(void) { return 0; }\n'
                   'void config_set_defaults(void *p) { (void)p;reset++; }\n'
                   'void ps5_input_reset_autoconfig(void) { assert(reset);refresh++; }\n'
                   '#define CMD_EVENT_MENU_RESET_TO_DEFAULT_CONFIG 1\n'
                   'int main(void) { switch(1) {\n' + source[start:end] +
                   '} assert(reset==1 && refresh==1); }')
