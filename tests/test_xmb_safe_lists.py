"""Execute patched list/node/menu functions with deterministic allocation failures.

GPU/animation and callback binding are stubs; this checks ownership, publication,
retry suppression, large lists, and recovery, not console rendering.
"""
import ast
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('xmb_probe', ROOT / 'tools/probe-xmb-allocations.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


def patched(name):
    source = (ROOT / 'vendor/retroarch' / name).read_text()
    tree = ast.parse((ROOT / 'tools/apply-port-patches.py').read_text())
    edits = next(ast.literal_eval(n.value) for n in tree.body
                 if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'EDITS' for t in n.targets))
    for file, anchor, replacement, marker in edits:
        if file == name and marker not in source:
            if anchor not in source:
                raise AssertionError(marker)
            source = source.replace(anchor, replacement, 1)
    return source


class SafeXmbLists(unittest.TestCase):
    def test_fail_each_allocation_and_recover(self):
        xmb = patched('menu/drivers/xmb.c')
        lists = patched('libretro-common/lists/file_list.c')
        menu = patched('menu/menu_driver.c')
        start = xmb.index('typedef struct\n{\n   /* patches/series, 0080: visible icon paths only */')
        end = xmb.index('} xmb_node_t;', start) + len('} xmb_node_t;')
        functions = [(lists, name) for name in ('file_list_reserve', 'file_list_append', 'file_list_insert',
                     'file_list_clear', 'file_list_get_userdata_at_offset', 'file_list_free_actiondata')]
        functions += [(xmb, name) for name in ('xmb_alloc_node', 'xmb_free_node', 'xmb_free_list_nodes',
                      'xmb_copy_node', 'xmb_list_clear', 'xmb_list_deep_copy', 'xmb_list_insert')]
        functions += [(menu, name) for name in ('menu_entries_append', 'menu_entries_prepend')]
        bodies = '\n'.join(probe.function(s, n) for s, n in functions)
        preamble = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "memory_xmb.h"
#include "menu/menu_driver.h"
#include "gfx/gfx_thumbnail.h"
#include <string/stdstring.h>
typedef union {max_align_t alignment; size_t bytes;} Header;
static size_t live, peak, calls, fail_at, errors;
static void *checked_malloc(size_t n) {
    if (++calls == fail_at) return NULL;
    Header *h = malloc(sizeof(*h) + n); assert(h); h->bytes=n;
    live += n; if(live>peak) peak=live; return h+1;
}
static void checked_free(void *p) {
    if(!p) return; Header *h=(Header*)p-1; assert(live>=h->bytes);
    live-=h->bytes; free(h);
}
static void *checked_realloc(void *p,size_t n) {
    void *q=checked_malloc(n); if(!q) return NULL;
    if(p) {size_t old=((Header*)p-1)->bytes; memcpy(q,p,old<n?old:n); checked_free(p);} return q;
}
static char *checked_strdup(const char *s) {
    size_t n=strlen(s)+1; void *p=checked_malloc(n); return p?memcpy(p,s,n):NULL;
}
bool gfx_animation_kill_by_tag(uintptr_t *tag) {(void)tag;return true;}
void gfx_thumbnail_reset(gfx_thumbnail_t *t) {assert(!t->texture);}
static struct menu_state menu_driver_state;
struct menu_state *menu_state_get_ptr(void) {return &menu_driver_state;}
typedef struct { float items_passive_alpha,items_passive_zoom,items_active_alpha; } xmb_handle_t;
static float xmb_item_y(xmb_handle_t *xmb, int i, int current) {return (float)(i-current);}
#define ps5_xmb_observe(...) ((void)0)
#define malloc checked_malloc
#define ps5_menu_malloc checked_malloc
#define realloc checked_realloc
#define free checked_free
#define strdup checked_strdup
#define menu_cbs_init(...) ((void)0)
#define menu_setting_find_enum(...) NULL
#define RARCH_ERR(...) (++errors)
'''
        experiment = r'''
static void clear(file_list_t *list) {
    xmb_free_list_nodes(list,true); file_list_clear(list);
}
static void destroy(file_list_t *list) {clear(list);free(list->list);memset(list,0,sizeof(*list));}
static bool append(file_list_t *list) {
    return menu_entries_append(list,"content","entry",MSG_UNKNOWN,0,0,0,NULL);
}
int main(void) {
    _Static_assert(sizeof(xmb_node_t)==96,"compact node");
    _Static_assert(sizeof(menu_file_list_cbs_t)==648,"callback ABI");
    xmb_handle_t xmb={0};
    menu_ctx_driver_t driver={0}; driver.ident="xmb"; driver.list_insert=xmb_list_insert;
    menu_driver_state.driver_ctx=&driver; menu_driver_state.userdata=&xmb;
    struct item_file stack_item={0};stack_item.path="parent";
    file_list_t stack={0};stack.list=&stack_item;stack.size=1;
    file_list_t *stacks[]={&stack};menu_list_t menus={0};menus.menu_stack=stacks;
    menu_driver_state.entries.list=&menus;
    file_list_t list={0}, copy={0};
    for(size_t i=0;i<7384;i++) assert(append(&list));
    assert(list.size==7384 && peak<7000000);
    xmb_list_deep_copy(&list,&copy,0,7); assert(copy.size==8);
    clear(&copy); xmb_list_deep_copy(&copy,&list,0,0); assert(list.size==0);
    destroy(&list);destroy(&copy);assert(live==0);
    // Every allocation in a new entry and in prepend fails in turn. No half entry.
    for(int prepend=0;prepend<2;prepend++) for(size_t fault=1;fault<=8;fault++) {
        assert(append(&list));size_t before=list.size;
        calls=0;fail_at=fault;
        if(prepend) menu_entries_prepend(&list,"p","l",MSG_UNKNOWN,0,0,0);
        else append(&list);
        fail_at=0;
        if(list.allocation_failed) {
            assert(list.size==before);size_t attempts=calls;
            for(int j=0;j<100;j++) assert(!append(&list));
            assert(calls==attempts);
        } else assert(list.size==before+1);
        for(size_t j=0;j<list.size;j++) assert(list.list[j].userdata && list.list[j].actiondata);
        clear(&list);assert(append(&list));destroy(&list);assert(live==0);
    }
    // Growth failure on an empty list must not underflow size-1.
    for(size_t fault=1;fault<=8;fault++) {
        calls=0;fail_at=fault;append(&list);fail_at=0;
        assert(list.size<=1);destroy(&list);assert(live==0);
    }
    // Deep copy checks reserve, all three strings, nodes, names, paths and callbacks.
    assert(append(&list));
    list.list[0].alt=strdup("alternate");
    xmb_node_t *node=list.list[0].userdata;
    node->console_name=strdup("system");
    node->thumbnail_icon.thumbnail_path_data=malloc(sizeof(gfx_thumbnail_path_data_t));
    memset(node->thumbnail_icon.thumbnail_path_data,0,sizeof(gfx_thumbnail_path_data_t));
    size_t source_live=live;
    for(size_t fault=1;fault<=12;fault++) {
        calls=0;fail_at=fault;
        xmb_list_deep_copy(&list,&copy,0,10);fail_at=0;
        assert(copy.size<=1 && list.size==1 && !strcmp(node->fullpath,"parent"));
        if(copy.size) {
            xmb_node_t *cloned=copy.list[0].userdata;
            assert(cloned!=node && cloned->thumbnail_icon.thumbnail_path_data!=node->thumbnail_icon.thumbnail_path_data);
        }
        destroy(&copy);assert(live==source_live);
    }
    destroy(&list);assert(live==0);
    puts("PASS: 7384 entries; compact nodes; each allocation failure; rollback; copy ownership; retry latch; recovery; zero leaks");
}
'''
        with tempfile.TemporaryDirectory() as directory:
            d = Path(directory)
            (d / 'lists').mkdir()
            (d / 'lists/file_list.h').write_text(patched('libretro-common/include/lists/file_list.h'))
            source = d / 'test.c'
            source.write_text(preamble + xmb[start:end] + bodies + experiment)
            binary = d / 'test'
            subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined',
                            '-I'+str(d), '-I'+str(ROOT/'src'), '-I'+str(ROOT/'vendor/retroarch'),
                            '-I'+str(ROOT/'vendor/retroarch/libretro-common/include'),
                            str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, env={**os.environ, 'ASAN_OPTIONS':'detect_leaks=0',
                                                          'UBSAN_OPTIONS':'halt_on_error=1'})
