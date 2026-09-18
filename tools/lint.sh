#!/usr/bin/env bash
# ps5-native-app-boilerplate - Linux/WSL source and metadata checks.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs the portable checks used by the Make workflow.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
bash tools/setup-native-dependencies.sh >/dev/null
bash tools/run_clang_format.sh --check
bash tools/run_clang_tidy.sh

mapfile -t repository_files < <(git ls-files --cached --others --exclude-standard)
checked=0
for file in "${repository_files[@]}"; do
    [[ -f $file ]] || continue
    case "$file" in
        *.c|*.cc|*.cpp|*.h|*.hpp|*.ld|*.py|*.ps1|*.sh|*.yml|*.yaml|Makefile|.clang-format|.clang-tidy|.env.example)
            header=$(head -n 20 "$file")
            grep -Fq ps5-native-app-boilerplate <<<"$header"
            grep -Fq 'Copyright (C) 2026 BlackBearReloaded' <<<"$header"
            grep -Fq 'SPDX-License-Identifier: GPL-3.0-or-later' <<<"$header"
            ((checked += 1))
            ;;
        *.cs|*.csproj)
            echo "managed source is not allowed: $file" >&2
            exit 2
            ;;
    esac
done

while IFS= read -r script; do
    bash -n "$script"
done < <(find tools -maxdepth 1 -type f -name '*.sh' -print)
bash tools/validate-assets.sh

python3 - <<'PY'
from pathlib import Path
import json, re, subprocess
for name in subprocess.check_output(["git", "ls-files", "*.json"], text=True).splitlines():
    if Path(name).is_file():
        with open(name, encoding="utf-8") as source:
            json.load(source)

with open("sce_sys/param.json", encoding="utf-8") as source:
    param = json.load(source)
title_id = param.get("titleId", "")
content_id = param.get("contentId", "")
if not re.fullmatch(r"PPSA\d{5}", title_id):
    raise SystemExit("param.json titleId must use PPSA followed by five digits")
if not re.fullmatch(r"\d{5}", param.get("conceptId", "")):
    raise SystemExit("param.json conceptId must contain five digits")
if (not re.fullmatch(r"[A-Z]{2}\d{4}-PPSA\d{5}_00-[A-Z0-9]{16}", content_id)
        or title_id not in content_id):
    raise SystemExit("param.json contentId must be valid and contain titleId")
if not re.fullmatch(r"\d{2}\.\d{3}\.\d{3}", param.get("contentVersion", "")):
    raise SystemExit("param.json contentVersion must use NN.NNN.NNN")
if not re.fullmatch(r"\d{2}\.\d{2}", param.get("masterVersion", "")):
    raise SystemExit("param.json masterVersion must use NN.NN")
size = param.get("downloadDataSize")
if isinstance(size, bool) or not isinstance(size, int) or size < 0:
    raise SystemExit("param.json downloadDataSize must be a non-negative integer")
category = param.get("applicationCategoryType")
if (category, param.get("contentBadgeType")) not in {(0, 1), (65536, 2)}:
    raise SystemExit("param.json category and badge must describe a game or media app")
if category == 0:
    intents = param.get("gameIntent", {}).get("permittedIntents", [])
    if not any(item.get("intentType") == "launchActivity" for item in intents):
        raise SystemExit("game param.json must permit the launchActivity intent")
elif "gameIntent" in param:
    raise SystemExit("media param.json must not contain gameIntent")
localized = param.get("localizedParameters", {})
language = localized.get("defaultLanguage", "")
title = localized.get(language, {}).get("titleName", "")
if not isinstance(title, str) or not title.strip():
    raise SystemExit("param.json default-language titleName cannot be empty")
PY

if git grep -n -E 'C:\\Users\\|/home/denis|/mnt/c/Users/denis|\bDenis\b' -- . \
    ':(exclude)tools/lint.sh'; then
    echo "repository contains a local path or personal-name leak" >&2
    exit 2
fi
git diff --check
printf 'Lint passed: %d attributed code and tooling files.\n' "$checked"
