#!/usr/bin/env bash
# ps5-native-app-boilerplate - Application identity initializer.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Updates the coordinated identity and category fields in param.json.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
param=${1:-$root/sce_sys/param.json}

command -v python3 >/dev/null || {
    echo "missing required command: python3" >&2
    exit 2
}

python3 - "$param" <<'PY'
import json
import os
from pathlib import Path
import re
import stat
import sys
import tempfile

path = Path(sys.argv[1])
title_id = os.environ.get("TITLE_ID", "")
app_name = os.environ.get("APP_NAME", "").strip()
category = os.environ.get("APP_CATEGORY", "game").lower()
suffix = os.environ.get("CONTENT_SUFFIX", "")

if not re.fullmatch(r"PPSA\d{5}", title_id):
    raise SystemExit("TITLE_ID must use PPSA followed by five digits")
if not app_name or any(character in app_name for character in "\r\n"):
    raise SystemExit("APP_NAME must be a non-empty, single-line title")
if category not in {"game", "media"}:
    raise SystemExit("APP_CATEGORY must be game or media")
if not suffix:
    suffix = re.sub(r"[^A-Z0-9]", "", app_name.upper())[:16].ljust(16, "0")
if not re.fullmatch(r"[A-Z0-9]{16}", suffix):
    raise SystemExit("CONTENT_SUFFIX must contain exactly 16 uppercase letters or digits")

with path.open(encoding="utf-8") as source:
    param = json.load(source)
original_mode = stat.S_IMODE(path.stat().st_mode)

content_id = param.get("contentId", "")
prefix = content_id.split("-", 1)[0]
if not re.fullmatch(r"[A-Z]{2}\d{4}", prefix):
    prefix = "UP9000"

param["titleId"] = title_id
param["conceptId"] = title_id[4:]
param["contentId"] = f"{prefix}-{title_id}_00-{suffix}"

localized = param.setdefault("localizedParameters", {})
language = localized.setdefault("defaultLanguage", "en-US")
localized.setdefault(language, {})["titleName"] = app_name

if category == "game":
    param["applicationCategoryType"] = 0
    param["contentBadgeType"] = 1
    param["gameIntent"] = {"permittedIntents": [{"intentType": "launchActivity"}]}
else:
    param["applicationCategoryType"] = 65536
    param["contentBadgeType"] = 2
    param.pop("gameIntent", None)

path.parent.mkdir(parents=True, exist_ok=True)
descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
try:
    with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as output:
        json.dump(param, output, indent=2, ensure_ascii=False)
        output.write("\n")
    os.chmod(temporary_name, original_mode)
    os.replace(temporary_name, path)
except BaseException:
    try:
        os.unlink(temporary_name)
    except FileNotFoundError:
        pass
    raise

print(f"Configured {title_id}: {app_name} ({category})")
print(f"Content ID: {param['contentId']}")
PY
