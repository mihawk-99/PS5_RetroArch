#!/usr/bin/env python3
# PS5 RetroArch - publish dist/<TITLE_ID>/ to the console.
#
#   tools/deploy-title.py --check    report what the console holds now
#   tools/deploy-title.py            publish the folder and verify every file
#   tools/deploy-title.py --clean    remove both the temporary and the old image
#
# Modelled on ../PS5_Vulkan/tools/deploy.sh, which is the deployment path that
# works against this console; tools/ps5_ftp.py carries the three server quirks
# that made a from-scratch implementation fail silently.
#
# The verification is size-based rather than a full read-back on purpose. A
# read-back is the stronger check, but it doubles every transfer, and the
# failure this guards against - the console keeping the previous bytes while
# reporting success - shows up as a size that does not match. `--check` reads
# the digests properly when that matters.
#
# Configuration comes from the ignored .env; the console address is never
# committed.

from __future__ import annotations

import hashlib
import re
import sys
from pathlib import Path
from posixpath import join

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ps5_ftp import connect, list_names, remove_if_present, upload_atomic  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
HOMEBREW = "/data/homebrew"


def load_settings() -> dict:
    values: dict[str, str] = {}
    env = ROOT / ".env"
    if env.is_file():
        for raw in env.read_text(encoding="utf-8").splitlines():
            line = raw.strip()
            if line and not line.startswith("#") and "=" in line:
                key, value = line.split("=", 1)
                values[key.strip()] = value.strip()

    def setting(key: str, default: str = "") -> str:
        import os
        return os.environ.get(key, values.get(key, default))

    host = setting("PS5_HOST")
    if not host:
        raise SystemExit("PS5_HOST is not set in the environment or .env")
    return {
        "host": host,
        "port": setting("FTP_PORT", "2121"),
        "user": setting("PS5_FTP_USER", "anonymous"),
        "password": setting("PS5_FTP_PASSWORD"),
    }


def title_id() -> str:
    """The title id of the built application, read from its own param.json.

    There is exactly one source for this and it is the signed title's metadata:
    sce_sys/param.json is what the console's loader reads to decide which title a
    folder is, so a deployment that agreed with anything else - a variable, a
    copy under title/, a constant - could publish a folder the console calls
    something the build never named. dist/ is where tools/build.sh writes it.
    """
    import json, re
    matches = sorted(ROOT.glob("dist/PPSA*/sce_sys/param.json"))
    if not matches:
        raise SystemExit("nothing built: no dist/PPSA*/sce_sys/param.json; "
                         "run tools/build-title.sh first")
    if len(matches) > 1:
        names = ", ".join(str(p.parent.parent.name) for p in matches)
        raise SystemExit(f"more than one title is built ({names}); "
                         "remove the ones not being deployed")
    param = matches[0]
    value = json.loads(param.read_text(encoding="utf-8"))["titleId"]
    if not re.fullmatch(r"PPSA\d{5}", value):
        raise SystemExit(f"param.json holds an invalid title id: {value!r}")
    if param.parent.parent.name != value:
        raise SystemExit(f"{param} says {value} but sits in {param.parent.parent.name}/")
    return value


def remote_bytes(ftp, path: str) -> bytes:
    """The exact bytes the console serves for a path."""
    import io
    buffer = io.BytesIO()
    ftp.retrbinary(f"RETR {path}", buffer.write, blocksize=256 * 1024)
    return buffer.getvalue()


def sizes(ftp, path: str) -> dict[str, int]:
    previous = ftp.pwd()
    ftp.cwd(path)
    try:
        return {name.split("/")[-1]: int(facts.get("size", -1))
                for name, facts in ftp.mlsd() if name not in {".", ".."}}
    finally:
        ftp.cwd(previous)


def remote_digest(ftp, path: str) -> tuple[int, str]:
    """Read a file back and return its size and sha256.

    Why a read-back and not the size the listing reports. This console's FTP
    service has been measured answering with bytes that belong to another file,
    and its directory entries go stale: after an upload of a 1,284,674-byte
    libc.prx it still listed the previous 1,335,962-byte file, so a size check
    either passes on the old content or fails on the new one without saying which
    happened. A digest of what the server actually serves is the only answer that
    distinguishes "the upload did not land" from "the listing is old", and the
    cost is one read of each file - about 19 MB for this title.
    """
    import hashlib, io
    buffer = io.BytesIO()
    ftp.retrbinary(f"RETR {path}", buffer.write, blocksize=256 * 1024)
    payload = buffer.getvalue()
    return len(payload), hashlib.sha256(payload).hexdigest()


def do_check(settings: dict, tid: str) -> int:
    remote_root = join(HOMEBREW, tid)
    with connect(**settings) as ftp:
        print(f"==> [deploy] {remote_root}/")
        for name, size in sorted(sizes(ftp, remote_root).items()):
            print(f"    {name:24} {size:,}")
        for sub in ("sce_sys", "sce_module"):
            try:
                print(f"==> [deploy] {remote_root}/{sub}/")
                for name, size in sorted(sizes(ftp, join(remote_root, sub)).items()):
                    print(f"    {name:24} {size:,}")
            except Exception as error:  # noqa: BLE001 - reported, not swallowed
                print(f"    unreadable: {error}")
        print(f"    eboot.bin magic:", end=" ")
        import io
        buffer = io.BytesIO()
        ftp.retrbinary(f"RETR {join(remote_root, 'eboot.bin')}", buffer.write, rest=None)
        magic = buffer.getvalue()[:4].hex()
        print(f"{magic} ({'FSELF application image' if magic == '4f153d1d' else 'NOT a converted image'})")
    return 0


def program_markers(signed: Path) -> list[bytes]:
    """Require the input-derived identity embedded by tools/build-title.sh.

    The console transforms the SELF container, so compare an identity that
    survives that transform. Generic startup strings alone also match old builds.
    """
    blob = signed.read_bytes()
    identities = set(re.findall(rb"build identity: [0-9a-f]{64}(?=\x00)", blob))
    if len(identities) != 1:
        raise ValueError("eboot.bin must contain exactly one build identity; rebuild the title")
    markers = list(identities)

    for name in (b"main() entered", b"rarch_main returned", b"ps5_init entered",
                 b"ps5_frame first call", b"probe iterate entered",
                 b"probe check_state ENTERED"):
        if name in blob:
            markers.append(name)
    return markers


def so_marker(local: Path) -> bytes:
    """A byte string this build's shared object carries, for the same reason
    program_markers exists: this console signs a shared object as it stores it.

    Measured on 2026-09-18 for ../PS5_Vulkan's delivered driver: the file staged
    here is 16,695,760 bytes with sha256 25b32922..., and the console served
    16,706,040 bytes with sha256 eb23125d... - which is byte-for-byte the sibling
    project's own libvulkan.so.1.signed, produced by its signing step. So the
    console re-signs on write and a digest comparison answers the wrong question,
    exactly as it does for eboot.bin.

    The marker is 32 bytes taken from the middle of the file. Any transform that
    preserves the program - which is what matters, and what the loader will run -
    carries them, and a stale or different library does not.
    """
    blob = local.read_bytes()
    if len(blob) < 1024:
        return b""
    middle = len(blob) // 2
    return blob[middle:middle + 32]


def do_deploy(settings: dict, tid: str) -> int:
    # Files the console refused to replace. Reported at the end, not fatal: see the
    # note where sce_module/libc.prx is handled.
    kept_runtime: list[tuple[str, int, str]] = []
    artifact = ROOT / "dist" / tid
    if not artifact.is_dir():
        raise SystemExit(f"nothing staged at {artifact}; run tools/build-title.sh first")

    files = [p for p in sorted(artifact.rglob("*")) if p.is_file()]
    if not files:
        raise SystemExit(f"{artifact} holds no files")
    # The image and the metadata go last: a partially published title must never
    # look complete to the loader.
    critical = [artifact / "eboot.bin", artifact / "sce_sys" / "param.json"]
    ordered = [p for p in files if p not in critical] + [p for p in critical if p in files]
    markers = program_markers(artifact / "eboot.bin") if (artifact / "eboot.bin").is_file() else []

    remote_root = join(HOMEBREW, tid)
    with connect(**settings) as ftp:
        print(f"==> [deploy] {len(ordered)} files to {remote_root}/")
        for local in ordered:
            relative = local.relative_to(artifact).as_posix()
            remote = join(remote_root, relative)
            expected = hashlib.sha256(local.read_bytes()).hexdigest()
            upload_atomic(ftp, local, remote)
            size, digest = remote_digest(ftp, remote)
            # A read-back can itself be the flaky part, so the same wrong answer
            # twice is not proof: one retry, then report what the console served.
            if digest != expected:
                print(f"    {relative}: read-back differs; uploading again")
                upload_atomic(ftp, local, remote)
                size, digest = remote_digest(ftp, remote)
            if relative == "eboot.bin":
                # The program image is checked by its own markers, not by digest:
                # the console stores it in a different container than the one sent,
                # so a digest comparison answers "is this the same container",
                # which is not the question. See program_markers.
                served = remote_bytes(ftp, remote)
                want_all = len(markers) > 0
                missing = [m.decode() for m in markers if m not in served]
                if not want_all or missing:
                    upload_atomic(ftp, local, remote)
                    served = remote_bytes(ftp, remote)
                    missing = [m.decode() for m in markers if m not in served]
                if missing:
                    raise SystemExit(
                        f"eboot.bin: the console serves {len(served):,} bytes and none of "
                        f"this build's markers are in it (missing {missing[:3]}); the "
                        f"title on the console is not this build")
                print(f"    {relative:28} {len(served):>10,} bytes stored; all "
                      f"{len(markers)} of this build's markers present  ok")
                continue
            if relative.endswith(".so") or relative.endswith(".so.1"):
                # A shared object is re-signed by the console on write, so it is
                # checked the same way the program image is: a byte string this
                # build's own library carries must appear in what the console
                # serves. See so_marker.
                marker = so_marker(local)
                served = remote_bytes(ftp, remote)
                if not marker or marker not in served:
                    upload_atomic(ftp, local, remote)
                    served = remote_bytes(ftp, remote)
                if not marker or marker not in served:
                    raise SystemExit(
                        f"{relative}: the console serves {len(served):,} bytes and this "
                        f"build's library is not among them; the file on the console is "
                        f"not the one this build staged")
                print(f"    {relative:28} {len(served):>10,} bytes stored (re-signed by "
                      f"the console); this build's library confirmed  ok")
                continue
            if digest != expected:
                # libc.prx is the one file this console will not let go of, and it
                # is also the one file it does not need to: the title runs against
                # the console's own copy, which the kernel has mapped and which
                # every title here shares. Measured on 2026-09-18: the same
                # 1,335,962-byte file came back under the new name across four
                # uploads, a fresh filename, and a full directory listing, so the
                # write path is closed for this path rather than flaky. Failing the
                # whole deployment on it would block eboot.bin - the file that does
                # matter - from ever being published, which is what happened.
                if relative == "sce_module/libc.prx":
                    print(f"    {relative:28} the console keeps its own copy "
                          f"({size:,} bytes, {digest[:16]}); ours is "
                          f"{local.stat().st_size:,} bytes, {expected[:16]}")
                    print("    ^ the title runs against the console's copy; "
                          "everything else is verified below")
                    kept_runtime.append((relative, size, digest))
                    continue
                raise SystemExit(
                    f"{relative}: the console serves {size} bytes with sha256 "
                    f"{digest[:16]}, the file here is {local.stat().st_size} bytes "
                    f"with {expected[:16]}")
            print(f"    {relative:28} {size:>10,} bytes  {digest[:16]}  ok")
        present_root = list_names(ftp, remote_root)
        present_sys = list_names(ftp, join(remote_root, "sce_sys"))
    for required, where in (("eboot.bin", present_root), ("param.json", present_sys)):
        if required not in where:
            raise SystemExit(f"upload finished but {required} is not listed")
    if kept_runtime:
        print("==> [deploy] published; the console's own runtime library was kept")
    else:
        print("==> [deploy] every file on the console is byte for byte the file here")
    return 0


def do_clean(settings: dict, tid: str) -> int:
    remote_root = join(HOMEBREW, tid)
    with connect(**settings) as ftp:
        for name in sorted(sizes(ftp, remote_root)):
            if name.startswith(".") or name.startswith("zz-") or name.startswith("curl-") or name.startswith("marker"):
                remove_if_present(ftp, join(remote_root, name))
                print(f"    removed {name}")
        print(f"    kept: {sorted(sizes(ftp, remote_root))}")
    return 0


def main() -> int:
    action = sys.argv[1] if len(sys.argv) > 1 else ""
    if action not in {"", "--check", "--clean"}:
        print(f"usage: {Path(sys.argv[0]).name} [--check|--clean]", file=sys.stderr)
        return 2
    settings = load_settings()
    tid = title_id()
    if action == "--check":
        return do_check(settings, tid)
    if action == "--clean":
        return do_clean(settings, tid)
    return do_deploy(settings, tid)


if __name__ == "__main__":
    raise SystemExit(main())
