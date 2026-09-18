#!/usr/bin/env bash
# PS5 RetroArch - can this file be started by the console's homebrew launcher?
#
#   tools/check-payload.sh dist/baseline/retroarch.elf
#
# Two routes start code on this console and they need different artefacts:
#
#   launcher  /hbldr starts an ELF and resolves its symbols; the file must import
#             the kernel stubs and carry its entry points in the *dynamic*
#             symbol table, because the launcher reads that table.
#   loader    the console's own application loader starts a title's eboot.bin,
#             which must be a converted development container (magic 4f153d1d).
#
# Converting for the loader strips the dynamic symbols - the converter refuses to
# publish exports - so a converted image can never be launched by the launcher,
# and an unconverted ELF can never be started as a title. This script answers the
# question for one file, so a wrong pairing is caught before a console run rather
# than diagnosed from a crash.
#
# Details and the measurements behind the rules: docs/FINDINGS.md.

set -euo pipefail

file=${1:-}
[[ -n $file ]] || { echo "usage: ${0##*/} FILE" >&2; exit 2; }
[[ -f $file ]] || { echo "error: no such file: $file" >&2; exit 2; }

SDK=${PS5_PAYLOAD_SDK:-/home/mihawk/ps5-payload-sdk}
nm="$SDK/bin/prospero-nm"
[[ -x $nm ]] || { echo "error: $nm not found; set PS5_PAYLOAD_SDK" >&2; exit 2; }

magic=$(head -c 4 "$file" | od -An -tx1 | tr -d ' \n')
size=$(stat -c%s "$file")
printf 'file           %s\n' "$file"
printf 'size           %s bytes\n' "$size"
printf 'magic          %s\n' "$magic"

case "$magic" in
    4f153d1d)
        echo "kind           converted development container (a title's eboot.bin)"
        echo "verdict        LOADER route only; the launcher cannot start this, because"
        echo "               conversion strips the dynamic symbols the launcher reads"
        exit 1
        ;;
    7f454c46)
        echo "kind           ELF"
        ;;
    *)
        echo "verdict        unrecognised container; neither route takes it" >&2
        exit 1
        ;;
esac

dyn=$("$nm" -D --defined-only "$file" 2>/dev/null | awk '$2 ~ /^[TtDdBbWw]$/ {print $3}')
# Imports come from the dynamic section's DT_NEEDED entries. nm's undefined list
# does not carry them: on this target they are resolved through stub libraries,
# so the file names them as needed objects rather than as undefined symbols.
needed=$(readelf -dW "$file" 2>/dev/null | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p')
problems=0

for entry in _start main; do
    if grep -qx "$entry" <<<"$dyn"; then
        printf 'entry point    %s (dynamic)\n' "$entry"
    fi
done
grep -qx '_start' <<<"$dyn" || { echo "  missing a dynamic _start"; problems=1; }

if grep -q '^libkernel_web\.sprx$' <<<"$needed"; then
    echo "imports        libkernel_web.sprx"
else
    echo "  does not import libkernel_web.sprx"; problems=1
fi
if grep -qx 'libkernel_sys\.sprx' <<<"$needed"; then
    echo "  imports libkernel_sys.sprx, which the launcher's process lacks"; problems=1
fi
printf 'dynamic syms   %s defined, %s undefined\n' "$(wc -w <<<"$dyn")" "$(wc -w <<<"$needed")"

if (( problems )); then
    echo "verdict        NOT launchable by the homebrew launcher" >&2
    exit 1
fi
echo "verdict        LAUNCHER route: this file is shaped for it"
