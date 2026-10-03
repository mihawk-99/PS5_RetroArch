#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Mihawk
"""Archive the corresponding source of a staged title, one archive per part.

    python3 tools/source-bundle.py dist/PPSA99169 OUT [--only a,b] [--skip a,b] [--list]
                                   [--allow-dirty]

It reads the title's licenses/components.json (tools/stage-notices.py) and writes, for
each part that is code, the source at the revision recorded there:

  a git source      git archive of that revision, with every submodule the build
                    checked out, each at the commit the revision records for it
  a core's tarball  the pinned tarball the build verified (build.json's digest)
  RetroArch         vendor/retroarch, the pristine fetched tree, checked against its pin

plus SHA256SUMS and SOURCES.txt, which say what each archive is. These are meant to be
attached to the release beside the title ZIP (docs/RELEASING.md), so the source is
offered from the same place as the binary. A part whose repository has uncommitted
changes is refused unless --allow-dirty is given: its committed revision is not what
was built. --list prints the plan without writing anything.
"""

import argparse
import hashlib
import io
import json
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TABLE = ROOT / "tooling/notices/components.json"


class BundleError(Exception):
    pass


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def git(path, *args, binary=False):
    result = subprocess.run(["git", "-C", str(path), *args], check=True, capture_output=True)
    return result.stdout if binary else result.stdout.decode().strip()


def pinned(spec, root):
    text = (root / spec["file"]).read_text(encoding="utf-8")
    found = re.search(r'^\s*' + re.escape(spec["var"]) + r'="?([^"\s#]+)', text, re.M)
    if not found:
        raise BundleError(f"no {spec['var']}= in {spec['file']}")
    return found.group(1)


def submodules(tree, revision, prefix=""):
    """(path, commit, checkout) for each submodule initialised in TREE, recursively."""
    found = []
    listing = git(tree, "ls-tree", "-r", revision)
    for line in listing.splitlines():
        meta, path = line.split("\t", 1)
        mode, kind, commit = meta.split()
        if kind != "commit":
            continue
        checkout = tree / path
        if not (checkout / ".git").exists():
            continue  # not checked out, so not part of what was built
        found.append((prefix + path, commit, checkout))
        found += submodules(checkout, commit, prefix + path + "/")
    return found


def git_archive(tree, revision, output, name, paths=(), with_submodules=True):
    """One tar.gz: TREE at REVISION under NAME/, submodules appended at their paths."""
    with tarfile.open(output, "w:gz") as bundle:
        parts = [("", revision, tree)]
        if with_submodules:
            parts += submodules(tree, revision)
        for path, commit, checkout in parts:
            prefix = f"{name}/{path}/" if path else f"{name}/"
            raw = git(checkout, "archive", "--format=tar", f"--prefix={prefix}", commit,
                      *(paths if not path else ()), binary=True)
            with tarfile.open(fileobj=io.BytesIO(raw)) as part:
                for member in part.getmembers():
                    if member.type == tarfile.XGLTYPE or member.name == "pax_global_header":
                        continue
                    bundle.addfile(member, part.extractfile(member) if member.isfile() else None)
    return len(parts) - 1


def plan(component, root):
    """The archives to write for one part: a list of (filename, action, description)."""
    source = component["source"]
    kind, revision = source["kind"], source["revision"]
    short = revision[:12]
    cid = component["id"]
    spec = next(c for c in json.loads(TABLE.read_text())["components"] if c["id"] == cid)["source"]
    steps = []
    if kind in ("git", "stamp", "provenance") or (kind == "core" and "tree" in spec):
        if kind == "git":
            tree = root / spec["path"].replace("${VULKAN}", "../PS5_Vulkan")
        elif kind == "stamp":
            tree = root / "../PS5_PayloadSDK"
        elif kind == "provenance":
            tree = root / ("../PS5_Mesa" if cid == "radv" else "../ps5-opengl-sdk-0.3.0")
        else:
            tree = root / spec["tree"]
        # Parts from one repository share one archive, named after the repository.
        name = cid if kind == "core" else tree.resolve().name
        steps.append((f"{name}-{short}.tar.gz", ("git", tree, revision, ()), source.get("url", "")))
    elif kind == "core":
        digest = source.get("source_archive_sha256")
        matches = [p for p in (root / ".deps/downloads").glob(f"*-{revision}.tar.gz")
                   if digest and sha256(p) == digest]
        if not matches:
            raise BundleError(f"{cid}: no downloaded tarball with the digest build.json records")
        steps.append((matches[0].name, ("copy", matches[0]), source.get("url", "")))
    elif kind == "pin" and cid == "retroarch":
        stamp = (root / "vendor/retroarch/.rarch-revision").read_text().strip()
        if stamp != revision:
            raise BundleError(f"vendor/retroarch is at {stamp}, the pin is {revision}")
        steps.append((f"retroarch-{short}.tar.gz", ("tree", root / "vendor/retroarch"), source.get("url", "")))
    elif kind == "pin" and cid == "zlib":
        tarball = root / f".deps/native/zlib/zlib-{revision}.tar.gz"
        steps.append((tarball.name, ("copy", tarball), source.get("url", "")))
    elif kind == "pin" and cid == "libmicrohttpd":
        version = pinned({"file": "tools/build-webui-http.sh", "var": "version"}, root)
        tarball = root / f".deps/webui/libmicrohttpd-{version}.tar.gz"
        if sha256(tarball) != revision:
            raise BundleError("libmicrohttpd: source archive differs from the build pin")
        steps.append((tarball.name, ("copy", tarball), source.get("url", "")))
    else:
        return []  # a part with no source to archive here (see SOURCES.txt)
    for extra in spec.get("extra", []):
        if "tarball" in extra:
            tarball = root / extra["tarball"]
            steps.append((tarball.name, ("copy", tarball), f"used by {cid}"))
        else:
            rev = pinned(extra["pin"], root)
            steps.append((f"{extra['name']}-{rev[:12]}.tar.gz",
                          ("git", root / extra["git"], rev, tuple(extra.get("paths", ()))),
                          f"{extra.get('remote', '')}/tree/{rev} (used by {cid})"))
    return steps


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("title", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--only", default="")
    parser.add_argument("--skip", default="")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--allow-dirty", action="store_true")
    args = parser.parse_args(argv)
    staged = json.loads((args.title / "licenses/components.json").read_text(encoding="utf-8"))
    only = set(filter(None, args.only.split(",")))
    skip = set(filter(None, args.skip.split(",")))
    written, index, done = [], [], set()
    try:
        for component in staged["components"]:
            cid = component["id"]
            if component["kind"] != "code" or (only and cid not in only) or cid in skip:
                continue
            if component["source"].get("dirty") and not args.allow_dirty:
                raise BundleError(f"{cid}: built from uncommitted source (commit it, or --allow-dirty)")
            steps = plan(component, ROOT)
            if not steps:
                # Nothing archived here: say where the source is, so the index is complete.
                index.append(f"-\t{cid}\t{component['source'].get('url', component['source']['revision'])}")
            for filename, action, where in steps:
                if filename in done:
                    continue
                done.add(filename)
                index.append(f"{filename}\t{cid}\t{where}")
                if args.list:
                    print(f"{cid:18} {filename}  <- {action[0]} {action[1]}")
                    continue
                args.out.mkdir(parents=True, exist_ok=True)
                target = args.out / filename
                if action[0] == "copy":
                    shutil.copyfile(action[1], target)
                elif action[0] == "tree":
                    with tarfile.open(target, "w:gz") as bundle:
                        bundle.add(action[1], arcname=filename[:-len(".tar.gz")],
                                   filter=lambda m: None if m.name.endswith(".rarch-revision") else m)
                else:
                    count = git_archive(action[1], action[2], target, filename[:-len(".tar.gz")],
                                        action[3])
                    print(f"==> [source] {filename}: {count} submodules")
                written.append(target)
    except (BundleError, OSError, subprocess.CalledProcessError, StopIteration) as error:
        print(f"error: [source] {error}", file=sys.stderr)
        return 2
    if args.list:
        return 0
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "SOURCES.txt").write_text(
        "archive\tpart\tpublished at\n" + "\n".join(index) + "\n", encoding="utf-8")
    with open(args.out / "SHA256SUMS", "w", encoding="utf-8") as sums:
        for path in sorted(args.out.glob("*.tar*")):
            sums.write(f"{sha256(path)}  {path.name}\n")
    print(f"==> [source] {len(written)} archives in {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
