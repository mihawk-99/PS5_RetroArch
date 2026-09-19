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
        # This project's input driver, declared beside the others so that
        # input/input_driver.c can name it in input_drivers[]. The implementation
        # is src/input_ps5.cpp, not a file in this tree, for the same reason
        # video_ps5 is: the console's pad calls and the driver's shape are this
        # project's, and upstream stays upstream.
        #
        # It is an *input* driver and not a joypad driver because every joypad
        # driver upstream ships needs a library this SDK does not carry, so
        # primary_joypad is NULL here - see the guard below. input_state_wrap
        # consults the joypad only when there is one and calls the input driver's
        # own input_state unconditionally, so a pad read directly still reaches
        # the menu.
        "input/input_driver.h",
        "extern input_driver_t input_ps4;",
        "extern input_driver_t input_ps4;\n"
        "/* Supplied by this project (src/input_ps5.cpp): reads the console's pad\n"
        " * through scePadRead and reports it as a RetroPad. Declared with C linkage\n"
        " * because RetroArch's own sources are C. */\n"
        "extern input_driver_t input_ps5;",
        "extern input_driver_t input_ps5;",
    ),
    (
        # And listed in the table itself, before input_null so that a
        # configuration naming \"ps5\" finds it and the null driver stays the last
        # entry, which is what terminates the array.
        "input/input_driver.c",
        "   &input_null,\n   NULL,\n};",
        "   &input_ps5,\n   &input_null,\n   NULL,\n};",
        "   &input_ps5,",
    ),
    (
        # A core that has not loaded registers no controller-port callback, and
        # upstream calls it anyway.
        #
        # This is a real latent fault, found while chasing the config-path crash
        # and kept because it is the same shape as the joypad guard below:
        # `core_set_controller_port_device` guards its own `pad` argument and never
        # guards the callback it exists to call. `dynamic_dummy.c` only survives it
        # because it happens to define an empty stub, so a build whose dummy core
        # does not would jump to address zero - which is exactly what the console
        # reported for the crash this was found during:
        # `page fault (user read instruction, page not present)`, `rip: 0`.
        #
        # It did NOT turn out to be that crash: with this guard compiled in
        # (verified in the object as `test %rax,%rax; je` before `call *%rax`) the
        # title still dies with a byte-identical register dump. The change is kept
        # because the missing check is real, not because it fixed that.
        "runloop.c",
        "   runloop_st->current_core.retro_set_controller_port_device(pad->port, pad->device);\n",
        "   /* Guarded by this port (patches/series, 0007): a core that has not loaded\n"
        "    * registers no callbacks, so this member can be NULL. */\n"
        "   if (!runloop_st->current_core.retro_set_controller_port_device)\n"
        "      return false;\n"
        "   runloop_st->current_core.retro_set_controller_port_device(pad->port, pad->device);\n",
        "registers no callbacks, so this member can be NULL",
    ),
    (
        # Select is not initialise: the input driver was named and never wrapped.
        #
        # `input_driver_find_driver` runs during driver pre-initialisation and
        # *selects* a driver into `input_driver_st.current_driver`; it deliberately
        # does not initialise one. Initialisation is `input_driver_init_wrap`, and
        # on this path the only call to it is at the end of
        # `video_driver_init_input` - which returns early when
        # `input_driver_st.current_driver` is already set:
        #
        #     if (*input)
        #        return true;                      <- taken, because pre-init selected
        #     ...
        #     input_driver_init_wrap(...)           <- never reached
        #
        # Upstream's intent for that early return is a *video* driver that
        # pre-initialised an input driver of its own. This build's video driver does
        # not: it leaves the pointers alone on purpose, so that RetroArch's own
        # input path is used. The result was measured - the driver is named
        # (`probe drivers: input="ps5"`), `ps5_input_init` never runs, `current_data`
        # stays NULL, and every button read answers 0.
        #
        # Clearing the selection here makes the existing code below re-select and
        # then wrap it, which is exactly what that code is for. It is a no-op for a
        # video driver that did pre-initialise input, because that case still
        # returns above via `tmp`.
        "input/input_driver.c",
        "   input_driver_t         **input = &input_driver_st.current_driver;\n"
        "   if (*input)\n",
        "   input_driver_t         **input = &input_driver_st.current_driver;\n"
        "   /* Changed by this port (patches/series, 0009): a driver selected during\n"
        "    * pre-initialisation is not an initialised one, and leaving it here makes\n"
        "    * the wrap below unreachable. Since `tmp` is NULL - the video driver did\n"
        "    * not provide an input driver - the selection is discarded so that the\n"
        "    * code below re-selects from the settings and then initialises it. */\n"
        "   if (tmp == NULL)\n"
        "      *input = NULL;\n"
        "   if (*input)\n",
        "a driver selected during",
    ),
    (
        # The console's pad is this build's input driver, so it is also the
        # compiled default.
        #
        # This is what makes the driver reachable without a config file. There is
        # no config file at runtime yet: content loading rebuilds argv and drops
        # the title's `-c`, and the fix for that is parked because reading the
        # config still crashes the launch. With no config read, the whole input
        # path runs on compiled defaults - `probe init_input entered: *input=ba4dc0
        # configured="null" joypad="null"` is that measurement - so naming this
        # project's driver here is what puts it in `input_drivers[]`'s place before
        # the frontend initialises anything.
        #
        # One line, and reversible: when the config file is readable again the
        # config's own `input_driver` wins at parse time and this default stops
        # mattering, at which point it can be deleted.
        "configuration.c",
        "      case INPUT_NULL:\n          break;",
        "      case INPUT_NULL:\n"
        "          /* Named by this port (patches/series, 0008): the console's own pad\n"
        "           * driver is the only input driver this build can run, and with no\n"
        "           * config file read it has to come from the compiled default. */\n"
        "          return \"ps5\";",
        "the only input driver this build can run",
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
