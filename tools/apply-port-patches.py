#!/usr/bin/env python3
"""Apply this port's changes to RetroArch's sources.

    python3 tools/apply-port-patches.py <configured-tree>

Why a script and not `patch`. A unified diff carries line numbers and context
taken from one revision, so it either fails outright or applies in the wrong
place when upstream moves by a few lines. This port's changes to RetroArch are
small insertions at known anchors, and each anchor is a line upstream has no
reason to change, so the changes are matched on their anchors and are safe to
re-run:

    gfx/video_driver.h   declare the driver beside video_null's declaration
    gfx/video_driver.c   list the driver in video_drivers[] beside &video_null
    qb/config.params.sh  give HAVE_XKBCOMMON the same `auto` default that every
                         other optional library already declares, so that
                         --disable-xkbcommon is an option configure accepts

Every edit prints `applied` or `present`, so the script reports what it did rather
than leaving the tree in an unknown state. Nothing is ever written to
vendor/retroarch: the tree passed in is the configured copy under build/.

Exit status is 0 when every edit is present at the end, 1 otherwise.
"""

from __future__ import annotations

import sys
from pathlib import Path

# (file, anchor, inserted-before-anchor, already-present-marker)
EDITS = [
    (
        "gfx/video_driver.h",
        "extern video_driver_t video_null;",
        "extern video_driver_t video_null;\n"
        "/* Supplied by this project (src/video_ps5.cpp): presents through the\n"
        " * console's VideoOut display layer. Declared with C linkage because\n"
        " * RetroArch's own sources are C. */\n"
        "extern video_driver_t video_ps5;",
        "extern video_driver_t video_ps5;",
    ),
    (
        "gfx/video_driver.c",
        "   &video_null,",
        "   &video_ps5,\n   &video_null,",
        "   &video_ps5,",
    ),
    (
        # qb/config.params.sh declares the default state of every optional
        # library, and configure accepts a --enable/--disable switch for each one
        # it finds there. HAVE_XKBCOMMON is the single optional library upstream
        # checks for without declaring: check_val '' XKBCOMMON ... runs, finds
        # this machine's xkbcommon through pkg-config, and switches a feature on
        # that the build then cannot compile. Declaring it `auto` changes no
        # upstream behaviour - it is the default every sibling already has - and
        # it is what makes --disable-xkbcommon an option rather than an error.
        #
        # The inserted line is one line on purpose. qb.params.sh reads this file
        # by cutting each line at its `=` and evaluating what is left, so a
        # continuation line - even a comment one - is read as a variable
        # assignment and configure dies before it starts.
        "qb/config.params.sh",
        "HAVE_UDEV=auto             # Udev/Evdev gamepad support\n",
        "HAVE_UDEV=auto             # Udev/Evdev gamepad support\n"
        "HAVE_XKBCOMMON=auto        # xkbcommon; declared here because "
        "check_val never declares it upstream\n",
        "HAVE_XKBCOMMON=auto",
    ),
]


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.splitlines()[2].strip(), file=sys.stderr)
        return 2
    tree = Path(sys.argv[1])
    if not tree.is_dir():
        print(f"error: no such tree: {tree}", file=sys.stderr)
        return 2

    ok = True
    for name, anchor, replacement, marker in EDITS:
        path = tree / name
        if not path.is_file():
            print(f"  {name}: MISSING")
            ok = False
            continue
        text = path.read_text(encoding="utf-8")
        if marker in text:
            print(f"  {name}: present")
            continue
        if anchor not in text:
            # Upstream moved the anchor. Refusing here is better than inserting
            # somewhere plausible: a driver registered in the wrong table is a
            # build that links and a frontend that ignores it.
            print(f"  {name}: ANCHOR NOT FOUND ({anchor.strip()!r})")
            ok = False
            continue
        path.write_text(text.replace(anchor, replacement, 1), encoding="utf-8")
        print(f"  {name}: applied")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
