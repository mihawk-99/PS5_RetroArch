#!/usr/bin/env python3
"""Turn a console crash capture into function names.

    python3 tools/symbolize-crash.py <capture> [more captures ...]

Why this exists. A title that dies on the console leaves a klog capture whose
backtrace is a list of bare addresses, and the addresses cannot be symbolized
from the built image: `--exclude-libs` leaves large regions of the link - the
driver, the shader compiler, the SDK's C++ runtime - with no symbol table at all,
and `addr2line` answers `??` for every frame that matters. What does survive is
`build/title.map`, which `tools/build.sh` writes on every link and which names the
archive member and section every address came from.

The console prints the frames innermost first, so this prints them in that order
and then the same list reversed as the call chain, which is the readable form.

Rebuild before using it: a map from an older link describes a different image.
"""

from __future__ import annotations

import bisect
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAP = ROOT / "build" / "title.map"

# lld's map lines are `vma lma size align name`, all but the alignment in hex.
MAP_LINE = re.compile(r"^\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+(\d+)\s+(\S.*)$")
FRAME = re.compile(r"^# ([0-9a-f]{8,16})\s*$")
# The image's text starts here; see the console's own `xotext:` line and the
# linker script's fixed layout.
TEXT_BASE = 0x400000
# The SDK's own modules are not part of this image and are reported separately.
SDK_BASE = 0x800000000


def load_map(path: Path) -> tuple[list[int], list[tuple[int, int, str]]]:
    rows: list[tuple[int, int, str]] = []
    for line in path.read_text(errors="replace").splitlines():
        match = MAP_LINE.match(line)
        if match:
            rows.append((int(match.group(1), 16), int(match.group(3), 16), match.group(5).strip()))
    rows.sort()
    return [row[0] for row in rows], rows


def name_for(starts: list[int], rows: list[tuple[int, int, str]], address: int) -> str:
    """The archive member and section an address belongs to, or why it does not."""
    if address >= SDK_BASE:
        return "the console's own module (see the capture's `dynamic libraries` list)"
    vma = address - TEXT_BASE
    index = bisect.bisect_right(starts, vma) - 1
    if index < 0:
        return "outside this image"
    start, size, name = rows[index]
    where = f"+{vma - start:#x}"
    if vma >= start + size:
        where += " (past the end of that section, so the map is stale - rebuild)"
    # The map names the section, and lld also emits a line per symbol; the symbol
    # line is the one without a path in it.
    if name.endswith(")"):
        return f"{name.rsplit(':', 1)[-1].strip('()')} {where}"
    return f"{name} {where}"


def frames(capture: Path) -> list[int]:
    found: list[int] = []
    in_backtrace = False
    for line in capture.read_text(errors="replace").splitlines():
        if line.startswith("# backtrace:"):
            in_backtrace = True
            continue
        if in_backtrace:
            match = FRAME.match(line)
            if match:
                address = int(match.group(1), 16)
                if address:
                    found.append(address)
                continue
            if found:
                break
    return found


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__ or "", file=sys.stderr)
        return 2
    if not MAP.is_file():
        print(f"{MAP} is missing; run tools/build-title.sh first", file=sys.stderr)
        return 2

    starts, rows = load_map(MAP)
    status = 0
    for argument in sys.argv[1:]:
        capture = Path(argument)
        if not capture.is_file():
            print(f"{capture}: no such capture", file=sys.stderr)
            status = 1
            continue
        addresses = frames(capture)
        print(f"=== {capture}")
        if not addresses:
            print("    no backtrace in this capture: the title may have returned instead of dying")
            continue
        print("    innermost first, as the console prints them:")
        for address in addresses:
            print(f"      {address:#014x}  {name_for(starts, rows, address)}")
        print("    as a call chain:")
        for address in reversed(addresses):
            print(f"      {name_for(starts, rows, address)}")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
