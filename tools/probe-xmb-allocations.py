#!/usr/bin/env python3
"""Offline allocation experiment using the configured XMB/list source bodies.

This is a list-lifetime experiment, not a frontend or PS5 allocator simulation.
No console access. GPU thumbnail/animation boundaries are inert; test nodes
have no textures. Synthetic list lengths are not measurements of the console.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def function(source, name):
    match = re.search(r'^[\w *]+\b' + name + r'\([^;]*?\)\s*\{', source, re.M)
    if not match:
        raise ValueError(f'missing function: {name}')
    # These selected functions contain no braces in string literals/comments.
    start = source.index('{', match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'build/ra-conf')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--expect', type=Path, help='compare the entire result with recorded evidence')
    args = parser.parse_args()
    root = args.source.resolve()
    xmb_path = root / 'menu/drivers/xmb.c'
    list_path = root / 'libretro-common/lists/file_list.c'
    xmb = xmb_path.read_text()
    lists = list_path.read_text()
    types_start = xmb.index('typedef struct\n{\n   gfx_thumbnail_path_data_t thumbnail_path_data;')
    types_end = xmb.index('} xmb_node_t;', types_start) + len('} xmb_node_t;')
    functions = [(lists, name) for name in (
        'file_list_reserve', 'file_list_append', 'file_list_clear',
        'file_list_get_userdata_at_offset', 'file_list_free_actiondata')]
    functions += [(xmb, name) for name in (
        'xmb_alloc_node', 'xmb_free_node', 'xmb_free_list_nodes',
        'xmb_copy_node', 'xmb_list_clear', 'xmb_list_deep_copy')]
    bodies = '\n'.join(function(source, name) for source, name in functions)
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

typedef union { max_align_t alignment; size_t size; } Header;
static size_t live, peak, allocs;
static void *probe_malloc(size_t n) {
    Header *h = malloc(sizeof(*h) + n);
    assert(h);
    h->size = n; live += n; ++allocs;
    if (live > peak) peak = live;
    return h + 1;
}
static void probe_free(void *p) {
    if (!p) return;
    Header *h = (Header *)p - 1;
    assert(live >= h->size);
    live -= h->size; free(h);
}
static void *probe_realloc(void *p, size_t n) {
    void *q = probe_malloc(n);
    if (p) {
        size_t old = ((Header *)p - 1)->size;
        memcpy(q, p, old < n ? old : n);
        probe_free(p);
    }
    return q;
}
static char *probe_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    return memcpy(probe_malloc(n), s, n);
}
bool gfx_animation_kill_by_tag(uintptr_t *tag) { (void)tag; return true; }
void gfx_thumbnail_reset(gfx_thumbnail_t *t) { assert(!t->texture); }
#define malloc probe_malloc
#define free probe_free
#define realloc probe_realloc
#define strdup probe_strdup
'''
    experiment = r'''
static void clear_entries(file_list_t *list) {
    /* menu_entries_clear's order, with XMB as the selected driver. */
    xmb_list_clear(list);
    for (size_t i = 0; i < list->size; ++i)
        file_list_free_actiondata(list, i);
    file_list_clear(list);
}
static void populate(file_list_t *list, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        assert(file_list_append(list, "entry", "label", 0, 0, i));
        list->list[i].userdata = xmb_alloc_node();
        list->list[i].actiondata = malloc(sizeof(menu_file_list_cbs_t));
        memset(list->list[i].actiondata, 0, sizeof(menu_file_list_cbs_t));
    }
}
int main(void) {
    _Static_assert(sizeof(xmb_node_t) == 15488, "must match captured PS5 node request");
    _Static_assert(sizeof(menu_file_list_cbs_t) == 648, "must match PS5 callback size");
    file_list_t current = {0}, old = {0};
    unsigned sizes[] = {12, 20, 1, 500, 6};
    size_t warm = 0, largest = 0;
    populate(&current, 6);
    for (unsigned cycle = 0; cycle < 2000; ++cycle) {
        for (unsigned tab = 0; tab < sizeof(sizes)/sizeof(*sizes); ++tab) {
            size_t visible = current.size < 8 ? current.size : 8;
            xmb_list_deep_copy(&current, &old, 0, visible - 1);
            clear_entries(&current);
            populate(&current, sizes[tab]);
            if (live > largest) largest = live;
        }
        if (cycle == 0) warm = live;
        assert(live == warm);
    }
    clear_entries(&current); clear_entries(&old);
    free(current.list); free(old.list);
    assert(live == 0);
    printf("{\"node_bytes\":%zu,\"path_data_bytes\":%zu,"
           "\"path_chars_bytes\":%zu,\"callback_bytes\":%zu,"
           "\"switches\":10000,\"largest_live_bytes\":%zu,"
           "\"peak_requested_bytes\":%zu,\"warm_cycle_live_bytes\":%zu,"
           "\"after_cleanup_bytes\":%zu,\"allocation_calls\":%zu,",
           sizeof(xmb_node_t), sizeof(gfx_thumbnail_path_data_t),
           (size_t)(7 * PATH_MAX_LENGTH + 4 * NAME_MAX_LENGTH),
           sizeof(menu_file_list_cbs_t), largest, peak, warm, live, allocs);
    /* Independent ownership check: horizontal nodes can own console_name. */
    xmb_node_t *node = xmb_alloc_node();
    node->console_name = strdup("tab");
    char *owned_name = node->console_name;
    xmb_free_node(node);
    printf("\"console_name_unfreed_bytes\":%zu}\n", live);
    free(owned_name); /* Clean the experiment's intentionally demonstrated leak. */
    assert(live == 0);
}
'''
    with tempfile.TemporaryDirectory(prefix='xmb-probe-') as td:
        source = Path(td) / 'probe.c'
        source.write_text(preamble + xmb[types_start:types_end] + bodies + experiment)
        binary = Path(td) / 'probe'
        subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined',
                        '-I' + str(ROOT / 'src'),
                        '-I' + str(root), '-I' + str(root / 'libretro-common/include'),
                        str(source), '-o', str(binary)], check=True)
        # The desktop sandbox uses ptrace, which LeakSanitizer cannot inspect.
        # Explicit allocation accounting above checks ownership/cleanup instead.
        result = json.loads(subprocess.check_output(
            [str(binary)], text=True,
            env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0',
                 'UBSAN_OPTIONS': 'halt_on_error=1'}))
    result['kind'] = 'host source-extracted list-lifetime experiment; not console acceptance'
    result['synthetic_list_lengths'] = [12, 20, 1, 500, 6]
    result['synthetic_visible_slice_max'] = 8
    result['source_sha256'] = {
        str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in (xmb_path, list_path, root / 'gfx/gfx_thumbnail_path.h',
                  root / 'gfx/gfx_thumbnail.h', root / 'menu/menu_cbs.h')}
    text = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
    print(text, end='')
    if args.expect and result != json.loads(args.expect.read_text()):
        raise SystemExit('XMB allocation probe differs from expected evidence')


if __name__ == '__main__':
    main()
