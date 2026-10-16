"""Exercise patched core input/cursor code without a console or game image."""
from pathlib import Path
import os
import json
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class TouchpadStylus(unittest.TestCase):
    def compile_run(self, source, includes=()):
        with tempfile.TemporaryDirectory() as td:
            cpp = Path(td, 'test.cpp')
            cpp.write_text(source)
            binary = str(Path(td, 'test'))
            subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined',
                            '-fno-omit-frame-pointer', *['-I'+str(p) for p in includes],
                            str(cpp), '-o', binary], check=True)
            subprocess.run([binary], check=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))

    def patched(self, core, path):
        # Apply the committed patch to the exact pinned fork, independent of the
        # current configured build. This also checks build reproducibility.
        script = (ROOT / f'tools/build-{core}.sh').read_text()
        revision = script.split('revision=')[1].split()[0]
        patch = ROOT / f'tooling/{core}/touchpad-stylus.patch'
        paths = [line[6:] for line in patch.read_text().splitlines() if line.startswith('+++ b/')]
        with tempfile.TemporaryDirectory() as td:
            for filename in paths:
                output = Path(td, filename)
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_bytes(subprocess.check_output(
                    ['git', '-C', str(ROOT / f'.deps/{core}-src'), 'show', f'{revision}:{filename}']))
            subprocess.run(['patch', '-s', '-p1', '-i', str(patch)], cwd=td, check=True)
            return Path(td, path).read_text()

    def test_core_and_menu_scope(self):
        source = (ROOT / 'src/input_ps5.cpp').read_text()
        self.compile_run('''
#include <cassert>
#include <cstring>
#define HAVE_MENU
constexpr unsigned MENU_ST_FLAG_ALIVE = 1, RUNLOOP_FLAG_CORE_RUNNING = 2;
struct {unsigned flags;} menu{};
struct {unsigned flags; struct {struct {const char* library_name;} info;} system;} runloop{};
auto menu_state_get_ptr() {return &menu;}
auto runloop_state_get_ptr() {return &runloop;}
''' + function(source, 'bool stylus_enabled()') + '''
int main() {
  runloop.system.info.library_name = "Azahar";
  assert(!stylus_enabled());
  runloop.flags = RUNLOOP_FLAG_CORE_RUNNING;
  assert(stylus_enabled());
  menu.flags = MENU_ST_FLAG_ALIVE;
  assert(!stylus_enabled());
  menu.flags = 0;
  runloop.system.info.library_name = "DeSmuME";
  assert(stylus_enabled());
  runloop.system.info.library_name = "melonDS DS";
  assert(stylus_enabled());
  runloop.system.info.library_name = "Other";
  assert(!stylus_enabled());
  runloop.system.info.library_name = nullptr;
  assert(!stylus_enabled());
}
''')

    def test_melonds_relative_mouse(self):
        cache = ROOT / '.deps/melondsds-cache/melondsds'
        glm = ROOT / 'build/cores/melondsds/deps/glm-src'
        if not cache.exists() or not glm.exists():
            self.skipTest('requires the pinned melonDS DS source cache and GLM headers')
        pin = json.loads((ROOT/'tooling/melondsds/sources.json').read_text())['melondsds'][1]
        with tempfile.TemporaryDirectory() as td:
            for name in ('cursor.cpp', 'cursor.hpp'):
                path = 'src/libretro/input/'+name
                target = Path(td, path); target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(subprocess.check_output(['git', '-C', str(cache), 'show', pin+':'+path]))
            subprocess.run(['patch', '-s', '--fuzz=0', '-p1', '-d', td, '-i',
                            str(ROOT/'tooling/melondsds/touchpad-stylus.patch')], check=True)
            method = function(Path(td,'src/libretro/input/cursor.cpp').read_text(),
                              'void CursorState::UpdateRelativeMouse()')
        self.compile_run(r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/common.hpp>
using glm::vec2; using glm::clamp;
template<typename T> const glm::vec<2,T> NDS_SCREEN_SIZE{256,192};
enum {RETRO_DEVICE_MOUSE, RETRO_DEVICE_ID_MOUSE_X=0, RETRO_DEVICE_ID_MOUSE_Y=1,
      RETRO_DEVICE_ID_MOUSE_LEFT=2};
int16_t mouse[3]{};
namespace retro { int16_t input_state(unsigned,unsigned,unsigned,unsigned id) {return mouse[id];} }
uint64_t ticks=100;
uint64_t cpu_features_get_perf_counter() {return ++ticks;}
class CursorState {
public:
  bool _mouseTouching=false, _isTouchReleased=false, _joypadCursorTouching=false;
  uint64_t _pointerCursorLastUpdate=0, _joypadCursorLastUpdate=0;
  vec2 _joystickCursorPosition{128,96}; glm::ivec2 _pointerCursorPosition{40,30};
  unsigned resets=0;
  void ResetCursorTimeout() {++resets;}
  void UpdateRelativeMouse() noexcept;
};
''' + method + r'''
int main() {
  CursorState c; c.UpdateRelativeMouse(); assert(c.resets==0);
  mouse[0]=192; mouse[1]=108; c.UpdateRelativeMouse();
  assert(std::fabs(c._joystickCursorPosition.x-153.6f)<0.001f);
  assert(std::fabs(c._joystickCursorPosition.y-115.2f)<0.001f);
  float before=c._joystickCursorPosition.x; mouse[0]=1; mouse[1]=0;
  for(int i=0;i<8;++i) c.UpdateRelativeMouse();
  assert(c._joystickCursorPosition.x>before+1); // Preserve subpixel motion.
  mouse[0]=mouse[1]=32767; c.UpdateRelativeMouse();
  assert(c._joystickCursorPosition==vec2(255,191));
  mouse[0]=mouse[1]=-32767; c.UpdateRelativeMouse();
  assert(c._joystickCursorPosition==vec2(0,0));
  mouse[0]=mouse[1]=0; mouse[2]=1; c.UpdateRelativeMouse();
  assert(c._joypadCursorTouching && !c._isTouchReleased);
  c._joypadCursorTouching=false; mouse[2]=0; c.UpdateRelativeMouse();
  assert(c._isTouchReleased && !c._joypadCursorTouching);
  c._isTouchReleased=false; c._joypadCursorTouching=true; c.UpdateRelativeMouse();
  assert(c._joypadCursorTouching && !c._isTouchReleased); // R2 remains independent.
  c._pointerCursorLastUpdate=500; mouse[0]=1; c.UpdateRelativeMouse();
  assert(c._joystickCursorPosition.x>40 && c._joystickCursorPosition.y==30);
}
''', includes=[glm])

    def test_desmume_stylus_pixels_and_clipping(self):
        source = self.patched('desmume', 'desmume/src/frontend/libretro/libretro.cpp')
        self.compile_run('''
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstdint>
#include <vector>
constexpr int RETRO_PIXEL_FORMAT_XRGB8888 = 1;
int colorMode;
uint32_t pointer_color_32=0xffffff;
uint16_t pointer_colour=0xffff;
unsigned scale=1, GPU_LR_FRAMEBUFFER_NATIVE_WIDTH=256, GPU_LR_FRAMEBUFFER_NATIVE_HEIGHT=192;
int TouchX=0,TouchY=0,FramesWithPointer=600,hybrid_layout_scale=1,hybrid_layout_ratio=3,bpp=2;
int gap_size(){return 0;}
''' + function(source, 'static inline int32_t Saturate(') +
            function(source, 'static void DrawStylus(') +
            function(source, 'static void DrawPointer(') +
            function(source, 'static void DrawPointerHybrid(') + '''
int main() {
  for (int mode : {0,1}) {
    colorMode=mode; bpp=mode ? 4 : 2;
    std::vector<uint32_t> frame(768*1152);
    for (int hybrid : {1,3}) {
      hybrid_layout_scale=hybrid;
      FramesWithPointer=600; TouchX=255; TouchY=191;
      DrawPointer(reinterpret_cast<uint16_t*>(frame.data()),768);
      DrawPointerHybrid(reinterpret_cast<uint16_t*>(frame.data()),768,true);
      DrawPointerHybrid(reinterpret_cast<uint16_t*>(frame.data()),768,false);
    }
  }
  for (int mode : {0,1}) for (int scale : {1,5}) {
    colorMode = mode;
    const int w = 32*scale, h = 24*scale, pitch = w+8;
    for (int x : {-3, 0, w-1}) for (int y : {-3,0,h-1}) {
      std::vector<uint32_t> pixels(pitch*h+8, 0x12345678);
      DrawStylus(reinterpret_cast<uint16_t*>(pixels.data()),pitch,w,h,x,y,scale);
      for (int row=0;row<h;++row) for(int col=w;col<pitch;++col) {
        if(mode) assert(pixels[row*pitch+col]==0x12345678);
        else assert(reinterpret_cast<uint16_t*>(pixels.data())[row*pitch+col] ==
                    ((row*pitch+col)%2 ? 0x1234 : 0x5678));
      }
      assert(pixels.back()==0x12345678);
      if(x==0 && y==0) {
        if(mode) assert(pixels[0]!=0x12345678);
        else {auto p=reinterpret_cast<uint16_t*>(pixels.data());
          assert(p[0]!=0x5678);}
      }
    }
  }
}
''')

    def test_azahar_stylus_matches_desmume(self):
        software = self.patched('azahar', 'src/citra_libretro/input/mouse_tracker.cpp')
        desmume = self.patched('desmume', 'desmume/src/frontend/libretro/libretro.cpp')
        shader = self.patched('azahar', 'src/video_core/host_shaders/vulkan_cursor.frag')
        # Compile the real shaders together, then exercise the fragment's actual
        # shape predicate against DeSmuME's rasterizer at native and scaled sizes.
        with tempfile.TemporaryDirectory() as td:
            for ext in ('vert', 'frag'):
                Path(td, f'cursor.{ext}').write_text(self.patched(
                    'azahar', f'src/video_core/host_shaders/vulkan_cursor.{ext}'))
            subprocess.run(['glslangValidator', '-V', '-l', 'cursor.vert', 'cursor.frag'],
                           cwd=td, check=True, capture_output=True)
        shape = shader[shader.index('            float along'):shader.index('                color +=')]
        shape = shape.replace('p.x', 'dx').replace('p.y', 'dy')
        shape = shape.replace('vec3 ink', 'uint32_t ink').replace('vec3(1.0)', '0xffffff')
        shape = shape.replace('vec3(32.0, 40.0, 48.0) / 255.0', '0x202830')
        # GLSL literals are floats; preserve that in the extracted C++ predicate.
        import re
        shape = re.sub(r'(\d+\.\d+)', r'\1f', shape)
        self.compile_run("""
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstdint>
#include <vector>
using std::min; using std::max; using std::abs;
constexpr int RETRO_PIXEL_FORMAT_XRGB8888=1;
int colorMode=1;
uint32_t pointer_color_32=0xffffff;
namespace Layout {
struct FramebufferLayout {
  int left,top,right,bottom;
  bool IsWithinTouchscreen(int x,int y)const{return x>=left&&x<right&&y>=top&&y<bottom;}
}; }
struct SoftwareCursorRenderer {
  void Render(int,int,float,float,float,const Layout::FramebufferLayout&,void*);
};
uint32_t shader_sample(float dx,float dy,uint32_t original) {
""" + shape + """
                return ink;
            }
    return original;
}
""" + function(desmume, 'static void DrawStylus(') +
            function(software, 'void SoftwareCursorRenderer::Render(') + """
int main() {
  SoftwareCursorRenderer cursor;
  for(int scale : {1,5,18}) {
    const int w=40*scale,h=32*scale;
    Layout::FramebufferLayout layout{0,0,w,h};
    for(int x : {0,w/2,w-1}) for(int y : {0,h/2,h-1}) {
      std::vector<uint32_t> ds(w*h,0x123456),az=ds;
      DrawStylus(reinterpret_cast<uint16_t*>(ds.data()),w,w,h,x,y,scale);
      cursor.Render(w,h,x,y,8*scale,layout,az.data());
      assert(ds==az);
      for(int py=0;py<h;++py) for(int px=0;px<w;++px) {
        unsigned r=0,g=0,b=0;
        for(int sy=0;sy<2;++sy) for(int sx=0;sx<2;++sx) {
          auto c=shader_sample((px-x+(sx-.5f)*.5f)/scale,
                               (py-y+(sy-.5f)*.5f)/scale,0x123456);
          r+=(c>>16)&255;g+=(c>>8)&255;b+=c&255;
        }
        assert(ds[py*w+px]==((r/4)<<16|(g/4)<<8|(b/4)));
      }
      // Preserve alpha and never paint outside the lower screen.
      layout={3*scale,4*scale,w-3*scale,h-4*scale};
      az.assign(w*h,0xff123456);
      cursor.Render(w,h,x,y,8*scale,layout,az.data());
      for(int py=0;py<h;++py) for(int px=0;px<w;++px) {
        assert((az[py*w+px]>>24)==255);
        if(!layout.IsWithinTouchscreen(px,py)) assert(az[py*w+px]==0xff123456);
        else assert((az[py*w+px]&0xffffff)==ds[py*w+px]);
      }
      layout={0,0,w,h};
    }
  }
  cursor.Render(40,50,10,20,8,{0,0,40,50},nullptr);
}
""")

    def test_azahar_stylus_geometry(self):
        source = self.patched('azahar', 'src/video_core/renderer_vulkan/renderer_vulkan.cpp')
        start = source.index('    const float buf_w', source.index('void RendererVulkan::DrawCursor('))
        end = source.index('    const u64 size = sizeof(vertices);', start)
        self.compile_run("""
#include <algorithm>
#include <cassert>
#include <cmath>
#include <initializer_list>
struct Rect {float left,top,right,bottom; float GetHeight()const{return bottom-top;}};
int main() {
  for(float resolution : {1.f,5.f,18.f}) for(float x : {0.f,160.f,319.f})
    for(float y : {0.f,120.f,239.f}) {
      struct {float width,height; Rect bottom_screen;} layout{
        400*resolution,480*resolution,{40*resolution,240*resolution,360*resolution,480*resolution}};
      struct {float projected_x,projected_y;} cursor{x*resolution,y*resolution};
""" + source[start:end] + """
      assert(sizeof(vertices)/sizeof(float)==24);
      for(unsigned i=0;i<24;i+=4) {
        float px=(vertices[i]+1)*buf_w/2, py=(vertices[i+1]+1)*buf_h/2;
        assert(px>=layout.bottom_screen.left-.001 && px<=layout.bottom_screen.right+.001);
        assert(py>=layout.bottom_screen.top-.001 && py<=layout.bottom_screen.bottom+.001);
        // Every clipped vertex still measures its local position from the nib.
        assert(std::abs(px-(abs_x+vertices[i+2]*scale))<.001);
        assert(std::abs(py-(abs_y+vertices[i+3]*scale))<.001);
      }
    }
}
""")

    def test_azahar_hover_drag_and_resolution(self):
        source = self.patched('azahar', 'src/citra_libretro/input/mouse_tracker.cpp')
        bodies = '\n'.join(function(source, signature) for signature in [
            'void MouseTracker::OnMouseMove(', 'void MouseTracker::Restrict(',
            'void MouseTracker::Update('])
        self.compile_run('''
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <initializer_list>
enum {RETRO_DEVICE_MOUSE,RETRO_DEVICE_POINTER,RETRO_DEVICE_JOYPAD,RETRO_DEVICE_ANALOG};
enum {RETRO_DEVICE_ID_MOUSE_X,RETRO_DEVICE_ID_MOUSE_Y,RETRO_DEVICE_ID_MOUSE_LEFT};
enum {RETRO_DEVICE_ID_POINTER_X,RETRO_DEVICE_ID_POINTER_Y,RETRO_DEVICE_ID_POINTER_PRESSED};
constexpr int RETRO_DEVICE_ID_JOYPAD_R3=0, RETRO_DEVICE_INDEX_ANALOG_RIGHT=0;
constexpr int RETRO_DEVICE_ID_ANALOG_X=0, RETRO_DEVICE_ID_ANALOG_Y=1;
namespace Layout {
struct Rect {int left=0, top=0, right=320,bottom=240;
unsigned GetWidth() const {return right-left;} unsigned GetHeight() const{return bottom-top;}};
struct FramebufferLayout {Rect bottom_screen;
bool IsWithinTouchscreen(int x,int y) const {return x>=bottom_screen.left &&
x<bottom_screen.right && y>=bottom_screen.top && y<bottom_screen.bottom;}};
}
namespace LibRetro {
enum class CStickFunction {CStick, Touch};
struct {bool enable_mouse_touchscreen=true,enable_touch_touchscreen=true;
CStickFunction analog_function=CStickFunction::CStick; float analog_deadzone=0.1f;} settings;
int inputs[4][3]{};
int CheckInput(int,int dev,int,int id) {return inputs[dev][id];}
namespace Input {
class MouseTracker {public:
int x=0,y=0; float lastMouseX=0,lastMouseY=0,projectedX=0,projectedY=0;
float mouseRemainderX=0,mouseRemainderY=0;
bool isPressed=false;
std::chrono::steady_clock::time_point last_moved{};
Layout::FramebufferLayout framebuffer_layout;
void OnMouseMove(int,int); void Restrict(int,int,int,int);
void Update(int,int,const Layout::FramebufferLayout&);
};
''' + bodies + '''
}}
int main() {
using namespace LibRetro;
for (int scale : {1,5,18}) {
  Input::MouseTracker tracker;
  Layout::FramebufferLayout layout;
  layout.bottom_screen = {40*scale,240*scale,360*scale,480*scale};
  auto update=[&]{tracker.Update(400*scale,480*scale,layout);};
  inputs[RETRO_DEVICE_MOUSE][0]=960;
  inputs[RETRO_DEVICE_MOUSE][1]=540;
  inputs[RETRO_DEVICE_MOUSE][2]=0;
  update();
  assert(tracker.x==160*scale && tracker.y==120*scale && !tracker.isPressed);
  assert(tracker.last_moved.time_since_epoch().count()>0);
  inputs[RETRO_DEVICE_MOUSE][0]=0; inputs[RETRO_DEVICE_MOUSE][1]=0;
  inputs[RETRO_DEVICE_MOUSE][2]=1;
  // Unpressed absolute pointer coordinates must never snap a mouse click.
  inputs[RETRO_DEVICE_POINTER][0]=32767;
  update();
  assert(tracker.isPressed && tracker.x==160*scale && tracker.y==120*scale);
  inputs[RETRO_DEVICE_MOUSE][0]=32767; inputs[RETRO_DEVICE_MOUSE][1]=32767;
  update(); assert(tracker.x==320*scale-1 && tracker.y==240*scale-1);
  inputs[RETRO_DEVICE_MOUSE][0]=-32767; inputs[RETRO_DEVICE_MOUSE][1]=-32767;
  inputs[RETRO_DEVICE_MOUSE][2]=0;
  update(); assert(tracker.x==0 && tracker.y==0 && !tracker.isPressed);
}
}
''')
