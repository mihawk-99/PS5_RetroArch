"""The overlay adaptation must draw both halves and retain UV/alpha data."""
import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class OverlayGeometry(unittest.TestCase):
    def test_two_triangles_preserve_all_attributes(self):
        spec = importlib.util.spec_from_file_location('patches', ROOT / 'tools/apply-port-patches.py')
        patches = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patches)
        block = next(e[2] for e in patches.EDITS if e[3] == 'patches/series, 0110: overlays share')
        source = r'''
#include <assert.h>
#include <string.h>
#include <math.h>
struct vk_vertex { float x,y,u,v,r,g,b,a; };
struct Range { void *data; };
struct vk_vertex output[7];
int fail;
int vulkan_buffer_chain_alloc(void *ctx, void *vbo, unsigned size, struct Range *range) {
 assert(size==6*sizeof(struct vk_vertex)); range->data=output; return !fail;
}
int main(void) {
 struct vk_vertex vertices[8]={{0}};
 for(int j=0;j<8;j++) { vertices[j].x=(j%4)/2; vertices[j].y=j%2; vertices[j].u=j; vertices[j].a=j/8.f; }
 struct Chain { int vbo; } chain;
 struct State { void *context; struct Chain *chain; struct { struct vk_vertex *vertex; } overlay; } state={0,&chain,{vertices}},*vk=&state;
 for(fail=0;fail<2;fail++) {
  memset(output,0x55,sizeof(output));
  struct vk_vertex sentinel=output[6];
  for(int i=0;i<2;i++) {
   struct Range range; struct { unsigned vertices; } call;
''' + block + r'''
   assert(call.vertices==6);
   unsigned expected[6]={0,1,2,2,1,3};
   for(int j=0;j<6;j++) assert(!memcmp(&output[j],&vertices[i*4+expected[j]],sizeof(output[j])));
   assert(!memcmp(&sentinel,&output[6],sizeof(sentinel)));
   float area=0;
   for(int j=0;j<6;j+=3) {
    struct vk_vertex a=output[j],b=output[j+1],c=output[j+2];
    area+=fabsf((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))/2;
   }
   assert(area==1.f);
  }
  if(fail) assert(!memcmp(&sentinel,&output[0],sizeof(sentinel)));
 }
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'overlay.c'
            path.write_text(source)
            exe = Path(tmp) / 'overlay'
            subprocess.run(['cc', str(path), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
