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
    (
        # RetroArch guards its own C entry point with `#ifndef HAVE_MAIN`, and
        # HAVE_MAIN turns out to mean something other than "the platform has a
        # main". The comment above rarch_main says it: with HAVE_MAIN undefined,
        # rarch_main *is* the program - it initialises, then runs the main loop,
        # and does not return until the frontend quits. With HAVE_MAIN defined,
        # rarch_main only initialises and returns immediately.
        #
        # This port defined HAVE_MAIN to get rid of a duplicate `main` symbol,
        # which is what the flag looks like it is for. The result was a title that
        # initialised every driver correctly - our video driver included, its
        # display open at 1920x1080, per /app0/trace.txt on the console - and then
        # exited zero without ever drawing a frame or printing a word. The loop
        # had been compiled out.
        #
        # The duplicate goes away by the change below instead: upstream's `main`
        # is renamed, HAVE_MAIN stays undefined, and rarch_main keeps the loop.
        # src/main.cpp's `main` is then the only definition, and the SDK's _start
        # reaches a real entry point rather than a wrapper that returns at once.
        "retroarch.c",
        "int main(int argc, char *argv[])\n{\n   return rarch_main(argc, argv, NULL);\n}",
        "/* Renamed by this port (patches/series, 0003). The SDK's _start calls\n"
        " * `main`, which src/main.cpp supplies, so leaving this one named `main`\n"
        " * would be two definitions of one symbol. It is kept, and stays callable,\n"
        " * so that upstream's intended entry remains visible here. */\n"
        "int rarch_main_entry(int argc, char *argv[])\n{\n"
        "   return rarch_main(argc, argv, NULL);\n}",
        "int rarch_main_entry(int argc, char *argv[])",
    ),
    (
        # A null joypad driver is a normal state on this console, and upstream
        # dereferences it. input_driver_collect_system_input calls
        # input_joypad_analog_axis with input_st->primary_joypad, which is NULL
        # when no joypad driver initialised - and none does here, because every
        # joypad driver upstream ships (udev, linuxraw, SDL, XInput, dinput) needs
        # a library or a header this SDK does not carry. input_joypad_analog_axis
        # then reads drv->axis with no check on drv at all: the function guards
        # every `axis` member against AXIS_NONE and never guards the struct.
        #
        # The failure this caused is worth recording, because nothing about it
        # looked like a null pointer. The title started, the display opened, the
        # first frame was presented - and then it died on the second pass through
        # the runloop with SIGSEGV, fault address 0x18. Probe by probe it came down
        # to this call, which only runs when the menu is alive, which is why pass
        # one survived: menu_is_alive is set after the first frame.
        #
        # The guard below is the smallest change that makes the function honest
        # about a driver it does not have: with no driver there is no axis to read,
        # which is exactly what the function's own `res = 0` means.
        "input/input_driver.c",
        "static int16_t input_joypad_analog_axis(\n"
        "      unsigned input_analog_dpad_mode,\n"
        "      float input_analog_deadzone,\n"
        "      float input_analog_sensitivity,\n"
        "      const input_device_driver_t *drv,\n"
        "      rarch_joypad_info_t *joypad_info,\n"
        "      unsigned idx,\n"
        "      unsigned ident,\n"
        "      const struct retro_keybind *binds)\n"
        "{\n",
        "static int16_t input_joypad_analog_axis(\n"
        "      unsigned input_analog_dpad_mode,\n"
        "      float input_analog_deadzone,\n"
        "      float input_analog_sensitivity,\n"
        "      const input_device_driver_t *drv,\n"
        "      rarch_joypad_info_t *joypad_info,\n"
        "      unsigned idx,\n"
        "      unsigned ident,\n"
        "      const struct retro_keybind *binds)\n"
        "{\n"
        "   /* Added by this port (patches/series, 0004). Every member read below is\n"
        "    * reached through this driver; with no joypad driver present, which is\n"
        "    * this console's normal state, there is no axis to read and the rest of\n"
        "    * the function's answer is the zero it already starts with. */\n"
        "   if (drv == NULL)\n"
        "      return 0;\n",
        "if (drv == NULL)\n      return 0;",
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
