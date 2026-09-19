"""RGUI's producer and the port's upload must agree on RGBA channel order."""

import ast
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class VulkanMenuColor(unittest.TestCase):
    def test_all_rgba4444_values_reach_the_matching_rgba8888_bytes(self):
        script = ast.parse((ROOT / "tools/apply-port-patches.py").read_text())
        edits = next(ast.literal_eval(node.value) for node in script.body
                     if isinstance(node, ast.Assign)
                     and getattr(node.targets[0], "id", "") == "EDITS")
        source = (ROOT / "vendor/retroarch/gfx/drivers/vulkan.c").read_text()
        for name, anchor, replacement, marker in edits:
            if name == "gfx/drivers/vulkan.c" and marker not in source:
                self.assertIn(anchor, source)
                source = source.replace(anchor, replacement, 1)
        upload = source.split("static void vulkan_set_texture_frame(", 1)[1]
        expression = re.search(r"\n\s+\*dstpix\s*=\s*(.*?);", upload, re.S).group(1)
        rgui = (ROOT / "vendor/retroarch/menu/drivers/rgui.c").read_text()
        producer = re.search(r"static uint16_t argb32_to_rgba4444\(uint32_t col\)\s*"
                             r"\{(.*?)\n\}", rgui, re.S).group(1)
        program = r'''
#include <stdint.h>
#include <stdio.h>
static uint16_t produce(uint32_t col) { PRODUCER }
static uint32_t convert(uint32_t pix) { return EXPRESSION; }
int main(void)
{
    for (unsigned rgba = 0; rgba < 65536; ++rgba)
    {
        unsigned r = ((rgba >> 12) & 15) * 17;
        unsigned g = ((rgba >> 8) & 15) * 17;
        unsigned b = ((rgba >> 4) & 15) * 17;
        unsigned a = (rgba & 15) * 17;
        uint16_t packed = produce((a << 24) | (r << 16) | (g << 8) | b);
        uint32_t got = convert(packed);
        uint32_t want = r | (g << 8) | (b << 16) | (a << 24);
        if (got != want)
        {
            fprintf(stderr, "RGBA4444=%04x got=%08x want=%08x\n", packed, got, want);
            return 1;
        }
    }
    puts("65536 RGBA4444 colours match RGBA8888 byte order and full-range expansion");
}
'''.replace("PRODUCER", producer).replace("EXPRESSION", expression)
        compiler = shutil.which("clang") or shutil.which("gcc")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            source_file = Path(directory) / "colour.c"
            binary = Path(directory) / "colour"
            source_file.write_text(program)
            subprocess.run([compiler, "-std=c11", str(source_file), "-o", str(binary)],
                           check=True, capture_output=True, text=True)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
