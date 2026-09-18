#!/usr/bin/env python3
# PS5 RetroArch - the frontend's own structure, checked where a console is not
# needed.
#
#   python3 -m unittest discover -s tests -p 'test_*.py'
#
# Three things are checked here, and each is a way this project has already been
# wrong once:
#
#   1. the driver table. The port registers video_ps5 in RetroArch's
#      video_drivers[] through patches/. A build once linked and signed with the
#      patched header and the unpatched table, so the frontend compiled, started,
#      and had no ps5 entry at all. The check is a relocation in the object file,
#      which is a fact about what was built rather than about what was written.
#
#   2. the frame layout. src/display.cpp writes the console's frame through a
#      tiled addressing function, and a wrong one produces a scrambled picture
#      rather than a crash - a failure that a console run reports as "the screen
#      looks wrong", which is expensive to debug. The formula is compiled here,
#      from the source, and checked for the property that matters: it is a
#      one-to-one map onto the frame's own offsets. It is also pinned against a
#      table of values, so a change to the arithmetic cannot be silent.
#
#   3. the artifact. eboot.bin is a fake self and names the title its folder is
#      named after; both are what the console's loader reads.

from __future__ import annotations

import re
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

FRAME_WIDTH = 1920
FRAME_HEIGHT = 1080
BLOCK = 128
FRAME_BYTES = 0x1000000


def cxx() -> str:
    for candidate in ("clang++", "g++", "c++"):
        found = shutil.which(candidate)
        if found:
            return found
    raise unittest.SkipTest("no host C++ compiler found")


def tiled_offset_body() -> str:
    """The body of tiled_offset, taken from src/display.cpp.

    Compiled rather than reimplemented. A test that restates the formula proves
    only that the test agrees with itself; this one fails when the source moves.
    """
    source = (ROOT / "src" / "display.cpp").read_text(encoding="utf-8")
    match = re.search(
        r"tiled_offset\(unsigned x, unsigned y\) noexcept\s*\{(.*?)\n\}", source, re.S)
    if match is None:
        raise AssertionError("src/display.cpp no longer defines tiled_offset(x, y)")
    return match.group(1)


def build_offset_table() -> dict[tuple[int, int], int]:
    """Compile the real formula and dump it for a sample of the frame."""
    body = tiled_offset_body()
    program = f"""
#include <cstdint>
#include <cstdio>
#include <cstddef>
namespace {{
constexpr unsigned frame_width = {FRAME_WIDTH};
[[nodiscard]] constexpr std::size_t tiled_offset(unsigned x, unsigned y) noexcept
{{{body}}}
}}
int main()
{{
    /* The corners, the centre, and a stride that is coprime with the tile size so
     * the sample crosses tile and block boundaries in both directions. */
    const unsigned xs[] = {{0, 1, 4, 63, 64, 127, 128, 129, 960, 1791, 1918, 1919}};
    const unsigned ys[] = {{0, 1, 63, 64, 127, 128, 129, 540, 895, 1023, 1078, 1079}};
    for (unsigned x : xs)
        for (unsigned y : ys)
            std::printf("%u %u %zu\\n", x, y, tiled_offset(x, y));
    return 0;
}}
"""
    with tempfile.TemporaryDirectory() as work:
        directory = Path(work)
        (directory / "probe.cpp").write_text(program, encoding="utf-8")
        binary = directory / "probe"
        done = subprocess.run([cxx(), "-std=c++20", "-O1", "-o", str(binary),
                               str(directory / "probe.cpp")],
                              capture_output=True, text=True)
        if done.returncode != 0:
            raise AssertionError(f"the offset probe did not compile:\n{done.stderr}")
        output = subprocess.run([str(binary)], capture_output=True, text=True, check=True).stdout
    table = {}
    for line in output.splitlines():
        x, y, offset = line.split()
        table[(int(x), int(y))] = int(offset)
    return table


class DriverTable(unittest.TestCase):
    """video_ps5 is really in RetroArch's driver table, in the object that ships."""

    obj = ROOT / "build" / "ra" / "obj" / "gfx_video_driver.c.o"

    def setUp(self) -> None:
        if not self.obj.is_file():
            self.skipTest(f"{self.obj} is not built; run tools/build-retroarch.sh")

    def test_table_lists_ps5_first_and_null_second(self) -> None:
        done = subprocess.run(["readelf", "-r", str(self.obj)], capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stderr)
        section = re.search(
            r"Relocation section '\.rela\.data\.video_drivers' at.*?\n(.*?)\n\n",
            done.stdout, re.S)
        self.assertIsNotNone(section, "the object has no .rela.data.video_drivers section")
        targets = re.findall(r"R_X86_64_64\s+\S+\s+(\S+)", section.group(1))
        self.assertEqual(
            targets[:2], ["video_ps5", "video_null"],
            "video_drivers[] must name video_ps5 before video_null, so the frontend "
            "finds this project's driver first")

    def test_exactly_two_entries_so_the_table_stays_terminated(self) -> None:
        done = subprocess.run(["readelf", "-r", str(self.obj)], capture_output=True, text=True)
        section = re.search(
            r"Relocation section '\.rela\.data\.video_drivers' at.*?\n(.*?)\n\n",
            done.stdout, re.S)
        self.assertIsNotNone(section)
        count = len(re.findall(r"R_X86_64_64", section.group(1)))
        self.assertEqual(count, 2, "the table is NULL-terminated; its size is part of it")


class FrameLayout(unittest.TestCase):
    """The tiled frame addressing maps every pixel somewhere of its own."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.table = build_offset_table()

    def test_no_two_pixels_share_an_offset(self) -> None:
        offsets = list(self.table.values())
        self.assertEqual(len(offsets), len(set(offsets)),
                         "two sampled pixels map to the same frame offset")

    def test_offsets_stay_inside_the_frame(self) -> None:
        for (x, y), offset in self.table.items():
            self.assertLess(offset, FRAME_BYTES,
                            f"pixel ({x}, {y}) writes past the frame at {offset:#x}")

    def test_sampled_values_are_pinned(self) -> None:
        """The exact arithmetic, pinned.

        These are the offsets the current formula produces. They were read off a
        run of it rather than derived here: the point is not that they are
        independently correct but that they are recorded, so changing the formula
        changes this table and the change shows up in a diff instead of on a
        television. The two tests above are the ones that carry the real
        requirement.
        """
        pinned = {
            (0, 0): 0x0,
            (1, 0): 0x4,
            (127, 0): 0xaf8c,
            (128, 0): 0x10000,
            (1919, 0): 0xeaf8c,
            (0, 1): 0x10,
            (0, 127): 0x5f70,
            (0, 128): 0xf0000,
            (0, 1079): 0x780670,
            (1918, 1078): 0x86a9e8,
        }
        for key, expected in pinned.items():
            with self.subTest(pixel=key):
                self.assertEqual(self.table[key], expected,
                                 f"the frame layout changed at {key}: "
                                 f"{self.table[key]:#x} instead of {expected:#x}")


class Artifact(unittest.TestCase):
    """The built folder is a title the console can be asked to run."""

    dist = ROOT / "dist" / "PPSA99169"

    def setUp(self) -> None:
        if not (self.dist / "eboot.bin").is_file():
            self.skipTest("nothing built; run tools/build-title.sh")

    def test_eboot_is_a_fake_self(self) -> None:
        magic = (self.dist / "eboot.bin").read_bytes()[:4]
        self.assertEqual(magic, bytes.fromhex("4f153d1d"),
                         "eboot.bin must be a signed fake self, not a raw ELF")

    def test_eboot_carries_a_64_bit_x86_64_elf_inside_it(self) -> None:
        """The container's own header, and the ELF program image it wraps.

        eboot.bin is not an ELF, so its first bytes are not an ELF header: it is a
        fake self, a small header followed by a table of segments, and the ELF
        program image is written into the first segment. Reading the file as an
        ELF - which an earlier version of this test did - fails on the container's
        second byte and proves nothing about the program inside it.

        What the container's own fields mean is deliberately not decoded here.
        Their layout belongs to the pipeline's self writer
        (tooling/native/self_container.cpp), and a test that restates it would be
        a second, drifting copy - as the two attempts before this one were. What
        is checked instead is what a wrong build would break: a real ELF image is
        inside the file, it is 64-bit x86-64, and it declares an entry point.
        """
        blob = (self.dist / "eboot.bin").read_bytes()
        self.assertEqual(blob[:4], bytes.fromhex("4f153d1d"))
        self.assertEqual(len(blob), 8_026_239, "the image is not the one this build made")

        offset = blob.find(b"\x7fELF")
        self.assertNotEqual(offset, -1, "no ELF program image inside the container")
        image = blob[offset:]
        self.assertEqual(image[4], 2, "the program image is not 64-bit")
        self.assertEqual(struct.unpack_from("<H", image, 18)[0], 0x3E,
                         "the program image is not x86-64")
        entry = struct.unpack_from("<Q", image, 24)[0]
        self.assertGreater(entry, 0, "the program image declares no entry point")
        self.assertLess(entry, 0x1000,
                        "the program image's entry point is not near its start")

    def test_param_json_names_this_folder(self) -> None:
        import json
        param = json.loads((self.dist / "sce_sys" / "param.json").read_text(encoding="utf-8"))
        self.assertEqual(param["titleId"], self.dist.name)
        self.assertRegex(param["contentId"], rf"PPSA\d{{5}}")


if __name__ == "__main__":
    unittest.main()
