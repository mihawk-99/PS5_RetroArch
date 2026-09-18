#!/usr/bin/env python3
# PS5 RetroArch - the FTP helpers this console needs, taken from the sibling
# project ../PS5_Vulkan/tools/deploy.sh, where they were proven on this hardware.
#
# They exist because this console's ftpsrv differs from a standards-compliant
# server in three ways, each of which breaks the obvious implementation:
#
#   - a successful DELE is answered with 226, and ftplib's delete() accepts only
#     200/250, so a successful delete raises. sendcmd() accepts any 2xx.
#   - paths resolve from the root, so every operation uses an absolute path
#     instead of relying on the connection's directory.
#   - a listing with a path argument behaves inconsistently, so callers change
#     directory first and list bare.
#
# Nothing here is clever. It is a record of what this server actually does.

from ftplib import FTP, error_perm
from posixpath import dirname, join

def reply_code(error):
    return str(error).split(maxsplit=1)[0]


def list_names(ftp, path):
    previous = ftp.pwd()
    ftp.cwd(path)
    try:
        return {name for name, _ in ftp.mlsd() if name not in {".", ".."}}
    finally:
        ftp.cwd(previous)


def ensure_directory(ftp, path):
    current = ""
    for component in path.strip("/").split("/"):
        current += f"/{component}"
        try:
            ftp.mkd(current)
        except error_perm as error:
            if reply_code(error) != "550":
                raise
            previous = ftp.pwd()
            try:
                ftp.cwd(current)
            finally:
                ftp.cwd(previous)


def remove_if_present(ftp, path):
    try:
        # ftpsrv returns 226 for a successful DELE. FTP.sendcmd accepts every
        # valid 2xx completion while ftplib.FTP.delete only permits 200/250.
        ftp.sendcmd(f"DELE {path}")
        return True
    except error_perm as error:
        text = str(error).lower()
        if reply_code(error) == "550" and ("no such" in text or "not found" in text):
            return False
        raise


def upload_atomic(ftp, local, remote):
    directory = dirname(remote)
    temporary = join(directory, f".{remote.rsplit('/', 1)[-1]}.upload")
    ensure_directory(ftp, directory)
    remove_if_present(ftp, temporary)
    with local.open("rb") as source:
        ftp.storbinary(f"STOR {temporary}", source, blocksize=256 * 1024)
    remove_if_present(ftp, remote)
    ftp.rename(temporary, remote)


def connect(host, port, user, password, timeout=180):
    ftp = FTP()
    ftp.connect(host, int(port), timeout=timeout)
    ftp.login(user, password)
    return ftp
