"""The default XMB menu must ship every fixed icon and its usable font."""

import hashlib
import json
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parent.parent


class XmbAssets(unittest.TestCase):
    def test_xmb_enabled_with_rgui_available(self):
        flags = subprocess.check_output(['bash', 'tools/retroarch-sources.sh', '--config'],
                                        cwd=ROOT, text=True).splitlines()
        self.assertIn('--enable-xmb', flags)
        self.assertIn('--enable-rgui', flags)
        self.assertNotIn('--disable-xmb', flags)

    def test_complete_pinned_theme(self):
        base = ROOT / 'assets/xmb'
        manifest = json.loads((base / 'source.json').read_text())
        for name, expected in manifest['files'].items():
            with self.subTest(asset=name):
                self.assertEqual(hashlib.sha256((base / name).read_bytes()).hexdigest(), expected)
        source = (ROOT / 'vendor/retroarch/menu/drivers/xmb.c').read_text()
        start = source.index('static const char *xmb_texture_path')
        end = source.index('\n}', start)
        names = set(re.findall(r'"([^"/]+\.png)"', source[start:end]))
        self.assertTrue(names)
        for name in names:
            self.assertEqual((base / 'monochrome/png' / name).read_bytes()[:8], b'\x89PNG\r\n\x1a\n')
        self.assertEqual((base / 'monochrome/font.ttf').read_bytes()[:4], b'\0\1\0\0')
        self.assertTrue((base / 'monochrome/FONT-LICENSE.txt').is_file())


class XmbGeometry(unittest.TestCase):
    def test_strip_expansion_preserves_triangles_and_bounds(self):
        """Execute the port's C mapping, including the 8,064-vertex ribbon."""
        import ast
        import tempfile
        source = (ROOT / 'vendor/retroarch/gfx/drivers/vulkan.c').read_text()
        tree = ast.parse((ROOT / 'tools/apply-port-patches.py').read_text())
        edits = next(ast.literal_eval(n.value) for n in tree.body
                     if isinstance(n, ast.Assign) and n.targets[0].id == 'EDITS')
        for name, anchor, replacement, marker in edits:
            if name == 'gfx/drivers/vulkan.c' and marker not in source:
                self.assertIn(anchor, source)
                source = source.replace(anchor, replacement, 1)
        count = re.search(r'const unsigned output_count =\s*(.*?);', source, re.S).group(1)
        index = re.search(r'const unsigned s = as_strip\s*(.*?);', source, re.S).group(1)
        code = '''#include <assert.h>
#include <stdbool.h>
unsigned count(unsigned source_count, bool as_strip) { return COUNT; }
unsigned index_at(unsigned i, bool as_strip) { return as_strip INDEX; }
int main(void) {
    const unsigned quad[] = {0,1,2,2,1,3};
    for (unsigned i=0;i<6;i++) assert(index_at(i,true)==quad[i]);
    assert(count(0,true)==0 && count(1,true)==0 && count(2,true)==0);
    for (unsigned n=3;n<=8064;n++) {
        assert(count(n,true)==3*(n-2));
        assert(count(n,false)==n);
        for (unsigned i=0;i<count(n,true);i+=3) {
            unsigned a=index_at(i,true), b=index_at(i+1,true), c=index_at(i+2,true);
            assert(a<n && b<n && c<n);
            /* Each consecutive source triple appears once, with strip winding. */
            unsigned t=i/3;
            assert(c==t+2 && a+b==2*t+1);
            assert((t%2==0 && a<b) || (t%2==1 && b<a));
        }
        assert(index_at(n-1,false)==n-1);
    }
}
'''.replace('COUNT', count).replace('INDEX', index)
        helper = re.search(r'static unsigned ps5_vulkan_texture_width.*?\n}', source, re.S).group(0)
        code = code.replace('int main(void)', helper + '\nint main(void)')
        code = code.replace('const unsigned quad[]', '''assert(ps5_vulkan_texture_width(1,4)==64);
    assert(ps5_vulkan_texture_width(320,4)==320);
    assert(ps5_vulkan_texture_width(336,1)==512);
    assert(ps5_vulkan_texture_width(720,1)==768);
    for (unsigned bpp=1;bpp<=4;bpp*=2)
        for (unsigned width=1;width<=4096;width++) {
            unsigned physical=ps5_vulkan_texture_width(width,bpp);
            assert(physical>=width && (physical*bpp)%256==0);
            assert(physical-width < 256/bpp);
        }
    const unsigned quad[]''')
        with tempfile.TemporaryDirectory() as td:
            path = Path(td)
            (path / 'strip.c').write_text(code)
            subprocess.run(['cc', '-O2', '-Wall', '-Werror', str(path / 'strip.c'),
                            '-o', str(path / 'strip')], check=True)
            subprocess.run([str(path / 'strip')], check=True)
        self.assertIn('call.vertices     = output_count; /* patches/series, 0061: effect', source)
        self.assertIn('call.vertices     = output_count; /* patches/series, 0061: icon', source)
