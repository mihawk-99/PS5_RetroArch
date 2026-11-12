"""The XMB menu and graphics widgets ship their pinned icons and fonts."""

import fnmatch
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
        self.assertIn('--enable-gfx_widgets', flags)
        self.assertNotIn('--disable-xmb', flags)
        self.assertNotIn('--disable-gfx_widgets', flags)

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

    def test_widget_icons_are_present_and_pinned(self):
        base = ROOT / 'assets/xmb'
        manifest = json.loads((base / 'source.json').read_text())
        arrays = [
            ('gfx/gfx_widgets.c', 'gfx_widgets_icons_names'),
            ('gfx/widgets/gfx_widget_volume.c', 'ICONS_NAMES'),
        ]
        for filename, array in arrays:
            source = (ROOT / 'vendor/retroarch' / filename).read_text()
            body = re.search(r'\b' + array + r'\[[^]]+\]\s*=\s*\{([^}]+)\}', source)
            self.assertIsNotNone(body, filename)
            names = re.findall(r'"([^"/]+\.png)"', body.group(1))
            self.assertTrue(names, filename)
            for name in names:
                with self.subTest(asset=name):
                    path = 'monochrome/png/' + name
                    self.assertIn(path, manifest['files'])
                    data = (base / path).read_bytes()
                    self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
                    self.assertEqual(hashlib.sha256(data).hexdigest(), manifest['files'][path])

    def test_widget_fonts_are_pinned_and_licensed(self):
        base = ROOT / 'assets/ozone'
        manifest = json.loads((base / 'source.json').read_text())
        xmb = json.loads((ROOT / 'assets/xmb/source.json').read_text())
        self.assertEqual(manifest['revision'], xmb['revision'])
        required = {'regular.ttf', 'bold.ttf', 'README.md', 'INTER-LICENSE.txt', 'MPLUS-LICENSE.txt'}
        self.assertEqual(set(manifest['files']), required)
        for name, expected in manifest['files'].items():
            with self.subTest(asset=name):
                self.assertEqual(hashlib.sha256((base / name).read_bytes()).hexdigest(), expected)
        for name in ('regular.ttf', 'bold.ttf'):
            self.assertEqual((base / name).read_bytes()[:4], b'\0\1\0\0')

        components = json.loads((ROOT / 'tooling/notices/components.json').read_text())['components']
        component = next(c for c in components if c['id'] == 'retroarch-widget-fonts')
        for name in required | {'source.json'}:
            path = 'assets/ozone/' + name
            self.assertTrue(any(fnmatch.fnmatchcase(path, pattern)
                                for pattern in component['artifacts']), path)
        texts = {entry['from'] for entry in component['texts']}
        self.assertIn('assets/ozone/INTER-LICENSE.txt', texts)
        self.assertIn('assets/ozone/MPLUS-LICENSE.txt', texts)
        self.assertIn('assets/ozone/README.md', texts)


class XmbSystemIcons(unittest.TestCase):
    """Issue 35: each playlist tab shows its system's hardware (tools/xmb-system-icons.py)."""

    def test_one_icon_per_database_and_owned_by_its_licence(self):
        import sys
        import tempfile
        from PIL import Image
        sys.path.insert(0, str(ROOT / 'tools'))
        generator = __import__('importlib').import_module('xmb-system-icons')
        databases = [database for _, database in generator.platforms()]
        self.assertGreaterEqual(len(databases), 48)
        self.assertIn('Nintendo - Super Nintendo Entertainment System', databases)
        with tempfile.TemporaryDirectory() as temp:
            subprocess.run([sys.executable, 'tools/xmb-system-icons.py', temp], cwd=ROOT, check=True,
                           capture_output=True)
            made = sorted(p.name for p in Path(temp).iterdir())
            self.assertEqual(made, sorted(f'{database}.png' for database in databases))
            self.assertFalse([name for name in made if name.endswith('-content.png')])
            for name in made:
                with Image.open(Path(temp) / name) as icon:
                    self.assertEqual((icon.size, icon.mode), ((256, 256), 'RGBA'), name)
                    # The caption (logo and year) is cropped off: the hardware is centred,
                    # so the bottom rows are as transparent as the top ones.
                    box = icon.getchannel('A').getbbox()
                    self.assertLessEqual(abs(box[1] - (256 - box[3])), 2, name)
        components = {c['id']: c for c in json.loads((ROOT / 'tooling/notices/components.json').read_text())['components']}
        fixed = [f'assets/xmb/monochrome/png/{p.name}' for p in (ROOT / 'assets/xmb/monochrome/png').iterdir()]
        generated = [f'assets/xmb/monochrome/png/{name}' for name in made]
        owns = lambda component, path: any(fnmatch.fnmatchcase(path, g) for g in components[component]['artifacts'])
        for path in fixed:
            self.assertTrue(owns('retroarch-assets', path) and not owns('xmb-system-icons', path), path)
        for path in generated:
            self.assertTrue(owns('xmb-system-icons', path) and not owns('retroarch-assets', path), path)


    def test_other_playlist_names_find_their_system_icon(self):
        """Patch 0118: a playlist named "PS2" or "Nintendo 3DS" (a manual scan's custom
        name) shows the icon the build made for its system's database."""
        import sys
        import tempfile
        sys.path.insert(0, str(ROOT / 'tools'))
        generator = __import__('importlib').import_module('xmb-system-icons')
        databases = {database for _, database in generator.platforms()}
        names = ['PS2', 'PSP', 'Nintendo 3DS', 'MAME', 'SNES', 'Sega Genesis', 'content', 'Favorites']
        code = '#include <stdio.h>\n#include "ps5_library.h"\n' \
               'int sceSystemServiceLoadExec(const char *p, const char *const *a) { (void)p; (void)a; return -1; }\n' \
               'int main(int c, char **v) {\n' \
               '  for (int i = 1; i < c; i++) { const char *d = ps5_library_platform_database(ps5_library_platform(v[i]));\n' \
               '    printf("%s\\n", d ? d : "-"); }\n  return 0;\n}\n'
        with tempfile.TemporaryDirectory() as temp:
            (Path(temp) / 'names.c').write_text(code)
            subprocess.run(['cc', '-std=c11', '-Isrc', str(Path(temp) / 'names.c'), 'src/ps5_library.c', 'src/ps5_game.c',
                            '-o', str(Path(temp) / 'names')], cwd=ROOT, check=True)
            found = subprocess.check_output([str(Path(temp) / 'names'), *names], cwd=ROOT, text=True).split('\n')[:-1]
        expected = {'PS2': 'Sony - PlayStation 2', 'PSP': 'Sony - PlayStation Portable',
                    'Nintendo 3DS': 'Nintendo - Nintendo 3DS', 'MAME': 'MAME',
                    'SNES': 'Nintendo - Super Nintendo Entertainment System',
                    'Sega Genesis': 'Sega - Mega Drive - Genesis', 'content': '-', 'Favorites': '-'}
        self.assertEqual(dict(zip(names, found)), expected)
        for database in expected.values():
            if database != '-':
                self.assertIn(database, databases)
        patch = (ROOT / 'tools/apply-port-patches.py').read_text()
        self.assertIn('patches/series, 0118: a playlist named otherwise', patch)


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
