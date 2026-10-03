"""Execute the port's actual display selection with shuffled available modes."""
import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class DisplayRefresh(unittest.TestCase):
    def test_explicit_refresh_and_automatic_fastest(self):
        spec = importlib.util.spec_from_file_location('patches', ROOT / 'tools/apply-port-patches.py')
        patches = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patches)
        selection = next(e[2] for e in patches.EDITS if e[3] == 'patches/series, 0084): the requested size')
        change = next(e for e in patches.EDITS if e[3] == 'patches/series, 0109: explicit modes honor')
        selection = selection.replace(change[1], change[2])
        source = r'''
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
typedef struct { unsigned displayMode; struct { struct { unsigned width, height; } visibleRegion; unsigned refreshRate; } parameters; } VkDisplayModePropertiesKHR;
struct Info { unsigned width, height, refresh_rate_x1000; };
unsigned choose(VkDisplayModePropertiesKHR *modes, unsigned mode_count, struct Info *info) {
 unsigned w=0,h=0,*width=&w,*height=&h,i,best_mode=0,vulkan_ps5_display_refresh_x1000=0;
''' + selection + r'''
 return vulkan_ps5_display_refresh_x1000;
}
int main(void) {
 VkDisplayModePropertiesKHR modes[]={{1,{{3840,2160},119880}},{2,{{3840,2160},59940}},{3,{{1920,1080},119880}}};
 struct Info automatic={0,0,59940}, sixty={3840,2160,59940}, fast={3840,2160,119880}, rounded={3840,2160,60000};
 for(int i=0;i<2;i++) {
  assert(choose(modes,3,&automatic)==119880);
  assert(choose(modes,3,&sixty)==59940);
  assert(choose(modes,3,&fast)==119880);
  assert(choose(modes,3,&rounded)==59940);
  VkDisplayModePropertiesKHR tmp=modes[0]; modes[0]=modes[1]; modes[1]=tmp;
 }
 assert(choose(modes+1,2,&fast)==59940);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'selection.c'
            path.write_text(source)
            exe = Path(tmp) / 'selection'
            subprocess.run(['cc', str(path), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
