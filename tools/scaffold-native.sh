#!/usr/bin/env bash
# PS5 RetroArch - build this project's title-folder scaffold.
#
# This project is the native pipeline's scaffolding plus RetroArch's sources, so
# that RetroArch is *compiled for* the title pipeline instead of converted into it
# afterwards (docs/PHASE_LOG.md, "The route changes").
#
#   tools/scaffold-native.sh          create or refresh the scaffold
#   tools/scaffold-native.sh --check  report what is where, change nothing
#
# What is taken from the pipeline (../ps5-native-app-boilerplate-main):
#   tooling/   the native converter, the FSELF writer, the CRT and the PIE layout
#   runtime/   the loader-visible compatibility module, with its digest manifest
#   Makefile   the entry point
#   tools/     its build, deploy, packaging and asset scripts — merged into this
#              repository's tools/, never replacing it
#
# What is this repository's own:
#   tools/     the project's own scripts, which live beside the pipeline's
#   sce_sys/   the title's identity and launcher assets
#   src/       the integration: the entry point and the driver
#   vendor/    RetroArch's sources, fetched and never edited
#
# A note on tools/. An earlier version of this script replaced tools/ wholesale
# and deleted this project's own scripts. It now copies the pipeline's scripts in
# without overwriting anything, so re-running is safe.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

parent="$root/.."
donor="$parent/ps5-native-app-boilerplate-main"
mode=${1:-scaffold}

say() { printf '==> [scaffold] %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

[[ -d $donor ]] || die "the native pipeline is missing: $donor"

if [[ $mode == --check ]]; then
    for item in tooling runtime Makefile sce_sys src vendor; do
        [[ -e $root/$item ]] && printf '  %-10s present\n' "$item" || printf '  %-10s MISSING\n' "$item"
    done
    printf '  %-10s %s scripts\n' tools "$(ls "$root/tools" | wc -l)"
    exit 0
fi

say "copying the pipeline's scaffolding from ${donor##*/}"
for item in tooling runtime Makefile; do
    rm -rf -- "$root/$item"
    cp -a -- "$donor/$item" "$root/$item"
done
mkdir -p -- "$root/tools"
cp -an -- "$donor/tools/." "$root/tools/"   # -n: never clobber this project's scripts
mkdir -p -- "$root/sce_sys" "$root/src"

if [[ -f $root/runtime/libc.prx ]]; then
    (cd "$root/runtime" && sha256sum -c libc.prx.sha256 >/dev/null 2>&1) \
        && say "runtime verified against its manifest" \
        || die "the copied runtime does not match runtime/libc.prx.sha256"
else
    die "no runtime/libc.prx was copied; run the donor's own make libc first"
fi

cat > "$root/sce_sys/param.json" <<'JSON'
{
  "ageLevel": {"default": 0},
  "applicationCategoryType": 0,
  "applicationDrmType": "free",
  "attribute": 0,
  "attribute2": 0,
  "attribute3": 0,
  "conceptId": "99169",
  "contentBadgeType": 1,
  "contentId": "UP9000-PPSA99169_00-RETROARCH0000001",
  "contentVersion": "01.000.000",
  "downloadDataSize": 256,
  "gameIntent": {"permittedIntents": [{"intentType": "launchActivity"}]},
  "localizedParameters": {
    "defaultLanguage": "en-US",
    "en-US": {"titleName": "PS5 RetroArch"}
  },
  "masterVersion": "01.00",
  "pubtools": {
    "creationDate": "2026-09-18 00:00:00",
    "loudnessSnd0": "-28.00",
    "toolVersion": "2.00"
  },
  "requiredSystemSoftwareVersion": "0x0000000000000000",
  "sdkVersion": "0x0000000000000000",
  "titleId": "PPSA99169",
  "versionFileUri": ""
}
JSON
say "wrote sce_sys/param.json for PPSA99169"

[[ -f $root/title/assets/retroarch.png ]] \
    || die "the launcher icon source is missing: title/assets/retroarch.png"
cp -a -- "$root/title/assets/retroarch.png" "$root/sce_sys/icon0.png"
for asset in pic0.dds pic1.dds snd0.at9; do
    [[ -f $donor/sce_sys/$asset ]] && cp -a -- "$donor/sce_sys/$asset" "$root/sce_sys/$asset"
done
say "sce_sys holds param.json, the icon and the presentation assets"

[[ -d $root/vendor/retroarch/.git ]] || die \
    "vendor/retroarch is not a checkout; run tools/fetch-retroarch.sh first"
ln -sfn ../vendor/retroarch "$root/src/retroarch"
say "src/retroarch -> vendor/retroarch"

say "scaffold complete"
