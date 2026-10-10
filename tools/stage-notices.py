#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Mihawk
"""Write licenses/ into a built title folder.

    python3 tools/stage-notices.py dist/PPSA99169 [--driver radv|ps5vk] [--release-tag TAG]

Every part of the title is listed in tooling/notices/components.json with its licence,
the licence texts that must travel with it and where its source is. This copies those
texts from the source trees the build used, and writes two files beside them:

  licenses/components.json  each part's licence, source revision and URL, and the
                            sha256 of each executable file it went into
  licenses/README.txt       the same for a person reading the folder

A core is tied to its source by its build report (build/cores/<core>/build.json): the
digest recorded there must be the digest of the staged file, or this fails. RADV is tied
to PS5_Mesa by the release archive's PROVENANCE.txt, whose archive digest is checked.
tools/check-notices.py verifies the result; tools/source-bundle.py archives the sources.
"""

import argparse
import fnmatch
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TABLE = ROOT / "tooling/notices/components.json"
# The title's executables: eboot.bin, the frontends beside it, modules and cores.
EXECUTABLE = ("eboot.bin", "es-de.bin", "picker.bin", "lapy-root-daemon.elf", "*.prx", "*.so")


class NoticeError(Exception):
    pass


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def expand(text, tokens):
    for name, value in tokens.items():
        text = text.replace("${" + name + "}", value)
    return text


def resolve(path, tokens, root):
    candidate = Path(expand(path, tokens))
    return candidate if candidate.is_absolute() else root / candidate


def title_files(title):
    return sorted(p.relative_to(title).as_posix() for p in title.rglob("*")
                  if p.is_file() and not p.relative_to(title).as_posix().startswith("licenses/"))


def matches(pattern, files):
    return [f for f in files if fnmatch.fnmatchcase(f, pattern)]


def git(path, *args):
    return subprocess.run(["git", "-C", str(path), *args], check=True, text=True,
                          capture_output=True).stdout.strip()


def source_of(component, tokens, root, title):
    """The revision and URL of a component's source, and whether it is committed."""
    spec = component["source"]
    kind = spec["kind"]
    info = {"kind": kind}
    if kind == "git":
        path = resolve(spec["path"], tokens, root)
        info["revision"] = git(path, "rev-parse", "HEAD")
        # Tracked changes only: ignored build output is not source.
        info["dirty"] = bool(git(path, "status", "--porcelain", "--untracked-files=no"))
    elif kind == "pin":
        text = resolve(spec["file"], tokens, root).read_text(encoding="utf-8")
        found = re.search(r'^\s*' + re.escape(spec["var"]) + r'="?([^"\s#]+)', text, re.M)
        if not found:
            raise NoticeError(f"{component['id']}: no {spec['var']}= in {spec['file']}")
        info["revision"] = found.group(1)
        info["pinned_in"] = spec["file"]
    elif kind == "stamp":
        # The revision the installed tree records, which is what was linked; the pin
        # that installs it must agree, or the build used something else.
        info["revision"] = resolve(spec["file"], tokens, root).read_text().split()[0]
        pinned = source_of({"id": component["id"], "source": {"kind": "pin", **spec["pin"]}},
                           tokens, root, title)["revision"]
        if pinned != info["revision"]:
            raise NoticeError(f"{component['id']}: {spec['file']} records {info['revision']}, "
                              f"but {spec['pin']['file']} pins {pinned}; rerun the build")
        info["pinned_in"] = spec["pin"]["file"]
    elif kind == "json":
        data = json.loads(resolve(spec["file"], tokens, root).read_text(encoding="utf-8"))
        info["revision"] = data[spec["key"]]
    elif kind == "provenance":
        text = resolve(spec["file"], tokens, root).read_text(encoding="utf-8")
        fields = dict(line.split(":", 1) for line in text.splitlines() if ":" in line)
        fields = {k.strip(): v.strip() for k, v in fields.items()}
        info["revision"] = fields[spec["key"]].split()[0]
        if "archive" in spec:
            archive = resolve(spec["archive"], tokens, root)
            recorded = fields.get("archive sha256")
            if recorded and sha256(archive) != recorded:
                raise NoticeError(f"{component['id']}: {archive} is not the archive its "
                                  "PROVENANCE.txt describes")
            info["archive_sha256"] = recorded
    elif kind == "core":
        report = json.loads((root / "build/cores" / spec["build"] / "build.json").read_text())
        staged = [f for f in component["artifacts"] if f.endswith(".so")]
        for name in staged:
            path = title / name
            if path.is_file() and sha256(path) != report["sha256"]:
                raise NoticeError(f"{component['id']}: {name} is not the file "
                                  f"build/cores/{spec['build']}/build.json describes")
        info["revision"] = report["source_revision"]
        for key in ("source_archive_sha256", "source_submodules", "port_inputs_sha256"):
            if report.get(key):
                info[key] = report[key]
    elif kind == "fixed":
        info["revision"] = spec["revision"]
    else:
        raise NoticeError(f"{component['id']}: unknown source kind {kind}")
    if "url" in spec:
        info["url"] = spec["url"].replace("{rev}", info["revision"])
    elif "remote" in spec:
        info["url"] = f"{spec['remote']}/tree/{info['revision']}"
    return info


def copy_text(entry, tokens, root, target):
    """Copy one licence text (a file, a directory or a tarball member)."""
    destination = target / entry["to"]
    destination.parent.mkdir(parents=True, exist_ok=True)
    if "tar" in entry:
        archive = resolve(entry["tar"], tokens, root)
        with tarfile.open(archive) as tar:
            member = tar.extractfile(entry["member"])
            if member is None:
                raise NoticeError(f"{archive} has no {entry['member']}")
            destination.write_bytes(member.read())
        return f"{entry['tar']}:{entry['member']}"
    choices = entry["from"] if isinstance(entry["from"], list) else [entry["from"]]
    for choice in choices:
        source = resolve(choice, tokens, root)
        if source.is_dir():
            shutil.copytree(source, destination)
            return choice
        if source.is_file():
            if "head" in entry:
                lines = source.read_text(encoding="utf-8", errors="replace").splitlines(True)
                destination.write_text("".join(lines[:entry["head"]]), encoding="utf-8")
            else:
                shutil.copyfile(source, destination)
            return choice
    raise NoticeError(f"licence text not found: {' or '.join(choices)}")


def legal_notice():
    """The title's legal notice: no piracy, RPCS3 only built from source."""
    return (Path(__file__).resolve().parent.parent / "config" / "LEGAL.txt").read_text(encoding="utf-8")


def readme(components, tag):
    lines = [
        "PS5 RetroArch - licences, notices and sources",
        "=" * 45,
        "",
        f"Release: {tag or 'a development build (no release tag given)'}",
        "",
        # The legal notice first (config/LEGAL.txt, also LEGAL.txt beside eboot.bin)
        *legal_notice().splitlines()[3:],
        "",
        "This folder lists every part of this title, the licence each is under, the",
        "licence texts those licences require to travel with it, and the exact source",
        "revision each part was built from. components.json holds the same, with the",
        "sha256 of every executable file.",
        "",
        "Corresponding source: each part's source is published at the address given",
        "for it below, at the revision given. The release page this title was",
        "downloaded from also carries the release's source archives (made by",
        "tools/source-bundle.py in PS5_RetroArch), including this port's patches and",
        "the scripts that build and install the title (tools/build-title.sh).",
        "",
        "Not included: no games, BIOS files, console firmware or decryption keys. The",
        "emulators need the ones you supply from your own hardware and media.",
        "",
    ]
    noncommercial = [c for c in components if c.get("noncommercial")]
    if noncommercial:
        lines += ["Non-commercial terms", "--------------------",
                  "These parts may not be sold or used commercially under their own licences:",
                  *[f"  - {c['name']} ({c['licence']})" for c in noncommercial], ""]
    lines += ["Parts", "-----", ""]
    for c in components:
        source = c["source"]
        lines += [
            c["name"],
            f"  kind:      {c['kind']}",
            f"  licence:   {c['licence']}",
            *[f"  copyright: {line}" for line in c.get("copyright", [])],
            f"  texts:     {', '.join(c['id'] + '/' + t for t in c['texts']) or '-'}",
            f"  source:    {source.get('url', source['revision'])}",
            f"  revision:  {source['revision']}" + ("  (uncommitted changes)" if source.get("dirty") else ""),
            f"  changes:   {c.get('modifications', '-')}",
            f"  files:     {', '.join(sorted(c['files'])[:6])}" + (" ..." if len(c["files"]) > 6 else ""),
            "",
        ]
    lines += ["PlayStation and PS5 are trademarks of Sony Interactive Entertainment. This",
              "is an independent homebrew project, not affiliated with or endorsed by Sony",
              "Interactive Entertainment, the Khronos Group or the libretro project.", ""]
    return "\n".join(lines)


def stage(title, driver, tag, tokens, root=ROOT, table=TABLE):
    """Write licenses/; on any failure leave no partial folder behind."""
    target = title / "licenses"
    try:
        return write(title, driver, tag, tokens, root, table, target)
    except BaseException:
        shutil.rmtree(target, ignore_errors=True)
        raise


def write(title, driver, tag, tokens, root, table, target):
    table = json.loads(Path(table).read_text(encoding="utf-8"))
    files = title_files(title)
    if target.exists():
        shutil.rmtree(target)
    target.mkdir()
    staged = []
    for component in table["components"]:
        if component.get("driver") not in (None, driver):
            continue
        present = sorted({f for p in component["artifacts"] for f in matches(p, files)})
        if not present and not component.get("always"):
            continue
        if not present:
            raise NoticeError(f"{component['id']}: none of {component['artifacts']} is in {title}")
        texts = []
        folder = target / component["id"]
        folder.mkdir()
        for entry in component["texts"]:
            origin = copy_text(entry, tokens, root, folder)
            record = {"file": entry["to"], "from": origin}
            if entry.get("verbatim"):
                record["sha256"] = sha256(folder / entry["to"])
            texts.append(record)
        executables = {f: sha256(title / f) for f in present
                       if any(fnmatch.fnmatchcase(Path(f).name, p) for p in EXECUTABLE)}
        staged.append({
            "id": component["id"], "name": component["name"], "kind": component["kind"],
            "licence": component["licence"], "noncommercial": bool(component.get("noncommercial")),
            "copyright": component.get("copyright", []), "modifications": component.get("modifications"),
            "texts": [t["file"] for t in texts], "text_records": texts,
            "source": source_of(component, tokens, root, title),
            "files": present, "executables": executables,
        })
    (target / "components.json").write_text(
        json.dumps({"schema": 1, "release": tag, "driver": driver, "components": staged},
                   indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (target / "README.txt").write_text(readme(staged, tag), encoding="utf-8")
    return staged


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("title", type=Path)
    parser.add_argument("--driver", default=os.environ.get("PS5_VULKAN_DRIVER", "radv"),
                        choices=("radv", "ps5vk"))
    parser.add_argument("--release-tag", default="")
    parser.add_argument("--vulkan-dir", default=os.environ.get("PS5_VULKAN_DIR", "../PS5_Vulkan"))
    parser.add_argument("--sdk-fork", default="../PS5_PayloadSDK")
    parser.add_argument("--llvm", default=".deps/llvm-project")
    args = parser.parse_args(argv)
    tokens = {"VULKAN": args.vulkan_dir, "SDK_FORK": args.sdk_fork, "LLVM": args.llvm}
    try:
        staged = stage(args.title.resolve(), args.driver, args.release_tag, tokens)
    except (NoticeError, OSError, KeyError, subprocess.CalledProcessError) as error:
        print(f"error: [notices] {error}", file=sys.stderr)
        return 2
    dirty = [c["id"] for c in staged if c["source"].get("dirty")]
    print(f"==> [notices] {len(staged)} parts, licences in {args.title}/licenses")
    if dirty:
        print(f"==> [notices] uncommitted source in: {', '.join(dirty)} "
              "(a release needs committed, published source)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
