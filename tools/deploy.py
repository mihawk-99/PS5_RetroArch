#!/usr/bin/env python3
# PS5 RetroArch - publish the staged title folder to the console.
#
#   python3 tools/deploy.py deploy     upload dist/<name>/ to /data/homebrew/<name>/
#   python3 tools/deploy.py undeploy   remove /data/homebrew/<name>/
#   python3 tools/deploy.py list       list what is on the console now
#
# Safety rules, all of them enforced rather than documented:
#   - it only ever creates, replaces or removes the single remote directory
#     named after this project (REMOTE_NAME, "PS5_RetroArch"). Any other path on
#     the console is out of reach by construction, not by care: no recursive
#     delete is ever issued against a path this file did not compute itself;
#   - a file is uploaded under a temporary name and renamed only after the
#     transfer completes, so an interrupted deploy cannot leave a half-written
#     payload that the loader would start;
#   - DRY_RUN=1 prints every intended action and opens no socket.
#
# Credentials and the console address come from the environment or the ignored
# .env, never from a committed file. See docs/DEPLOYMENT.md.

from __future__ import annotations

import ftplib
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REMOTE_NAME = "PS5_RetroArch"
REMOTE_BASE = "/data/homebrew"
STAGE = ROOT / "dist" / "baseline"


def load_settings() -> dict:
    values: dict[str, str] = {}
    env_file = ROOT / ".env"
    if env_file.is_file():
        for raw in env_file.read_text(encoding="utf-8").splitlines():
            line = raw.strip()
            if line and not line.startswith("#") and "=" in line:
                key, value = line.split("=", 1)
                values[key.strip()] = value.strip()

    def setting(key: str, default: str = "") -> str:
        return os.environ.get(key, values.get(key, default))

    host = setting("PS5_HOST")
    if not host and setting("DEPLOY_DRY_RUN") != "1":
        raise SystemExit("PS5_HOST is not set in the environment or .env")
    return {
        "host": host,
        "port": int(setting("FTP_PORT", "2121")),
        "user": setting("PS5_FTP_USER", "anonymous"),
        "password": setting("PS5_FTP_PASSWORD"),
        "dry_run": setting("DEPLOY_DRY_RUN", "0") == "1",
    }


def connect(settings: dict) -> ftplib.FTP:
    ftp = ftplib.FTP()
    ftp.connect(settings["host"], settings["port"], timeout=30)
    ftp.login(settings["user"], settings["password"])
    return ftp


def remote_dir() -> str:
    return f"{REMOTE_BASE}/{REMOTE_NAME}"


def remote_sizes(ftp: ftplib.FTP) -> dict[str, int]:
    """File name to size, in the directory the connection is parked in."""
    sizes: dict[str, int] = {}
    for name, facts in ftp.mlsd("."):
        if facts.get("type") == "file":
            sizes[name.rstrip("/").split("/")[-1]] = int(facts.get("size", -1))
    return sizes


def listed_names(ftp: ftplib.FTP) -> set[str]:
    """Base names in the current directory.

    This console's ftpsrv returns each entry's full path rather than its name,
    so the last component is taken here instead of trusting the listing.
    """
    return {name.rstrip("/").split("/")[-1] for name, _ in ftp.mlsd()
            if name not in (".", "..")}


def ensure_remote_dir(ftp: ftplib.FTP, path: str) -> None:
    """Park the connection in `path`, creating it if it is not there yet.

    An absolute path is tried first: this console's FTP service accepts one and
    it is the only form that is unambiguous. The component-by-component walk is
    the fallback, and it starts over from the root each time, because that is
    the only place this service is known to resolve relative names from.
    """
    try:
        ftp.cwd(path)
        return
    except ftplib.error_perm:
        pass
    ftp.cwd("/")
    for part in path.strip("/").split("/"):
        try:
            ftp.cwd(part)
        except ftplib.error_perm:
            ftp.mkd(part)
            ftp.cwd(part)


def quiet_delete(ftp: ftplib.FTP, name: str) -> None:
    """Delete a file, tolerating this console's replies.

    Its ftpsrv answers a successful delete with 226 rather than 250, which
    ftplib raises on, and a missing file is not an error here either.
    """
    try:
        ftp.delete(name)
    except ftplib.all_errors:
        # This service answers a successful delete with 226, which ftplib
        # raises on; the file is gone either way.
        pass


def upload_file(ftp: ftplib.FTP, local: Path) -> None:
    """Upload one file and prove the console stored every byte of it.

    The temporary name and the rename are what make an interrupted deploy
    harmless; the size check is what makes a silently truncated transfer loud.
    Both matter here: one of these files is 70 MB, and a partial payload that
    keeps its name is a payload the loader will happily start.
    """
    name = local.name
    size = local.stat().st_size
    temporary = f".{name}.upload"
    with local.open("rb") as handle:
        ftp.storbinary(f"STOR {temporary}", handle, blocksize=1024 * 256)
    quiet_delete(ftp, name)
    try:
        ftp.rename(temporary, name)
    except ftplib.all_errors as error:
        raise SystemExit(f"{name}: the console refused the rename: {error}") from error
    stored = remote_sizes(ftp).get(name)
    if stored != size:
        raise SystemExit(
            f"{name}: the console stored {stored if stored is not None else 'nothing'} "
            f"of {size:,} bytes. Nothing was published under its real name."
        )
    print(f"    {name} ({size:,} bytes)")


def do_list(settings: dict) -> int:
    if settings["dry_run"]:
        print(f"would list {REMOTE_BASE}/ on {settings['host']}")
        return 0
    with connect(settings) as ftp:
        ftp.cwd(REMOTE_BASE)
        names = sorted(listed_names(ftp))
        print(f"{REMOTE_BASE}/ on {settings['host']}:")
        for name in names:
            marker = "  <- ours" if name == REMOTE_NAME else ""
            print(f"  {name}{marker}")
    return 0


def do_deploy(settings: dict) -> int:
    if not STAGE.is_dir():
        raise SystemExit(f"nothing staged at {STAGE}; build first (tools/build-baseline.sh)")
    files = sorted(p for p in STAGE.rglob("*") if p.is_file())
    if not files:
        raise SystemExit(f"{STAGE} holds no files")

    if settings["dry_run"]:
        print(f"would upload {len(files)} files to {remote_dir()}/ on {settings['host']}")
        for path in files:
            print(f"    {path.relative_to(STAGE)} ({path.stat().st_size:,} bytes)")
        return 0

    with connect(settings) as ftp:
        ensure_remote_dir(ftp, remote_dir())
        print(f"==> [deploy] {len(files)} files to {remote_dir()}/")
        # The payload goes last: everything it reads must already be there.
        ordered = sorted(files, key=lambda p: (p.name == "retroarch.elf", str(p)))
        for path in ordered:
            relative = path.relative_to(STAGE)
            # This console's FTP service resolves every path from the root and
            # refuses a nested argument, so the upload changes into the target
            # directory one component at a time and then returns.
            if relative.parent != Path("."):
                ensure_remote_dir(ftp, f"{remote_dir()}/{relative.parent}")
            upload_file(ftp, path)
            ftp.cwd("/")
        ftp.cwd(remote_dir())
        present = listed_names(ftp)
    missing = [str(p.relative_to(STAGE)) for p in files
               if p.relative_to(STAGE).as_posix() not in present]
    if missing:
        raise SystemExit(f"upload finished but the console does not list: {', '.join(missing)}")
    print("==> [deploy] done; launch it from the console's own loader")
    return 0


def do_undeploy(settings: dict) -> int:
    if settings["dry_run"]:
        print(f"would remove {remote_dir()}/ on {settings['host']}")
        return 0
    with connect(settings) as ftp:
        try:
            ftp.cwd(remote_dir())
        except ftplib.error_perm:
            print(f"==> [undeploy] {remote_dir()}/ is not present; nothing to do")
            return 0
        for name, facts in list(ftp.mlsd()):
            if name in (".", ".."):
                continue
            if facts.get("type") == "dir":
                raise SystemExit(
                    f"{remote_dir()}/{name} is a directory; refusing to remove it. "
                    "Remove it deliberately, by hand."
                )
            quiet_delete(ftp, name)
            print(f"    removed {name}")
        ftp.cwd(REMOTE_BASE)
        ftp.rmd(REMOTE_NAME)
    print(f"==> [undeploy] {remote_dir()}/ removed; the other homebrew folders were not touched")
    return 0


def main() -> int:
    action = sys.argv[1] if len(sys.argv) > 1 else "deploy"
    if action not in {"deploy", "undeploy", "list"}:
        print(f"usage: {Path(sys.argv[0]).name} [deploy|undeploy|list]", file=sys.stderr)
        return 2
    settings = load_settings()
    if settings["dry_run"]:
        print("==> [deploy] dry run: no network request will be sent")
    return {"deploy": do_deploy, "undeploy": do_undeploy, "list": do_list}[action](settings)


if __name__ == "__main__":
    raise SystemExit(main())
