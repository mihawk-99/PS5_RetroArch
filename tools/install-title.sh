#!/usr/bin/env bash
# PS5 RetroArch - put the converted application image where the console reads it.
#
#   tools/install-title.sh --check     report what is on the console now
#   tools/install-title.sh             upload dist/<TITLE_ID>/eboot.bin
#
# Why this exists. A PPSA title's folder is mounted onto /system_ex/app/<TITLE_ID>
# by the console's own tooling, and what the loader runs from there is eboot.bin.
# That file must be the *converted, signed* application image: a raw link-stage
# ELF of the same size is not it, and the loader faults inside it before main()
# ever runs (measured: SIGSEGV, page fault at 0x1, thread "eboot.bin", with
# /app0/sce_module/libc.prx named on the stack).
#
# The previous image is kept beside it as eboot.bin.previous, so this is
# reversible: a title that worked before this command still works after it.
#
# The console address and credentials come from the ignored .env.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

title_id=$(python3 -c 'import json,sys; print(json.load(open("title/sce_sys/param.json"))["titleId"])')
remote_base="/data/homebrew/$title_id"
local_image="$root/dist/$title_id/eboot.bin"
mode=${1:-install}

say() { printf '==> [install] %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

[[ -f .env ]] || die "no .env; set PS5_HOST in it (docs/DEPLOYMENT.md)"
set -a; . ./.env; set +a
[[ -n ${PS5_HOST:-} ]] || die "PS5_HOST is not set in .env"

python3 - "$title_id" "$local_image" "$mode" "${PS5_HOST}" "${FTP_PORT:-2121}" \
    "${PS5_FTP_USER:-anonymous}" "${PS5_FTP_PASSWORD:-}" <<'PY'
import ftplib, hashlib, sys, os
from pathlib import Path

title_id, image, mode, host, port, user, password = sys.argv[1:8]
remote = f"/data/homebrew/{title_id}"
local = Path(image)

def connect():
    ftp = ftplib.FTP(); ftp.connect(host, int(port), timeout=60); ftp.login(user, password)
    return ftp

def listing(ftp):
    ftp.cwd(remote)
    out = {}
    for name, facts in ftp.mlsd("."):
        if name in (".", ".."):
            continue
        out[name.rstrip("/").split("/")[-1]] = int(facts.get("size", -1))
    return out

def remote_digest(ftp, name):
    h = hashlib.sha256(); size = 0
    def cb(chunk):
        nonlocal size
        h.update(chunk); size += len(chunk)
    ftp.retrbinary(f"RETR {name}", cb, blocksize=1024 * 256)
    return size, h.hexdigest()

def size_of(path):
    return path.stat().st_size

def upload(ftp, name, path, publish_as):
    with path.open("rb") as handle:
        ftp.storbinary(f"STOR {name}", handle, blocksize=1024 * 256)
    try:
        ftp.delete(publish_as)
    except ftplib.all_errors:
        pass
    ftp.rename(name, publish_as)
    return listing(ftp).get(publish_as, -1)

with connect() as ftp:
    before = listing(ftp)
    print(f"  remote: {remote}")
    for name in sorted(before):
        print(f"    {name} {before[name]:,}")

    if mode == "--check":
        raise SystemExit(0)

    if not local.is_file():
        raise SystemExit(f"error: no converted image at {local}; run tools/stage-ppsa.sh first")
    if "eboot.bin" not in before:
        raise SystemExit(f"error: {remote}/eboot.bin is not there; is the title installed?")

    # Keep the previous image, and record both digests so the change is auditable.
    old_size, old_digest = remote_digest(ftp, "eboot.bin")
    ftp.rename("eboot.bin", "eboot.bin.previous")
    stored = upload(ftp, ".eboot.bin.upload", local, "eboot.bin")
    stored_size, stored_digest = remote_digest(ftp, "eboot.bin")

    print(f"  replaced eboot.bin")
    print(f"    was: {old_size:,} bytes sha256 {old_digest[:16]}…  (kept as eboot.bin.previous)")
    print(f"    now: {stored:,} bytes sha256 {stored_digest[:16]}…  (local {size_of(local):,} bytes)")
    if stored != size_of(local) or stored_digest != hashlib.sha256(local.read_bytes()).hexdigest():
        raise SystemExit("error: the console's copy does not match the local image")
    print("  the console now holds the converted, signed application image")
PY
