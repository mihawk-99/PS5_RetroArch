#!/usr/bin/env python3
"""Build WebUI catalogs from the pinned core option tables (no game required).

Preprocessing uses the PS5 core's feature defines. Only option declarations are
compiled by the host helper; no emulator code or static constructors execute.
"""
from pathlib import Path
import ast
import hashlib
import json
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
# Core filename stem, libretro library name, source, extra preprocessing defines.
CORES = [
    ('ppsspp', 'PPSSPP', '.deps/ppsspp-src/libretro/libretro_core_options.h', []),
    ('desmume', 'DeSmuME', '.deps/desmume-src/desmume/src/frontend/libretro/libretro_core_options.h', ['HAVE_JIT']),
    ('mednafen_psx_hw', 'Beetle PSX HW', '.deps/beetle-psx-src/libretro_core_options.h', ['HAVE_HW','HAVE_VULKAN']),
    ('mednafen_saturn', 'Beetle Saturn', '.deps/beetle-saturn-src/libretro_core_options.h', []),
    ('pcsx2', 'LRPS2', '.deps/lrps2-src/libretro/libretro_core_options.h', []),
    ('mame', 'MAME', '.deps/mame-src/src/osd/libretro/libretro-internal/libretro_core_options.h', ['CORE_NAME="mame"']),
    ('mupen64plus_next', 'Mupen64Plus-Next', '.deps/mupen64plus-src/libretro/libretro_core_options.h', ['CORE_NAME="mupen64plus"','HAVE_PARALLEL_RDP','HAVE_PARALLEL_RSP','HAVE_THR_AL','HAVE_LLE','DYNAREC']),
    ('fceumm', 'FCEUmm', 'build/cores/fceumm/src/drivers/libretro/libretro_core_options.h', ['HAVE_NTSC_FILTER','HAVE_HDPACK']),
    ('genesis_plus_gx', 'Genesis Plus GX', 'build/cores/genesis_plus_gx/libretro/libretro_core_options.h', ['M68K_OVERCLOCK_SHIFT=20','Z80_OVERCLOCK_SHIFT=20','HAVE_YM3438_CORE','HAVE_OPLL_CORE']),
    ('snes9x', 'Snes9x', 'build/cores/snes9x/libretro/libretro_core_options.h', []),
    ('mgba', 'mGBA', 'build/cores/mgba/src/platform/libretro/libretro_core_options.h', []),
    ('dolphin', 'dolphin-emu', '.deps/dolphin-src/Source/Core/DolphinLibretro/Common/Options.cpp', ['_M_X86_64']),
    ('azahar', 'Azahar', '.deps/azahar-src/src/citra_libretro/core_settings.cpp', ['ENABLE_VULKAN']),
    ('vice_x64sc', 'VICE x64sc', '.deps/vice-src/libretro/libretro-core.c', ['__X64SC__','HAVE_RESID33','ARCHDEP_PRINTER_DEFAULT_DEV1="vice_printer.txt"']),
    ('fbneo', 'FinalBurn Neo', 'build/cores/fbneo/src/burner/libretro/retro_common.cpp', ['BUILD_NEOGEO']),
    # The cores added 2026-10-06; library names as each core reports them (its settings'
    # folder), tables found by name in each source (option_tables).
    ('a5200', 'a5200', '.deps/a5200-src/libretro/libretro_core_options.h', []),
    ('mednafen_ngp', 'Beetle NeoPop', '.deps/beetle-ngp-src/libretro_core_options.h', []),
    ('mednafen_pce', 'Beetle PCE', '.deps/beetle-pce-src/libretro_core_options.h', []),
    ('mednafen_pcfx', 'Beetle PC-FX', '.deps/beetle-pcfx-src/libretro_core_options.h', []),
    ('mednafen_vb', 'Beetle VB', '.deps/beetle-vb-src/libretro_core_options.h', []),
    ('mednafen_wswan', 'Beetle WonderSwan', '.deps/beetle-wswan-src/libretro_core_options.h', []),
    ('dosbox_pure', 'DOSBox-pure', '.deps/dosbox-pure-src/core_options.h', ['DBP_DEFAULT_SAMPLERATE_STRING="48000"']),
    ('flycast', 'Flycast', '.deps/flycast-src/shell/libretro/libretro_core_options.h', ['CORE_OPTION_NAME="reicast"']),
    ('handy', 'Handy', '.deps/handy-src/libretro/libretro_core_options.h', []),
    ('neocd', 'NeoCD', '.deps/neocd-src/src/libretro_variables.cpp', []),
    ('opera', 'Opera', '.deps/opera-src/libretro_core_options.c', ['THREADED_DSP=1']),  # its Makefile's default, kept for ps5
    ('picodrive', 'PicoDrive', '.deps/picodrive-src/platform/libretro/libretro_core_options.h', []),
    ('pokemini', 'PokeMini', '.deps/pokemini-src/libretro/libretro_core_options.h', []),
    ('prosystem', 'ProSystem', '.deps/prosystem-src/core/libretro_core_options.h', []),
    ('puae', 'PUAE', '.deps/puae-src/libretro/libretro-core.c', []),
    ('scummvm', 'ScummVM', '.deps/scummvm-src/backends/platform/libretro/include/libretro-core-options.h', ['USE_HIGHRES']),  # Makefile.common's default, kept for ps5
    ('stella', 'Stella', '.deps/stella-src/src/os/libretro/libretro.cxx', []),
    ('virtualjaguar', 'Virtual Jaguar', '.deps/virtualjaguar-src/libretro_core_options.h', []),
]
# Cores whose tables are found by name (the English ones), not named in extract().
NAMED = {'fbneo', 'dolphin', 'azahar', 'fceumm', 'ppsspp', 'desmume', 'mednafen_psx_hw',
         'mednafen_saturn', 'pcsx2', 'mame', 'mupen64plus_next', 'genesis_plus_gx', 'snes9x',
         'mgba', 'vice_x64sc'}

def blocks(text, opening):
    """Each block a pattern opens, to its matching brace (strings skipped)."""
    out = []
    for match in re.finditer(opening, text, re.M):
        depth = 1
        for token in re.finditer(r'"(?:\\.|[^"\\])*"|[{}]', text[match.end():]):
            depth += 1 if token[0] == '{' else -1 if token[0] == '}' else 0
            if not depth:
                out.append(text[match.start():match.end() + token.end()])
                break
    return out

def option_tables(text):
    """The English category and definition tables of a source, and whether they are v1."""
    v2 = re.findall(r'retro_core_option_v2_definition\s+(\w+)\s*\[', text)
    v1 = re.findall(r'retro_core_option_definition\s+(\w+)\s*\[', text)
    cats = re.findall(r'retro_core_option_v2_category\s+(\w+)\s*\[', text)
    pick = lambda names: next((n for n in names if n in ('option_defs_us_v2', 'option_defs_us', 'option_defs')), names[0] if names else None)
    defs = pick(v2)
    if defs:
        return next((c for c in cats if c in ('option_cats_us', 'option_cats')), cats[0] if cats else None), defs, False
    return None, pick(v1), True

def declaration(text, name):
    match = re.search(r'(?:static\s+)?(?:constexpr\s+|const\s+)?(?:struct\s+)?retro_core_option_(?:v2_category|v2_definition|definition)\s+'+re.escape(name)+r'\s*(?:\[[^\]]*\])?\s*=\s*\{', text)
    if not match:
        raise ValueError('Option declaration missing: '+name)
    # Tokenize strings so braces in help text cannot terminate an initializer.
    depth = 1
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|[{}]', text[match.end():]):
        if token[0] == '{': depth += 1
        elif token[0] == '}': depth -= 1
        if not depth:
            return text[match.start():match.end()+token.end()]+';'
    raise ValueError('Unclosed option declaration: '+name)

def preprocess(text, flags):
    text = re.sub(r'^\s*#\s*include[^\n]*', '', text, flags=re.M)
    return subprocess.check_output(['c++','-E','-P','-x','c++','-DHAVE_NO_LANGEXTRA','-D__PROSPERO__', *['-D'+v for v in flags], '-'], input=text, text=True, cwd=ROOT, stderr=subprocess.PIPE)

def neocd_tables(source):
    """NeoCD builds its table at run time (its BIOS choices are the files found): its
    builder, compiled here with no BIOS, gives every other option; the BIOS one is the
    console's, from the runtime snapshot once the core runs."""
    text = source.read_text()
    names = text.split('// Variable names and descriptions for the settings', 1)[1].split('// All core variables', 1)[0]
    cats = re.search(r'static retro_core_option_v2_category coreOptionCategories\[\] = \{.*?\};', text, re.S)[0]
    builders = ''.join(re.search(r'static void '+name+r'\(.*?\n\}\n', text, re.S)[0]
                       for name in ('fillBasicOption', 'fillBiosOption', 'buildCoreOptionsV2'))
    stub = ('#include <cstring>\n#include <string>\n#include <vector>\n'
            'struct BiosEntry { std::string description; };\n'
            'static struct { std::vector<BiosEntry> biosList; std::string biosChoices; } globals;\n'
            'static std::vector<retro_core_option_v2_definition> coreOptionDefinitions;\n'
            'static struct { retro_core_option_v2_category *categories; '
            'retro_core_option_v2_definition *definitions; } coreOptionsV2;\n')
    tail = ('static const retro_core_option_v2_definition *neocd_defs() '
            '{ buildCoreOptionsV2(); return coreOptionDefinitions.data(); }\n')
    return stub+names+cats+'\n'+builders+tail, 'coreOptionCategories', 'neocd_defs()'

def stella_data(source):
    """Stella declares legacy variables, "Label; first|second": no descriptions, and the
    first choice is the default (libretro's rule for them)."""
    settings = []
    for key, value in re.findall(r'\{\s*"(stella_\w+)",\s*"([^"]+)"\s*\}', source.read_text()):
        label, choices = value.split(';', 1)
        values = [c.strip() for c in choices.split('|')]
        settings.append({'key': key, 'label': label.strip(), 'description': '', 'category': '',
                         'default': values[0], 'choices': [[v, v] for v in values]})
    return {'categories': [], 'settings': settings}

def extract(stem, source, flags):
    if stem == 'neocd':
        return neocd_tables(source)
    text = source.read_text()
    if stem == 'vice_x64sc': text = (source.parent/'libretro-core.h').read_text()+'\n'+text
    if stem == 'puae': text = (source.parent/'libretro-core.h').read_text()+'\n'+text
    if stem == 'fbneo':
        text = (source.parent/'retro_common.h').read_text()+'\n'+text
        strings = re.findall(r'"(?:\\.|[^"\\])*"', (source.parent/'retro_string.cpp').read_text().split('= {', 1)[1])[:171]
        macros = (source.parent/'retro_string.h').read_text()
        translations = {key: strings[int(index)] for key, index in re.findall(r'#define (RETRO_\w+)\s+pSelLangStr\[\s*(\d+)\]', macros)}
        text = re.sub(r'RETRO_\w+', lambda m: translations.get(m[0], m[0]), text)
    if stem == 'mednafen_psx_hw': text = (source.parent/'libretro_options.h').read_text()+'\n'+text
    if stem == 'dolphin':
        # Every option key is a literal constexpr array in the adjacent header.
        keys = dict(re.findall(r'constexpr const char (\w+)\[\] = ("[^"\n]+");', (source.parent/'Options.h').read_text()))
        text = '\n'.join(re.findall(r'^#define MODIFIER_.*$', (source.parent/'Options.h').read_text(), re.M))+'\n'+text
        text = re.sub(r'Libretro::Options::\w+::(\w+)', lambda m: keys[m[1]], text)
    if stem == 'azahar':
        text = re.sub(r'config::category::(\w+)', r'"\1"', text)
        text = re.sub(r'config::(?:cpu|system|audio|graphics|layout|storage|input)::(\w+)', r'"citra_\1"', text)
        text = text.replace('config::enabled','"enabled"').replace('config::disabled','"disabled"')
    text = preprocess(text, flags)
    cats, defs = 'option_cats_us', 'option_defs_us'
    if stem == 'fceumm': defs = 'option_defs'
    if stem == 'dolphin': cats, defs = 'option_cats', 'option_defs'
    if stem == 'azahar': cats, defs = 'option_categories', 'option_definitions'
    prefix = ''
    if stem == 'dolphin':
        prefix = '\n'.join(re.findall(r'static constexpr const char\* CATEGORY_\w+\s*=\s*"[^"]+";', text))
    if stem == 'fbneo':
        names = re.findall(r'retro_core_option_v2_definition (var_fbneo_\w+)\s*=\s*\{', text)
        prefix = '\n'.join(declaration(text, name) for name in names)
        # General definitions are already English; game-specific DIP switches
        # and cheats are appended by the runtime snapshot when a game loads.
        return prefix+'\n'+declaration(text, cats)+'\nretro_core_option_v2_definition option_defs_us[] = {'+','.join(names)+', {}};', cats, defs
    if stem not in NAMED:
        cats, defs, v1 = option_tables(text)
        if not defs:
            raise ValueError(stem+': no option table found')
        # A table sized or keyed by the source's own enums and namespaces
        # (DOSBox Pure's DBP_Option, DBP_OptionCat): those kept whole.
        prefix = '\n'.join(re.findall(r'^\s*enum\b[^{;]*\{[^}]*\};', text, re.M | re.S) +
                           blocks(text, r'^\s*namespace\s+\w+\s*\{'))
        if stem == 'dosbox_pure':
            prefix = 'class Section;\n' + prefix  # declared by DBP_Option's helpers, never used here
        body = (declaration(text, cats)+'\n' if cats else '')+declaration(text, defs)
        return prefix+'\n'+body, (cats or 'nullptr') if not v1 else 'nullptr', defs
    return prefix+'\n'+declaration(text, cats)+'\n'+declaration(text, defs), cats, defs


def prepare(stem, source, data):
    # Mirror the runtime-populated VICE key lists; cartridges depend on the
    # user's BIOS folder and are added by registration on the console.
    if stem == 'vice_x64sc':
        mapper = preprocess((source.parent/'libretro-mapper.h').read_text(), ['__X64SC__'])
        table = mapper.split('static retro_keymap retro_keys', 1)[1].split('};', 1)[0]
        keys = re.findall(r'\{\s*([^,]+),\s*("(?:\\.|[^"\\])*"),\s*("(?:\\.|[^"\\])*")\s*\}', table)
        choices = [[ast.literal_eval(value), ast.literal_eval(label)] for _, value, label in keys]
        hotkeys = set(re.findall(r'strstr\(option_defs_us\[i\].key, "(vice_mapper_[^"]+)"\)', source.read_text()))
        for option in data['settings']:
            if option['key'].startswith('vice_mapper_'):
                option['choices'] = [pair for (key, _, _), pair in zip(keys, choices) if option['key'] not in hotkeys or key.strip() == '0' or key.strip().startswith('RETROK_')]
            elif option['key'] == 'vice_cartridge':
                option['choices'] = [['none', 'Off (no cartridge)']]
    overrides = {}
    if stem == 'ppsspp':
        profile = (source.parent/'libretro.cpp').read_text().split('static void ps5_apply_default_profile()', 1)[1].split('};', 1)[0]
        overrides.update(re.findall(r'\{\s*"([^"]+)",\s*"([^"]+)"\s*\}', profile))
    if stem == 'dolphin':
        profile = source.read_text().split('static void Ps5ApplyDefaultProfile()', 1)[1].split('};', 1)[0]
        keys = dict(re.findall(r'constexpr const char (\w+)\[\] = "([^"\n]+)";', (source.parent/'Options.h').read_text()))
        overrides.update((keys[key], value) for key, value in re.findall(r'\{Libretro::Options::\w+::(\w+), "([^"]+)"\}', profile))
    # Read the patched frontend's authoritative overrides, after core defaults.
    frontend = (ROOT/'build/ra-conf/core_option_manager.c').read_text()
    profile = frontend.split('static void ps5_core_option_default', 1)[1].split('};', 1)[0]
    overrides.update(re.findall(r'\{\s*"([^"]+)",\s*"([^"]+)"\s*\}', profile))
    keys = [s['key'] for s in data['settings']]
    assert keys and len(keys) == len(set(keys)), (stem, 'missing or duplicate options')
    categories = {c['key'] for c in data['categories']}
    for option in data['settings']:
        assert not option['category'] or option['category'] in categories, (stem, option['key'])
        assert re.fullmatch(r'[A-Za-z0-9_.-]+', option['key']), option['key']
        values = [c[0] for c in option['choices']]
        value = overrides.get(option['key'], option['default'])
        # libretro uses the first choice when the declared default is absent.
        option['default'] = value if value in values else values[0] if values else ''
    return data

def compile_tables(stem, source, flags, helper):
    """A core's option tables, compiled with the host helper and dumped as JSON."""
    tables, cats, defs = extract(stem, source, flags)
    with tempfile.TemporaryDirectory() as folder:
        cpp = Path(folder)/'options.cpp'; exe = Path(folder)/'options'
        cpp.write_text(helper+'\n'+tables+f'\nint main() {{ dump({cats}, {defs}); }}\n')
        result = subprocess.run(['c++', '-std=c++17', '-I'+str(ROOT/'vendor/retroarch/libretro-common/include'),
                                 str(cpp), '-o', str(exe)], capture_output=True, text=True)
        if result.returncode:
            (ROOT/'build/webui-catalog-error.cpp').write_text(cpp.read_text())
            raise RuntimeError(stem+': '+result.stderr[:4000])
        return json.loads(subprocess.check_output([str(exe)], text=True))

def generate(output):
    output.mkdir(parents=True, exist_ok=True)
    helper = (ROOT/'tooling/webui/dump-options.cpp').read_text()
    records=[]
    for stem,name,source,flags in CORES:
        source=ROOT/source
        data = stella_data(source) if stem == 'stella' else compile_tables(stem, source, flags, helper)
        data = prepare(stem, source, data)
        data['core']=name
        data['binary_sha256'] = hashlib.sha256((ROOT/'build/cores/stage/cores'/(stem+'_libretro.so')).read_bytes()).hexdigest()
        (output/(name+'.opt')).write_text(''.join(s['key']+' = '+json.dumps(s['default'])+'\n' for s in data['settings']))
        (output/(name+'.json')).write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n')
        print(stem,len(data['settings']),len(data['categories']),flush=True)
        records.append({'file':stem+'_libretro.so','name':name,'source':str(source.relative_to(ROOT)),'sha256':hashlib.sha256(source.read_bytes()).hexdigest()})
    (output/'index.cfg').write_text(''.join(r['file']+' = '+json.dumps(r['name'])+'\n' for r in records))
    (output/'sources.json').write_text(json.dumps(records,indent=2)+'\n')

if __name__=='__main__':
    generate(Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'build/webui-core-metadata')
