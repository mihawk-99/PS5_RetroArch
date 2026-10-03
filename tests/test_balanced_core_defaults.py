"""Run the real option parsers: defaults, saved overrides, reset and API versions."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from tests.test_xmb_safe_lists import ROOT, patched


class BalancedCoreDefaults(unittest.TestCase):
    def test_option_loading_and_reset(self):
        source = patched('core_option_manager.c')
        fixture = r'''
#include <assert.h>
/* Model the entry lookup only; parsing, value matching and reset are upstream. */
static struct config_entry_list saved;
struct config_entry_list *config_get_entry(const config_file_t *conf, const char *key)
{
   return conf ? &saved : NULL;
}
const char *msg_hash_to_str(enum msg_hash_enums msg) { return ""; }

static void check(const char *key, const char *old, const char *balanced,
                  const char *saved_value, bool legacy, bool available)
{
   struct core_option option = {0};
   core_option_manager_t manager = {0};
   struct retro_core_option_v2_definition def = {0};
   struct retro_variable var;
   char values[512];
   manager.opts = &option;
   manager.size = 1;
   def.key = key;
   def.default_value = old;
   def.values[0].value = "unrelated";
   def.values[1].value = old;
   def.values[2].value = available ? balanced : "unsupported";
   saved.value = (char *)saved_value;
   /* Exercise global/per-core config and the source config used for overrides. */
   manager.conf = saved_value && !legacy ? (config_file_t *)&saved : NULL;
   if (legacy)
   {
      snprintf(values, sizeof(values), "Resolution; %s|unrelated|%s", old,
            available ? balanced : "unsupported");
      var.key = key;
      var.value = values;
      assert(core_option_manager_parse_variable(&manager, 0, &var,
            saved_value ? (config_file_t *)&saved : NULL));
   }
   else
      assert(core_option_manager_parse_option(&manager, 0, &def, NULL));
   const char *expected = available ? balanced : old;
   assert(!strcmp(option.vals->elems[option.default_index].data, expected));
   if (saved_value && (!strcmp(saved_value, old) || !strcmp(saved_value, balanced)))
      expected = saved_value;
   assert(!strcmp(option.vals->elems[option.index].data, expected));
   core_option_manager_set_default(&manager, 0, false);
   assert(option.index == option.default_index && manager.updated);
   free(option.key);
   free(option.desc);
   string_list_free(option.vals);
   string_list_free(option.val_labels);
}

int main(void)
{
   const char *profiles[][3] = {
      {"ppsspp_internal_resolution", "4800x2720", "2880x1632"},
      {"ppsspp_mulitsample_level", "Disabled", "x8"},
      {"dolphin_efb_scale", "6", "4"},
      {"pcsx2_upscale_multiplier", "6x Native (~2160p/4K)", "4x Native (~1440p/2K)"},
      {"beetle_psx_hw_internal_resolution", "16x", "8x"},
      {"beetle_psx_hw_msaa", "8x", "1x"},
      {"citra_resolution_factor", "18", "6"},
      {"desmume_internal_resolution", "1280x960", "1024x768"},
   };
   for (size_t i = 0; i < sizeof(profiles) / sizeof(profiles[0]); ++i)
      for (int legacy = 0; legacy < 2; ++legacy)
      {
         const char **p = profiles[i];
         check(p[0], p[1], p[2], NULL, legacy, true);
         check(p[0], p[1], p[2], p[1], legacy, true);
         check(p[0], p[1], p[2], p[2], legacy, true);
         check(p[0], p[1], p[2], "invalid saved value", legacy, true);
         check(p[0], p[1], p[2], NULL, legacy, false);
      }
   /* Unknown options retain their own default, not the first available value. */
   check("unrelated_core_resolution", "native", "native", NULL, false, true);
   /* v1 definitions are converted before going through the same v2 parser. */
   struct retro_core_option_definition v1[2] = {0};
   v1[0].key = "ppsspp_internal_resolution";
   v1[0].default_value = "4800x2720";
   v1[0].values[0].value = "4800x2720";
   v1[0].values[1].value = "2880x1632";
   struct retro_core_options_v2 *v2 = core_option_manager_convert_v1(v1);
   assert(v2 && !strcmp(v2->definitions[0].key, v1[0].key));
   struct core_option option = {0};
   core_option_manager_t manager = {0};
   manager.opts = &option;
   assert(core_option_manager_parse_option(&manager, 0, v2->definitions, NULL));
   assert(option.default_index == 1 && option.index == 1);
   free(option.key);
   string_list_free(option.vals);
   string_list_free(option.val_labels);
   core_option_manager_free_converted(v2);
   return 0;
}
'''
        with tempfile.TemporaryDirectory() as td:
            path = Path(td, 'options.c')
            path.write_text(source + fixture)
            binary = Path(td, 'options')
            common = ROOT / 'vendor/retroarch/libretro-common'
            subprocess.run(['cc', '-std=gnu11', '-O1', '-ffunction-sections',
                            '-fdata-sections', '-I' + str(ROOT / 'vendor/retroarch'),
                            '-I' + str(common / 'include'), str(path),
                            str(common / 'lists/string_list.c'),
                            str(common / 'string/stdstring.c'),
                            '-Wl,--gc-sections', '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=15)
