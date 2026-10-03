"""Exercise the actual upload normalization patch, including reuse and HW bypass."""
import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class ShaderCoordinates(unittest.TestCase):
    def test_logical_upload_and_hardware_bypass(self):
        spec = importlib.util.spec_from_file_location('patches', ROOT / 'tools/apply-port-patches.py')
        patches = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patches)
        block = next(e[2] for e in patches.EDITS if e[3] == 'patches/series, 0111: all Slang samplers')
        source = r'''
#include <cassert>
#include <memory>
#include <vector>
using VkFormat = int;
using VkImageLayout = int;
enum { VK_FORMAT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
int barriers, copies, allocations, resized;
#define VULKAN_IMAGE_LAYOUT_TRANSITION_LEVELS(...) (++barriers)
struct Size2D { unsigned width, height; };
struct DeferredDisposer { DeferredDisposer(int) {} };
struct Framebuffer {
 Size2D size; int format, id;
 Framebuffer(int,int,Size2D s,int f,int):size(s),format(f),id(++allocations) {}
 Size2D get_size() {return size;}
 int get_format() {return format;}
 int get_image() {return id;}
 int get_view() {return id+100;}
 void set_size(DeferredDisposer &,Size2D s,int f) {size=s;format=f;++resized;}
};
Size2D copied;
int copy_layout;
void vulkan_framebuffer_copy(int,Size2D s,int,int,VkImageLayout layout) {
 ++copies; copied=s; copy_layout=layout;
}
void update_history_info() {}
struct Input {unsigned width,height,physical_width;int format,image,view,layout;};
struct Chain {
 Input input_texture;
 unsigned current_sync_index=0;
 int device=0,memory_properties=0,original_format=37,deferred_calls[3]={};
 std::vector<std::unique_ptr<Framebuffer>> ps5_logical_inputs;
 void run() {int cmd=0;
''' + block + r'''
 }
};
int main() {
 Chain c;
 c.input_texture={304,224,320,0,50,51,VK_IMAGE_LAYOUT_GENERAL}; c.run();
 assert(copies==1 && copied.width==304 && copied.height==224 && barriers==0);
 assert(copy_layout==VK_IMAGE_LAYOUT_GENERAL);
 assert(c.input_texture.physical_width==0 && c.input_texture.format==37);
 assert(c.input_texture.image==1 && c.input_texture.view==101);
 assert(c.input_texture.layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
 c.input_texture={304,224,320,0,50,51,VK_IMAGE_LAYOUT_GENERAL}; c.run();
 assert(allocations==1 && resized==0);
 c.current_sync_index=2;
 c.input_texture={304,224,320,0,50,51,9}; c.run();
 assert(allocations==2 && barriers==2 && copy_layout==VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
 c.input_texture={608,448,640,44,50,51,9}; c.run();
 assert(resized==1 && copied.width==608 && copied.height==448);
 assert(c.input_texture.format==44);
 int before=copies;
 c.input_texture={2880,1632,0,44,99,100,9}; c.run();
 assert(copies==before && c.input_texture.image==99 && c.input_texture.layout==9);
 c.input_texture={256,224,256,0,50,51,1}; c.run();
 assert(copies==before);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'coordinates.cpp'
            path.write_text(source)
            binary = Path(tmp) / 'coordinates'
            subprocess.run(['c++', '-std=c++11', str(path), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
