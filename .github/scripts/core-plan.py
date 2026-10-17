#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The cores tools/build-title.sh builds, for the workflow's per-core jobs.

  core-plan.py matrix              the core_names list of tools/build-title.sh as a JSON
                                   matrix: [{"core", "script", "stamp", "fork", "revision",
                                   "source"}]; fork and revision only for a core built from a
                                   mihawk-99 fork checkout (tools/core-fork.sh, LRPS2's own)
  core-plan.py prefetch CORE       that fork's pinned commit alone, fetched and checked out in
                                   the tree the build script uses, so it does not clone the whole
                                   history (MAME's is gigabytes); the script still checks it out
                                   at the pin and cleans it when it builds
  core-plan.py skip CORE...        leave those cores out of this checkout's title (CI_SKIP_CORES
                                   in the workflow): their names out of tools/build-title.sh's
                                   core_names and tools/generate-core-metadata.py's CORES, so the
                                   matrix has no job for them and the title neither builds nor
                                   stages them. An edit to the CI's working tree, never committed
  core-plan.py check CORE...|all  every one of those cores has what the title needs from it
                                   (stamp, library, info file, build report, licence texts);
                                   exit 1 naming what is missing. A core job rebuilds a core its
                                   cache restored without them; the title job checks them all
                                   before it builds anything
  core-plan.py collect CORE OUT    what the title job needs from a built core, copied into OUT
                                   at the same paths: build/cores/stage (the library, its info
                                   file, its system/ assets), the stamp that lets the title's
                                   own run of the script skip it, the build and ABI reports
                                   stage-notices.py reads, and the core option sources
                                   tools/generate-core-metadata.py extracts the WebUI's
                                   catalogs from

Each core's script is the one whose core_stamp_skip names <core>_libretro.so, the file
build-title.sh stages.
"""
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"


def core_names():
    text = (TOOLS / "build-title.sh").read_text()
    found = re.search(r"^core_names=\(([^)]*)\)", text, re.M)
    if not found:
        sys.exit("core-plan: no core_names=(...) in tools/build-title.sh")
    return found.group(1).split()


def plan():
    scripts = sorted(TOOLS.glob("build-*.sh"))
    cores = []
    for core in core_names():
        output = f"/build/cores/stage/cores/{core}_libretro.so"
        matches = []
        for script in scripts:
            text = script.read_text()
            stamp = re.search(r"core_stamp_skip\s+([\w.-]+)((?:\s*\\?\n?\s*\"[^\"]*\")+)", text)
            if stamp and output in stamp.group(2):
                matches.append((script, text, stamp.group(1)))
        if len(matches) != 1:
            sys.exit(f"core-plan: {len(matches)} scripts stage {core}_libretro.so")
        script, text, stamp = matches[0]
        entry = {"core": core, "script": script.name, "stamp": stamp}
        fork = re.search(r"core_fork_checkout\s+(PS5_\w+)\s", text) or \
            re.search(r"github\.com/mihawk-99/(PS5_\w+)\.git", text)
        revision = re.search(r"^revision=([0-9a-f]{40})\b", text, re.M)
        if fork and revision:
            name = re.search(r"^core_name=([\w.-]+)", text, re.M)
            source = re.search(r'source_dir="\$root/(\.deps/[\w.-]+-src)"', text)
            entry.update(fork=fork.group(1), revision=revision.group(1),
                         source=source.group(1) if source else f".deps/{name.group(1)}-src")
        cores.append(entry)
    return cores


def find(core):
    for entry in plan():
        if entry["core"] == core:
            return entry
    sys.exit(f"core-plan: {core} is not in tools/build-title.sh's core_names")


def git(*args, cwd=None):
    subprocess.run(["git", *args], cwd=cwd, check=True)


def prefetch(core):
    entry = find(core)
    if "fork" not in entry:
        print(f"==> [{core}] {entry['script']} fetches its own source")
        return
    tree = ROOT / entry["source"]
    if (tree / ".git").is_dir():
        print(f"==> [{core}] {entry['source']} is already there")
        return
    git("init", "-q", str(tree))
    # origin is the published fork: a relative submodule URL (../PS5_Dynarmic) resolves against it
    git("remote", "add", "origin", f"https://github.com/mihawk-99/{entry['fork']}.git", cwd=tree)
    git("fetch", "-q", "--depth", "1", "--no-recurse-submodules", "origin", entry["revision"], cwd=tree)
    # checked out too: a core its stamp finds up to date is not checked out by its script, and
    # collect still takes its licence texts and option sources from the tree
    git("checkout", "-q", "--force", "--detach", entry["revision"], cwd=tree)
    print(f"==> [{core}] {entry['fork']} {entry['revision'][:12]} in {entry['source']}")


def metadata_sources(core):
    """The files generate-core-metadata.py reads for CORE: its source and that folder's others."""
    spec = __import__("importlib.util").util.spec_from_file_location(
        "core_metadata", TOOLS / "generate-core-metadata.py")
    module = __import__("importlib.util").util.module_from_spec(spec)
    spec.loader.exec_module(module)
    files = []
    for stem, _name, source, _flags in module.CORES:
        if stem == core:
            folder = (ROOT / source).parent
            files += [path for path in sorted(folder.iterdir()) if path.is_file()]
    return files


def notice_components(entry):
    """The core's entries in tooling/notices/components.json: those whose artifacts name its
    library. Their build folder (build/cores/<build>/build.json) is not always the stamp's name:
    LRPS2's is lrps2, its stamp pcsx2."""
    table = json.loads((ROOT / "tooling/notices/components.json").read_text(encoding="utf-8"))
    library = f"cores/{entry['core']}_libretro.so"
    return [component for component in table["components"]
            if component.get("source", {}).get("kind") == "core" and library in component.get("artifacts", [])]


def licence_texts(entry, missing=None):
    """The licence texts stage-notices.py copies for this core, from its source tree; a text
    none of whose paths exists goes to missing (or stops the script)."""
    paths = []
    for component in notice_components(entry):
        for text in component.get("texts", []):
            if "from" not in text:
                continue
            choices = text["from"] if isinstance(text["from"], list) else [text["from"]]
            found = next((ROOT / choice for choice in choices if (ROOT / choice).exists()), None)
            if found:
                paths.append(found)
            elif missing is None:
                sys.exit(f"core-plan: {component['id']}'s licence text {choices} is missing")
            else:
                missing.append(choices[0])
    return paths


def required(entry):
    """What the title cannot be made without, for this core: the stamp its script skips by, the
    library and its info file, and the build report stage-notices.py reads
    (build/cores/<build>/build.json)."""
    stage = ROOT / "build/cores/stage"
    files = [ROOT / "build/cores/stamps" / entry["stamp"],
             stage / "cores" / f"{entry['core']}_libretro.so", stage / "info" / f"{entry['core']}_libretro.info"]
    files += [ROOT / "build/cores" / component["source"]["build"] / "build.json"
              for component in notice_components(entry)]
    return files


def missing_of(entry):
    missing = [str(path.relative_to(ROOT)) for path in required(entry) if not path.is_file()]
    licence_texts(entry, missing)
    return missing


def check(cores):
    """Every core's required files and licence texts are here; exit 1 naming what is not."""
    bad = 0
    for core in cores:
        missing = missing_of(find(core))
        if missing:
            bad += 1
            print(f"core-plan: {core} is missing {', '.join(missing)}", file=sys.stderr)
    if bad:
        sys.exit(1)
    print(f"==> [cores] {len(cores)} cores have their libraries, stamps, build reports and licences")


def collect(core, out):
    entry = find(core)
    out = Path(out).resolve()
    stage = ROOT / "build/cores/stage"
    files = required(entry) + sorted((stage / "cores").glob(f"{core}_libretro.*")) + \
        sorted((stage / "info").glob(f"{core}_libretro.*"))
    # the ABI reports beside the build reports, in the folder a script names after its stamp, its
    # core or its notice entry
    builds = {component["source"]["build"] for component in notice_components(entry)}
    for folder in sorted({entry["stamp"], core} | builds):
        files += [ROOT / "build/cores" / folder / "abi.json"]
    files = [path for path in dict.fromkeys(files) if path.is_file() or path in required(entry)]
    files += metadata_sources(core)
    folders = [stage / "system"] if (stage / "system").is_dir() else []
    for path in licence_texts(entry):
        (folders if path.is_dir() else files).append(path)
    for path in files:
        if not path.is_file():
            sys.exit(f"core-plan: {core} did not leave {path.relative_to(ROOT)}")
        target = out / path.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
    # folders: assets a core stages under build/cores/stage/system (PPSSPP, Dolphin; only this
    # job's core is there), and licence folders
    for path in folders:
        shutil.copytree(path, out / path.relative_to(ROOT), dirs_exist_ok=True)
    print(f"==> [{core}] {len(files)} files and {len(folders)} folders collected in {out}")


def skip(cores):
    known = core_names()
    for core in cores:
        if core not in known:
            sys.exit(f"core-plan: {core} is not in tools/build-title.sh's core_names")
    title = TOOLS / "build-title.sh"
    text = title.read_text()
    found = re.search(r"^core_names=\(([^)]*)\)", text, re.M)
    kept = [core for core in found.group(1).split() if core not in cores]
    title.write_text(text[:found.start(1)] + " ".join(kept) + text[found.end(1):])
    metadata = TOOLS / "generate-core-metadata.py"
    lines = metadata.read_text().splitlines(keepends=True)
    entry = re.compile(r"^\s*\('(" + "|".join(map(re.escape, cores)) + r")',")
    metadata.write_text("".join(line for line in lines if not entry.match(line)))
    print(f"==> [cores] left out of this build: {' '.join(cores)}; {len(kept)} cores")


def main(argv):
    if argv[:1] == ["matrix"] and len(argv) == 1:
        print(json.dumps(plan()))
    elif argv[:1] == ["prefetch"] and len(argv) == 2:
        prefetch(argv[1])
    elif argv[:1] == ["skip"]:
        if argv[1:]:
            skip(argv[1:])
    elif argv[:1] == ["check"] and len(argv) >= 2:
        check(argv[1:] if argv[1:] != ["all"] else [entry["core"] for entry in plan()])
    elif argv[:1] == ["collect"] and len(argv) == 3:
        collect(argv[1], argv[2])
    else:
        sys.exit("usage: core-plan.py matrix | prefetch CORE | skip CORE... | check CORE...|all | collect CORE OUT")


if __name__ == "__main__":
    main(sys.argv[1:])
