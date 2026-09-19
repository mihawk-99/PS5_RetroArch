#!/usr/bin/env bash
# PS5 RetroArch - verify the built title folder against its own manifest.
#
#   bash tools/check-manifest.sh            verify dist/<TITLE_ID>/
#   bash tools/check-manifest.sh --record   write manifest.sha256 beside it
#
# Why the title folder has a manifest at all. The artifact that reaches the
# console is a folder of files, and two of them decide whether the console runs
# this build or an older one: eboot.bin, which the loader executes, and
# sce_module/libc.prx, which is the C library the title carries. This project has
# already published a folder whose eboot.bin was a raw link-stage ELF and whose
# libc.prx was a different build from the one in the tree - both cases where every
# file was present, the sizes looked plausible, and the title could not start.
# "The folder is there" is not a check; a digest per file is.
#
# The manifest records the file list, each size, and each sha256, and this script
# recomputes all three. It also checks the two properties a folder must have for
# the console to treat it as a title at all, which no digest can express:
# eboot.bin is a fake-SELF application image, and the title id in param.json is
# the one the folder is named after.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

mode=${1:-}
case "$mode" in
    ''|--record) ;;
    *) echo "usage: ${0##*/} [--record]" >&2; exit 2 ;;
esac

mapfile -t built < <(find dist -maxdepth 1 -mindepth 1 -type d -name 'PPSA[0-9][0-9][0-9][0-9][0-9]' 2>/dev/null | sort)
(( ${#built[@]} )) || { echo "error: nothing built under dist/; run tools/build-title.sh" >&2; exit 2; }
if (( ${#built[@]} > 1 )); then
    printf 'error: more than one title is built: %s\n' "${built[*]}" >&2
    echo "       keep the one being shipped; a manifest names one folder" >&2
    exit 2
fi

folder=${built[0]}
title_id=${folder##*/}
manifest="$folder/manifest.sha256"

# The file list is sorted and relative, so the same folder recorded twice gives
# the same manifest and two people can compare them. manifest.sha256 is excluded
# from itself, which is the one entry that cannot be hashed.
record() {
    ( cd "$folder" && find . -type f ! -name 'manifest.sha256' -printf '%P\0' |
        sort -z | xargs -0 sha256sum --binary )
}

echo "==> [manifest] $folder ($(find "$folder" -type f | wc -l) files)"

if [[ $mode == --record ]]; then
    # A title id that does not match its folder is written down wrong once and
    # then verified wrong forever, so it is checked before the manifest exists.
    recorded=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1], encoding="utf-8"))["titleId"])' \
        "$folder/sce_sys/param.json")
    [[ $recorded == "$title_id" ]] || {
        echo "error: param.json says $recorded but the folder is $title_id" >&2
        exit 2
    }
    record > "$manifest"
    echo "==> [manifest] recorded $(wc -l < "$manifest") entries"
    exit 0
fi

[[ -f $manifest ]] || {
    echo "error: $folder has no manifest.sha256" >&2
    echo "       run 'bash tools/check-manifest.sh --record' when the build is the one to ship" >&2
    exit 2
}

failures=0
bad() { printf '  FAIL %s\n' "$*" >&2; failures=$((failures + 1)); }

echo "==> [manifest] every recorded file is present, and unchanged"
if ( cd "$folder" && sha256sum --check --strict --quiet manifest.sha256 ); then
    echo "    all $(wc -l < "$manifest") files match"
else
    bad "a file is missing, or its contents differ from the manifest"
fi

echo "==> [manifest] nothing unrecorded was added"
mapfile -t on_disk < <( cd "$folder" && find . -type f ! -name 'manifest.sha256' -printf '%P\n' | sort)
mapfile -t in_manifest < <( sed -e 's/^[0-9a-f]* [ *]//' -e 's|^\./||' "$manifest" | sort)
added=$(comm -23 <(printf '%s\n' "${on_disk[@]}") <(printf '%s\n' "${in_manifest[@]}"))
if [[ -n $added ]]; then
    printf '  FAIL unrecorded: %s\n' "$added" >&2
    failures=$((failures + 1))
else
    echo "    no extra files"
fi

echo "==> [manifest] the two files the console reads"
python3 tools/check-core.py "$folder/cores/fceumm_libretro.so" || bad "FCEUmm core ABI check failed"
[[ -s $folder/info/fceumm_libretro.info ]] || bad "FCEUmm core info is missing"
cmp -s "$folder/info/fceumm_libretro.info" "$folder/cores/fceumm_libretro.info" ||
    bad "FCEUmm metadata fallback differs or is missing"
for required in eboot.bin sce_sys/param.json sce_sys/icon0.png; do
    [[ -f $folder/$required ]] || bad "$required is missing"
done

if [[ -f $folder/eboot.bin ]]; then
    # 0x1D3D154F little-endian: the magic of a fake self, which is the container
    # the console's loader accepts in place of a signed application image. A raw
    # ELF here (7f 45 4c 46) is the failure this catches.
    magic=$(head -c 4 "$folder/eboot.bin" | od -An -tx1 | tr -d ' \n')
    if [[ $magic == 4f153d1d ]]; then
        echo "    eboot.bin is a fake self application image ($magic)"
    else
        bad "eboot.bin starts with $magic, not the fake self magic 4f153d1d"
    fi
fi

if [[ -f $folder/sce_sys/param.json ]]; then
    recorded=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1], encoding="utf-8"))["titleId"])' \
        "$folder/sce_sys/param.json")
    if [[ $recorded == "$title_id" ]]; then
        echo "    param.json names $recorded, which is the folder's name"
    else
        bad "param.json says $recorded but the folder is $title_id"
    fi
fi

if (( failures > 0 )); then
    printf 'check-manifest: FAIL (%s)\n' "$failures" >&2
    exit 1
fi
echo "check-manifest: PASS"
