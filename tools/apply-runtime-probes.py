#!/usr/bin/env python3
# PS5 RetroArch - the runtime probes, kept so a debug run does not start from zero.
#
#   python3 tools/apply-runtime-probes.py <configured-tree>          apply all
#   python3 tools/apply-runtime-probes.py <configured-tree> --revert  remove all
#   python3 tools/apply-runtime-probes.py --list                      show them
#
# Why this exists. Every probe in this project is written into the configured copy
# of RetroArch, and `tools/retroarch-sources.sh` rebuilds that copy from upstream -
# so a probe vanishes the moment anyone reconfigures, which is exactly what
# happened to the set that finally found the input fault. These are kept here
# instead: version-controlled, re-appliable in one command, and never part of the
# shipping build, because `tools/build-title.sh` does not call this script.
#
# Why probes in the frontend at all. The kernel log cannot see inside the
# frontend, and RetroArch's own file logger is initialised *after* it parses its
# config - so anything that goes wrong during startup is invisible to both. A line
# written to /app0/trace.txt from inside the failing function is the only
# instrument that works there, and it is what turned three "somewhere in init"
# theories into named statements.
#
# The probes are plain fprintf calls appended to the title's trace file. They are
# deliberately not routed through src/trace.cpp: the configured tree is C, and a
# raw stdio call is one line that cannot itself be the reason a build fails.
#
# What each one is for is in the note beside it - these are the landmarks that
# were hard to find the first time.

from __future__ import annotations

import sys
from pathlib import Path

# (file, anchor, inserted-before-anchor, marker, note)
PROBES = [
    (
        "input/input_driver.c",
        "   void              *new_data    = NULL;\n"
        "   input_driver_t         **input = &input_driver_st.current_driver;\n",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe INV: entered tmp=%p *input=%p configured=\\\"%s\\\"\\n\",\n"
        "                      (void*)tmp, (void*)*input, settings->arrays.input_driver); fclose(f); } }\n",
        "probe INV: entered",
        "what the input driver initialisation is handed: `tmp` is the pointer the "
        "video driver carried in - if it equals the selected driver, no video driver "
        "supplied one and the early return would skip initialisation entirely",
    ),
    (
        "input/input_driver.c",
        "   if (tmp)\n      *input = tmp;\n",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe INV: after-selection *input=%p tmp=%p\\n\",\n"
        "                      (void*)*input, (void*)tmp); fclose(f); } }\n",
        "probe INV: after-selection",
        "whether the selection survived to the point the wrap is decided - the "
        "difference between the wrap below being reachable and being dead code",
    ),
    (
        "input/input_driver.c",
        "   input_driver_st.current_data = new_data;\n",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe INV: after-wrap data=%p\\n\", new_data); fclose(f); } }\n",
        "probe INV: after-wrap data",
        "the state the wrapped driver returned - NULL here means the driver's own "
        "init refused, not that it was never called",
    ),
    (
        "input/input_driver.c",
        "   if (!input)\n      return NULL;\n   if ((ret = input->init(name)))",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe INV: wrap calling init\\n\"); fclose(f); } }\n",
        "probe INV: wrap calling init",
        "the last line before any input driver's own init runs",
    ),
    (
        "gfx/video_driver.c",
        "   if (!video_driver_init_input(tmp, settings, verbosity_enabled))",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe: about to call video_driver_init_input\\n\"); fclose(f); } }\n",
        "probe: about to call video_driver_init_input",
        "brackets the whole input-initialisation step from the video driver's side",
    ),
    (
        "retroarch.c",
        "   drivers_init(settings, DRIVERS_CMD_ALL, (enum driver_lifetime_flags)0, verbosity_enabled);",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe M:drivers-init-all\\n\"); fclose(f); } }\n",
        "probe M:drivers-init-all",
        "the landmark that separated \"the crash is in a driver\" from \"the crash is "
        "after every driver\", which took several runs to establish",
    ),
    (
        "retroarch.c",
        "   command_event(CMD_EVENT_CONTROLLER_INIT, NULL);",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe M:controller-init\\n\"); fclose(f); } }\n",
        "probe M:controller-init",
        "the last landmark before the crash that reading the config causes",
    ),
]


def apply(tree: Path, revert: bool) -> int:
    failures = 0
    for name, anchor, insert, marker, _note in PROBES:
        path = tree / name
        if not path.is_file():
            print(f"  {name}: MISSING FILE")
            failures += 1
            continue
        text = path.read_text(encoding="utf-8")
        if revert:
            if insert not in text:
                print(f"  {name}: not probed")
                continue
            path.write_text(text.replace(insert, ""), encoding="utf-8")
            print(f"  {name}: probe removed")
            continue
        if marker in text:
            print(f"  {name}: present")
            continue
        if anchor not in text:
            print(f"  {name}: ANCHOR NOT FOUND for {marker!r}")
            failures += 1
            continue
        path.write_text(text.replace(anchor, insert + anchor, 1), encoding="utf-8")
        print(f"  {name}: applied {marker}")
    return 1 if failures else 0


def main() -> int:
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        print(__doc__ or "")
        return 2
    if args[0] == "--list":
        for name, _anchor, _insert, marker, note in PROBES:
            print(f"{name}\n    {marker}\n    {note}\n")
        return 0
    tree = Path(args[0])
    if not (tree / "retroarch.c").is_file():
        print(f"error: {tree} does not look like a configured RetroArch tree", file=sys.stderr)
        return 2
    return apply(tree, "--revert" in args)


if __name__ == "__main__":
    raise SystemExit(main())
