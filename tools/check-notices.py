#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Mihawk
"""Check a title folder's licenses/ against the files it ships.

    python3 tools/check-notices.py [dist/PPSA99169] [--release]

What it establishes, and nothing more:
  - every executable file (eboot.bin, the frontends' es-de/es-de.bin and
    picker/picker.bin, *.prx, *.so) and every core .info file belongs
    to at least one listed part, and the sha256 recorded for it is the file's own;
  - every listed licence text is present and not empty, and a text marked verbatim
    (FBNeo's, Snes9x's, Genesis Plus GX's) is byte for byte the one staged;
  - a part under non-commercial terms is marked as such;
  - with --release, no RPCS3 core is in the title (a console build only, see
    docs/RELEASING.md), and every part's source is at a committed revision and has an
    address, which is what a published build needs.
It does not decide whether a licence permits a combination; the audit does that.
"""

import argparse
import fnmatch
import hashlib
import json
import sys
from pathlib import Path

COVERED = ("lapy-root-daemon.elf", "eboot.bin", "es-de/*.bin", "picker/*.bin", "sce_module/*.prx", "cores/*.so", "cores/*.info",
           "info/*.info")
EXECUTABLE = ("eboot.bin", "es-de.bin", "picker.bin", "lapy-root-daemon.elf", "*.prx", "*.so")


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def check(title, release=False):
    problems = []
    folder = title / "licenses"
    table = folder / "components.json"
    if not table.is_file():
        return [f"{table} is missing; run tools/stage-notices.py"]
    if not (folder / "README.txt").is_file():
        problems.append("licenses/README.txt is missing")
    components = json.loads(table.read_text(encoding="utf-8"))["components"]
    owned = {}
    for component in components:
        for name in component["files"]:
            owned.setdefault(name, []).append(component["id"])
        for record in component.get("text_records", []):
            path = folder / component["id"] / record["file"]
            if not path.exists() or (path.is_file() and path.stat().st_size == 0):
                problems.append(f"{component['id']}: licence text {record['file']} is missing or empty")
            elif "sha256" in record and sha256(path) != record["sha256"]:
                problems.append(f"{component['id']}: {record['file']} is not the verbatim text staged")
        if component["kind"] == "code" and not component["texts"]:
            problems.append(f"{component['id']}: code shipped with no licence text")
        for name, recorded in component["executables"].items():
            if not (title / name).is_file():
                problems.append(f"{component['id']}: {name} is listed but not in the title")
            elif sha256(title / name) != recorded:
                problems.append(f"{component['id']}: {name} changed after its notice was staged")
        if "LicenseRef" in component["licence"] and not component.get("noncommercial"):
            problems.append(f"{component['id']}: custom licence not marked non-commercial or reviewed")
        source = component["source"]
        if release:
            if source.get("dirty"):
                problems.append(f"{component['id']}: built from uncommitted source")
            if not source.get("url", "").startswith("https://"):
                problems.append(f"{component['id']}: no published source address")
    for path in sorted(p for p in title.rglob("*") if p.is_file()):
        name = path.relative_to(title).as_posix()
        if name.startswith("licenses/"):
            continue
        # RPCS3 (GPL-2.0-only) is combined with this port's GPL-3.0 code; until
        # I decide its licence question (docs/RELEASING.md) it is a console
        # build only and never in a release.
        if release and path.name.startswith("rpcs3_libretro"):
            problems.append(f"{name}: RPCS3 is a console build only, not for a release")
        if any(fnmatch.fnmatchcase(name, pattern) for pattern in COVERED) and name not in owned:
            problems.append(f"{name} belongs to no listed part")
        if (any(fnmatch.fnmatchcase(path.name, p) for p in EXECUTABLE)
                and not any(name in c["executables"] for c in components)):
            problems.append(f"{name} has no recorded digest")
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("title", type=Path, nargs="?",
                        help="the title folder (default: the one folder under dist/)")
    parser.add_argument("--release", action="store_true",
                        help="also require committed, published source for every part")
    args = parser.parse_args(argv)
    if args.title is None:
        built = sorted(p for p in Path(__file__).resolve().parent.parent.glob("dist/PPSA[0-9]*")
                       if p.is_dir())
        if len(built) != 1:
            print("error: [notices] name the title folder; dist/ holds "
                  f"{len(built)} of them", file=sys.stderr)
            return 2
        args.title = built[0]
    problems = check(args.title, args.release)
    for problem in problems:
        print(f"  FAIL {problem}", file=sys.stderr)
    if problems:
        print(f"==> [notices] {len(problems)} problem(s) in {args.title}/licenses", file=sys.stderr)
        return 1
    print(f"==> [notices] {args.title}/licenses covers every shipped executable")
    return 0


if __name__ == "__main__":
    sys.exit(main())
