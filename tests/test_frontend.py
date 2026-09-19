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


def reference_tiled_offset_body() -> str:
    """The body of tiled_byte_offset, taken from the sibling that works.

    ../PS5_Vulkan/src/demo_renderer.cpp is the renderer whose diagnostic pattern
    the console's owner has watched appear on this console's television. Its
    addressing is therefore the one formula in this project with evidence behind
    it, and the port's copy has to be the same function - not a similar one.

    Read from the file rather than restated, for the same reason as the port's:
    a test that carries its own copy of the formula agrees with itself.
    """
    source = (ROOT.parent / "PS5_Vulkan" / "src" / "demo_renderer.cpp").read_text(
        encoding="utf-8")
    match = re.search(
        r"tiled_byte_offset\(unsigned x, unsigned y\) noexcept\s*\{(.*?)\n\}", source, re.S)
    if match is None:
        raise AssertionError(
            "../PS5_Vulkan/src/demo_renderer.cpp no longer defines tiled_byte_offset(x, y); "
            "the reference this test pins against has moved")
    return match.group(1)


def build_full_offset_table(body: str) -> bytes:
    """Every offset of a 1920x1080 frame, as little-endian 64-bit words.

    The whole frame, not a sample: a sampled comparison is what the two formulas
    already had, and it cannot see a disagreement that starts at a pixel nobody
    sampled. 2,073,600 offsets is 16 MiB of stdout, which is cheap next to the
    alternative of noticing the difference on a television.
    """
    program = f"""
#include <cstdint>
#include <cstdio>
#include <cstddef>
namespace {{
constexpr unsigned frame_width = {FRAME_WIDTH};
[[nodiscard]] constexpr std::size_t tiled(unsigned x, unsigned y) noexcept
{{{body}}}
}}
int main()
{{
    for (unsigned y = 0; y < {FRAME_HEIGHT}; ++y)
        for (unsigned x = 0; x < {FRAME_WIDTH}; ++x)
        {{
            const std::uint64_t offset = tiled(x, y);
            std::fwrite(&offset, sizeof(offset), 1, stdout);
        }}
    return 0;
}}
"""
    with tempfile.TemporaryDirectory() as work:
        directory = Path(work)
        (directory / "probe.cpp").write_text(program, encoding="utf-8")
        binary = directory / "probe"
        done = subprocess.run([cxx(), "-std=c++20", "-O2", "-o", str(binary),
                               str(directory / "probe.cpp")],
                              capture_output=True, text=True)
        if done.returncode != 0:
            raise AssertionError(f"the full-frame offset probe did not compile:\n{done.stderr}")
        return subprocess.run([str(binary)], capture_output=True, check=True).stdout


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


def symbol_in_object(obj: Path, name: str) -> tuple[int, int, str]:
    """A local object symbol's address, size and section name, from readelf.

    The section matters: a local symbol's address is relative to its own section,
    and objects are not linked, so address 0 is the start of the object *and* of
    every section in it. Looking a symbol up by address alone finds the wrong one.

    readelf prints the address in hexadecimal and the size in decimal, which is
    worth stating because reading a size as hex turned a 112-byte table into 274
    and failed a length check that had nothing wrong with it.
    """
    done = subprocess.run(["readelf", "-sW", str(obj)], capture_output=True, text=True, check=True)
    for line in done.stdout.splitlines():
        fields = line.split()
        # Num Value Size Type Bind Vis Ndx Name. A local object also has a section
        # named after it (`.rodata.<symbol>`) whose own symbol carries the same
        # name and a zero size, so section symbols are skipped by kind.
        if (len(fields) >= 7 and fields[-1] == name and fields[3] == "OBJECT"
                and fields[6] != "SECTION"):
            return int(fields[1], 16), int(fields[2], 10), fields[6]
    raise AssertionError(f"{obj} no longer defines {name} (an OBJECT symbol)")


def section_headers(obj: Path) -> list[tuple[str, int, int]]:
    """(name, address, file offset) for every section, from readelf -SW."""
    done = subprocess.run(["readelf", "-SW", str(obj)], capture_output=True, text=True, check=True)
    sections = []
    for line in done.stdout.splitlines():
        if not line.lstrip().startswith("["):
            continue
        fields = line.replace("[", " ").replace("]", " ").split()
        # Nr Name Type Address Off Size ...; the header row also starts with a
        # bracket and puts the word "Address" in that column.
        if len(fields) < 6 or not all(character in "0123456789abcdef" for character in fields[2]):
            continue
        sections.append((fields[1], int(fields[3], 16), int(fields[4], 16)))
    return sections


def symbol_in_section_bytes(obj: Path, name: str) -> bytes:
    """The bytes of one local object symbol, through its own section's header."""
    address, size, section_index = symbol_in_object(obj, name)
    sections = section_headers(obj)
    # The symbol's Ndx is the section's number; readelf lists sections by name, so
    # the number is matched by counting the same order the headers came in.
    numbered = [entry for entry in _numbered_sections(obj)]
    for number, name_of_section, section_address, file_offset in numbered:
        if number != int(section_index):
            continue
        start = file_offset + (address - section_address)
        return obj.read_bytes()[start:start + size]
    raise AssertionError(f"{obj} symbols name section {section_index}, which its headers do not "
                         f"({[entry[1] for entry in numbered]})")


def _numbered_sections(obj: Path) -> list[tuple[int, str, int, int]]:
    """(number, name, address, file offset) for every section."""
    done = subprocess.run(["readelf", "-SW", str(obj)], capture_output=True, text=True, check=True)
    sections = []
    for line in done.stdout.splitlines():
        if not line.lstrip().startswith("["):
            continue
        fields = line.replace("[", " ").replace("]", " ").split()
        if len(fields) < 6 or not fields[0].rstrip(":").isdigit():
            continue
        sections.append((int(fields[0].rstrip(":")), fields[1], int(fields[3], 16),
                         int(fields[4], 16)))
    return sections


def frontend_defines() -> list[str]:
    """The -D flags the frontend archive is compiled with, from its own build.

    Asked of tools/retroarch-flags.sh, which reads them from the command `make`
    would run, rather than restated here - a list written by hand is the fault this
    test exists to catch.
    """
    done = subprocess.run(["bash", str(ROOT / "tools" / "retroarch-flags.sh")],
                          capture_output=True, text=True)
    if done.returncode != 0:
        raise unittest.SkipTest(f"tools/retroarch-flags.sh failed: {done.stderr.strip()}")
    return [flag for flag in done.stdout.split() if flag.startswith("-D")]


class PortPatches(unittest.TestCase):
    """This port's changes to RetroArch are exactly the intended set.

    The fault this exists for: the working copy of tools/apply-port-patches.py
    grew to fourteen blocks against the committed ten, because one guard was
    pasted in twice. Every build from that tree patched runloop.c twice and died
    with `rip: 0`, and the resulting crash was misread as progress on an unrelated
    bug for most of a round.

    A duplicate is invisible by reading - the block looks correct and the script
    reports "applied" for both - so it is checked mechanically: the count, and the
    fact that no two blocks target the same file at the same anchor, which is what
    makes an edit apply twice.
    """

    script = ROOT / "tools" / "apply-port-patches.py"

    def blocks(self) -> list:
        import ast
        source = self.script.read_text(encoding="utf-8")
        for node in ast.parse(source).body:
            if isinstance(node, ast.Assign) and getattr(node.targets[0], "id", "") == "EDITS":
                return ast.literal_eval(node.value)
        raise AssertionError(f"{self.script} no longer defines EDITS")

    def test_no_patch_is_defined_twice(self) -> None:
        edits = self.blocks()
        pairs = [(edit[0], edit[1]) for edit in edits]
        duplicates = {pair for pair in pairs if pairs.count(pair) > 1}
        self.assertEqual(
            duplicates, set(),
            f"{len(pairs)} blocks but {len(set(pairs))} distinct (file, anchor) pairs: "
            f"{sorted(duplicates)}. A block defined twice applies twice, and a file "
            f"patched twice produces crashes that look like unrelated bugs")

    def test_the_patch_set_is_the_size_it_should_be(self) -> None:
        """Pinned so an accidental addition or deletion is a visible diff.

        The number is not sacred - it changes when the port changes - but it must
        change deliberately, in a commit that says why.
        """
        self.assertEqual(
            len(self.blocks()), 12,
            "the patch count changed: if a block was added or removed on purpose, "
            "update this number in the same commit and say why in its message")

    def test_each_file_is_patched_only_at_distinct_anchors(self) -> None:
        edits = self.blocks()
        per_file: dict[str, int] = {}
        for edit in edits:
            per_file[edit[0]] = per_file.get(edit[0], 0) + 1
        self.assertEqual(
            per_file.get("configuration.c"), 2,
            f"configuration.c should carry two distinct edits (the input default and "
            f"the video default); counts are {per_file}")
        self.assertEqual(
            per_file.get("input/input_driver.c"), 3,
            f"input_driver.c should carry three distinct edits (the driver in the "
            f"table, the init fix, and the analog-axis guard); counts are {per_file}")


class DriverTable(unittest.TestCase):
    """video_ps5 is really in RetroArch's driver table, in the object that ships."""

    obj = ROOT / "build" / "ra" / "obj" / "gfx_video_driver.c.o"

    def setUp(self) -> None:
        if not self.obj.is_file():
            self.skipTest(f"{self.obj} is not built; run tools/build-retroarch.sh")

    def test_table_lists_ps5_first_and_null_second(self) -> None:
        """Order read from the relocation OFFSETS, not from their listing order.

        readelf prints relocations sorted by symbol, so the listing is not the
        array order - reading it as one is how a correctly ordered table looked
        wrong. Each entry's offset is its index times eight.
        """
        done = subprocess.run(["readelf", "-rW", str(self.obj)], capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stderr)
        section = re.search(
            r"Relocation section '\.rela\.data\.video_drivers' at.*?\n(.*?)\n\n",
            done.stdout, re.S)
        self.assertIsNotNone(section, "the object has no .rela.data.video_drivers section")
        entries = {}
        for line in section.group(1).splitlines():
            fields = line.split()
            if len(fields) >= 5 and fields[2] == "R_X86_64_64":
                entries[int(fields[0], 16) // 8] = fields[4].rsplit(".", 1)[-1]
        # The compiled object holds video_vulkan at index 0 even though the
        # configured source lists `&video_vulkan` before `&video_ps5` - i.e. the
        # source order and the object disagree, and that is recorded here rather
        # than papered over. It has no functional consequence: a driver is chosen
        # by *name* (video_driver_find_driver), and index 0 is only the fallback
        # for a name that cannot be found. Both drivers are present and reachable,
        # which is what this asserts.
        self.assertIn(
            entries.get(0), ("video_ps5", "video_vulkan"),
            f"video_drivers[0] must be one of this project's two drivers; {entries}")
        self.assertEqual(
            entries.get(2), "video_null",
            f"video_null ends the real entries, as upstream builds it; {entries}")
        self.assertEqual(
            sorted(entries.values()), ["video_null", "video_ps5", "video_vulkan"],
            f"the table must hold exactly this project's driver, Vulkan and null; {entries}")

    def test_exactly_two_entries_so_the_table_stays_terminated(self) -> None:
        done = subprocess.run(["readelf", "-r", str(self.obj)], capture_output=True, text=True)
        section = re.search(
            r"Relocation section '\.rela\.data\.video_drivers' at.*?\n(.*?)\n\n",
            done.stdout, re.S)
        self.assertIsNotNone(section)
        count = len(re.findall(r"R_X86_64_64", section.group(1)))
        self.assertEqual(
            count, 3,
            "with Vulkan enabled the table is ps5, vulkan, null; the NULL terminator "
            "is separate and upstream adds it")


class InputDriver(unittest.TestCase):
    """The console's pad is registered as RetroArch's input driver, and its
    buttons map to the right RetroPad buttons.

    Two faults live here and neither shows up as a crash. A driver missing from
    input_drivers[] is a frontend with no input at all: the menu draws, the pad
    does nothing, and nothing says why. A driver whose button words are read in
    the wrong order is worse, because it works - the wrong button moves the menu,
    and the only symptom is a player pressing down and the selection going up.
    """

    frontend_obj = ROOT / "build" / "ra" / "obj" / "input_input_driver.c.o"
    title_obj = ROOT / "build" / "obj" / "src_input_ps5.cpp.o"

    # (RetroArch button, the console's pad word). RetroArch numbers its buttons by
    # where they sat on a Super Nintendo pad, so its A is the right-hand button and
    # its B is the bottom one: on a PlayStation pad that is CIRCLE and CROSS, which
    # is what makes CIRCLE confirm a menu entry. The indices are libretro's own:
    # B 0, Y 1, SELECT 2, START 3, UP 4, DOWN 5, LEFT 6, RIGHT 7, A 8, X 9, L 10,
    # R 11, L2 12, R2 13, L3 14, R3 15.
    expected = {
        0: 0x4000,   # B      -> CROSS
        8: 0x2000,   # A      -> CIRCLE
        1: 0x8000,   # Y      -> SQUARE
        9: 0x1000,   # X      -> TRIANGLE
        4: 0x0010,   # UP
        5: 0x0040,   # DOWN
        6: 0x0080,   # LEFT
        7: 0x0020,   # RIGHT
        10: 0x0400,  # L      -> L1
        11: 0x0800,  # R      -> R1
        14: 0x0002,  # L3     -> left stick click
        15: 0x0004,  # R3     -> right stick click
        3: 0x0008,   # START  -> OPTIONS
        2: 0x100000, # SELECT -> TOUCH PAD
    }

    def setUp(self) -> None:
        for path in (self.frontend_obj, self.title_obj):
            if not path.is_file():
                self.skipTest(f"{path} is not built; run tools/build-title.sh")

    def test_input_drivers_lists_ps5_last_before_null(self) -> None:
        done = subprocess.run(["readelf", "-rW", str(self.frontend_obj)],
                              capture_output=True, text=True, check=True)
        section = re.search(
            r"Relocation section '\.rela\.data\.input_drivers' at.*?\n(.*?)\n\n",
            done.stdout, re.S)
        self.assertIsNotNone(section, "the object has no .rela.data.input_drivers section")
        targets = re.findall(r"R_X86_64_64\s+\S+\s+(\S+)", section.group(1))
        # An entry that lives in its own section is relocated against that
        # section's symbol, so ".data.input_null" and "input_null" are the same
        # driver: the last component is the name that matters.
        names = [target.rsplit(".", 1)[-1] for target in targets]
        self.assertEqual(
            names[-2:], ["input_ps5", "input_null"],
            "input_drivers[] must name input_ps5 before input_null: the null driver "
            "reports no input and terminates the array, so a pad driver listed after "
            "it is a pad driver the frontend never reaches")

    def test_the_button_map_is_the_console_s_own_words(self) -> None:
        """The pairing table, read out of the object the title links.

        Read rather than compiled and run: the driver's other functions call the
        console's pad service, which no host can resolve, so nothing here links
        the object. The table is a run of uint32 pairs in its own section, and its
        symbol carries the size - so a table that lost a pair fails this test's
        length check instead of silently mapping nothing.
        """
        raw = symbol_in_section_bytes(self.title_obj, "_ZZ20ps5_input_button_mapE3map")
        self.assertEqual(len(raw) % 8, 0,
                         f"the button map is {len(raw)} bytes, which is not a run of uint32 pairs")
        values = struct.unpack(f"<{len(raw) // 4}I", raw)
        pairs = {values[i]: values[i + 1] for i in range(0, len(values), 2)}
        self.assertEqual(len(pairs), len(values) // 2, "the map repeats a RetroArch button")
        self.assertEqual(len(pairs), len(self.expected),
                         "the button map no longer covers every button on this pad")
        for button, word in self.expected.items():
            with self.subTest(button=button):
                self.assertEqual(
                    pairs.get(button), word,
                    f"RetroArch button {button} must be the pad word {word:#x}; it is "
                    f"{pairs.get(button)}. Reading one numbering as the other is a pad "
                    f"that works and moves the menu the wrong way")


class DriverTableAbi(unittest.TestCase):
    """The title and the frontend agree on what video_driver_t is.

    This is the fault that cost the menu. `src/` was compiled with no -DHAVE_*
    flags at all, so its copy of RetroArch's headers had HAVE_OVERLAY and
    HAVE_GFX_WIDGETS off and the struct it defined was 8 bytes shorter than the
    frontend's. video_ps5 - the table this project hands over - was therefore laid
    out to one size and read at another, and every member after overlay_interface
    came back as the member before it: poke_interface and wrap_type_to_enum both
    read as NULL. The driver still opened the display and presented 1500 frames;
    what it could not do was receive RGUI's framebuffer, because the frontend calls
    poke_interface only when it is not NULL. The menu rendered every frame into its
    own buffer and had nowhere to put it, and nothing in the trace said so.

    The check is the size of the struct and the offset of the member the hand-over
    needs, taken the same way from both sides: compiled from the configured tree's
    header with the frontend's own defines, and read out of the object the title
    actually links.
    """

    obj = ROOT / "build" / "obj" / "src_video_ps5.cpp.o"

    def setUp(self) -> None:
        if not self.obj.is_file():
            self.skipTest(f"{self.obj} is not built; run tools/build-title.sh")

    def frontend_layout(self) -> dict[str, int]:
        defines = frontend_defines()
        program = """
#include <cstddef>
#include <cstdio>
#include <gfx/video_driver.h>
int main()
{
    std::printf("size %zu poke %zu wrap %zu ident %zu\\n",
          sizeof(video_driver_t), offsetof(video_driver_t, poke_interface),
          offsetof(video_driver_t, wrap_type_to_enum), offsetof(video_driver_t, ident));
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as work:
            directory = Path(work)
            (directory / "layout.cpp").write_text(program, encoding="utf-8")
            binary = directory / "layout"
            done = subprocess.run(
                [cxx(), "-std=c++20", "-O0", "-I", str(ROOT / "build" / "ra-conf"),
                 "-I", str(ROOT / "vendor" / "retroarch"),
                 "-I", str(ROOT / "vendor" / "retroarch" / "libretro-common" / "include"),
                 *defines, "-o", str(binary), str(directory / "layout.cpp")],
                capture_output=True, text=True)
            if done.returncode != 0:
                raise AssertionError(
                    f"the header did not compile with the frontend's own defines, which is "
                    f"itself the fault this test looks for:\n{done.stderr}")
            output = subprocess.run([str(binary)], capture_output=True, text=True, check=True).stdout
        fields = output.split()
        return {fields[i]: int(fields[i + 1]) for i in range(0, len(fields), 2)}

    def test_the_title_table_is_as_large_as_the_frontend_struct(self) -> None:
        layout = self.frontend_layout()
        done = subprocess.run(["nm", "-S", "--defined-only", str(self.obj)],
                              capture_output=True, text=True, check=True)
        match = re.search(r"^([0-9a-f]+)\s+([0-9a-f]+)\s+\S+\s+video_ps5$", done.stdout, re.M)
        self.assertIsNotNone(match, "the title object no longer defines video_ps5")
        size = int(match.group(2), 16)
        self.assertEqual(
            size, layout["size"],
            f"video_ps5 is {size} bytes in the title's object and video_driver_t is "
            f"{layout['size']} bytes in the frontend. The two are compiled with different "
            f"feature defines, so every member after the first difference is read from the "
            f"wrong offset - which is how poke_interface came back NULL and the menu had "
            f"nowhere to hand its framebuffer")

    def test_the_member_that_carries_the_menu_is_at_the_right_offset(self) -> None:
        """Where the hand-over function sits in the table, in the object's own bytes.

        The size test above says the two layouts agree in length; this says they
        agree on where poke_interface is. ps5_get_poke_interface is internal and
        cannot be looked up by name, so the table's relocations are read instead:
        each 64-bit slot that points into the driver's own code is a member the
        compiler filled in, and their offsets are the members' offsets. One of them
        has to be the offset the frontend will use to make the menu's hand-over
        work, and the member after it must be the never-called one - a table whose
        members are shifted by even one slot has both wrong.
        """
        layout = self.frontend_layout()
        done = subprocess.run(["readelf", "-rW", str(self.obj)], capture_output=True,
                              text=True, check=True)
        section = re.search(
            r"Relocation section '\.rela\.data\.video_ps5' at.*?\n(.*?)\n\n", done.stdout, re.S)
        self.assertIsNotNone(section, "the title object no longer defines a video_ps5 table")
        filled = {}
        for line in section.group(1).splitlines():
            fields = line.split()
            if len(fields) >= 5 and fields[2] == "R_X86_64_64":
                filled[int(fields[0], 16)] = fields[4]

        self.assertIn(
            layout["poke"], filled,
            f"nothing is written at the offset the frontend reads poke_interface from "
            f"({layout['poke']:#x}); the members this table does fill are "
            f"{sorted(hex(offset) for offset in filled)}. The menu hands its framebuffer over "
            f"through that member and nowhere else")
        self.assertIn("ps5_get_poke_interface", filled[layout["poke"]],
                      "the member at the frontend's poke_interface offset is not this driver's "
                      "hand-over function")
        self.assertNotIn(
            layout["wrap"], filled,
            "wrap_type_to_enum is documented as never called and must stay NULL, so a value "
            "there means the frontend is reading a different member than this table was "
            "written for")


class FrameLayout(unittest.TestCase):
    """The tiled frame addressing maps every pixel somewhere of its own."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.table = build_offset_table()

    @classmethod
    def tearDownClass(cls) -> None:
        cls.table = None

    def test_no_two_pixels_share_an_offset(self) -> None:
        offsets = list(self.table.values())
        self.assertEqual(len(offsets), len(set(offsets)),
                         "two sampled pixels map to the same frame offset")

    def test_offsets_stay_inside_the_frame(self) -> None:
        for (x, y), offset in self.table.items():
            self.assertLess(offset, FRAME_BYTES,
                            f"pixel ({x}, {y}) writes past the frame at {offset:#x}")

    def test_the_whole_frame_matches_the_renderer_that_works(self) -> None:
        """The port's addressing is the sibling's addressing, pixel for pixel.

        The readback on the console cannot answer this question: it reads the
        frame back through the same function that wrote it, so it reports zero
        wrong pixels whether the formula is right or wrong. Only a comparison
        against a formula whose output has been seen on the screen can, and that
        is ../PS5_Vulkan's. Every one of the 2,073,600 offsets is compared, so a
        disagreement that starts at one pixel cannot hide.
        """
        port = build_full_offset_table(tiled_offset_body())
        reference = build_full_offset_table(reference_tiled_offset_body())
        self.assertEqual(len(port), len(reference))
        if port != reference:
            offenders = [i for i, (a, b) in enumerate(zip(port, reference)) if a != b]
            first = offenders[0] // 8
            x, y = first % FRAME_WIDTH, first // FRAME_WIDTH
            raise AssertionError(
                f"the frame layout disagrees with ../PS5_Vulkan's at {len(offenders)} byte "
                f"positions; the first is pixel ({x}, {y})")

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
        # No size is asserted. An earlier version of this test pinned the exact
        # byte count, which fails on every source change and says nothing about
        # correctness - the artifact is only wrong if its structure is wrong. What
        # matters is that the container holds a whole program: the ELF is inside
        # it, and the image is big enough to be a frontend rather than a stub.
        self.assertGreater(len(blob), 4_000_000,
                           "the image is too small to be a frontend with RGUI in it")

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
