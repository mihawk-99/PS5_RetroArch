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


def do_deploy(settings: dict, tid: str) -> int:
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
            if digest != expected:
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
