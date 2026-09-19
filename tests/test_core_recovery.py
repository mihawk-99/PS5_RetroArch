"""Run actual upstream core-selection code against refused/successful loads."""
import importlib.util
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class CoreRecovery(unittest.TestCase):
    def test_refused_selection_never_changes_running_core_type(self):
        source = (ROOT / 'vendor/retroarch/tasks/task_content.c').read_text()
        spec = importlib.util.spec_from_file_location('patches', ROOT / 'tools/apply-port-patches.py')
        patches = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patches)
        patched = source
        for path, anchor, replacement, marker in patches.EDITS:
            if path == 'tasks/task_content.c' and marker not in patched:
                self.assertIn(anchor, patched)
                patched = patched.replace(anchor, replacement, 1)
        fixture = r'''
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define HAVE_DYNAMIC 1
#define RARCH_PATH_CORE 0
#define RARCH_PATH_CORE_LAST 1
#define CMD_EVENT_LOAD_CORE 2
enum rarch_core_type { CORE_TYPE_DUMMY, CORE_TYPE_PLAIN };
typedef void content_ctx_info_t;
typedef void (*retro_task_callback_t)(void);
static const char *paths[2] = {"", ""};
static bool accepted;
static int loads, changes;
static void path_set(int key, const char *path) { paths[key] = path; }
static const char *path_get(int key) { return paths[key]; }
static bool string_is_equal(const char *a, const char *b) { return !strcmp(a,b); }
static bool command_event(int command, void *data) { ++loads; return accepted; }
static void runloop_set_current_core_type(enum rarch_core_type type, bool explicit_type) { ++changes; }
'''
        main = r'''
int main(void) {
    accepted = false;
    if (task_push_load_new_core("bad.so", "", NULL, CORE_TYPE_PLAIN, NULL, NULL)) return 1;
    if (loads != 1 || changes != 0) return 2;
    accepted = true;
    if (!task_push_load_new_core("good.so", "", NULL, CORE_TYPE_PLAIN, NULL, NULL)) return 3;
    return loads == 2 && changes == 1 ? 0 : 4;
}
'''
        with tempfile.TemporaryDirectory() as td:
            for name, text, expected in [('before', source, 1), ('after', patched, 0)]:
                body = re.search(r'bool task_push_load_new_core\(.*?\n\}', text, re.S)[0]
                path = Path(td) / (name + '.c')
                path.write_text(fixture + body + main)
                binary = Path(td) / name
                subprocess.run(['cc', '-std=c99', str(path), '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)], check=False)
                self.assertEqual(result.returncode, expected)
