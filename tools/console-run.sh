#!/usr/bin/env bash
# PS5 RetroArch - run one title on the console and capture the console's own log.
#
#   tools/console-run.sh PPSA99169            listen, launch, capture, judge
#   tools/console-run.sh PPSA99169 --dry-run  report what would happen
#
# Why this exists. Two things learned the hard way on this console:
#
#   1. It runs ONE title at a time. Launching while another is running is refused
#      with 0x80940010 and nothing starts, so a "crash" read from a stale log is
#      really a refusal. This script checks what is running first and refuses to
#      launch into a busy console unless --force is given.
#   2. Every title's process is named `eboot.bin`, so a capture is only
#      attributable if it is delimited. The listener is started first, its
#      position in the stream is recorded, and every judgement below is made from
#      the lines after that mark.
#
# The console's log is the only witness this project trusts: FTP reads on this
# console have been measured returning another title's bytes for a path created
# moments earlier (docs/FINDINGS.md).

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

title=${1:-}
shift || true
force=0
for arg in "$@"; do
    case "$arg" in
        --force) force=1 ;;
        --dry-run) dry=1 ;;
        *) echo "usage: ${0##*/} TITLE_ID [--force] [--dry-run]" >&2; exit 2 ;;
    esac
done
[[ $title =~ ^[A-Z]{4}[0-9]{5}$ ]] || { echo "usage: ${0##*/} TITLE_ID [--force] [--dry-run]" >&2; exit 2; }

[[ -f .env ]] || { echo "no .env; set PS5_HOST in it" >&2; exit 2; }
set -a; . ./.env; set +a
host=${PS5_HOST:?PS5_HOST is not set in .env}
ctl=${PS5_CTL_PORT:-9111}
klog=${KLOG_PORT:-3232}
watch=${WATCH_SECONDS:-40}
mkdir -p klog

ctl_cmd() {
    timeout 10 bash -c "exec 3<>/dev/tcp/$host/$ctl; printf '%s\n' '$1' >&3; head -c 300 <&3" 2>/dev/null || true
}

before=$(ctl_cmd procs)
echo "==> [run] console holds: ${before:-<no answer>}"
busy=$(sed -n 's/.*title=\([A-Z0-9]*\).*/\1/p' <<<"$before")
if [[ -n $busy && $busy != "$title" && $force != 1 ]]; then
    cat >&2 <<MSG
error: $busy is running, and this console runs one title at a time.
       Launching $title now would be refused with 0x80940010 and nothing would
       start. Close $busy on the console, then run this again.
       Use --force to launch anyway (it will be refused, which is occasionally
       what you want to demonstrate).
MSG
    exit 3
fi

stamp=$(date +%H%M%S)
out="klog/${title}-${stamp}.log"
echo "==> [run] listening on $host:$klog -> ${out}"
( timeout "$((watch + 20))" bash -c "exec 3<>/dev/tcp/$host/$klog; cat <&3" > "$out" 2>&1 & )
sleep 3
mark=$(wc -l < "$out")
echo "==> [run] launch $title"
result=$(ctl_cmd "launch $title")
echo "==> [run] $result"

if [[ ${dry:-0} == 1 ]]; then
    echo "==> [run] dry run: no judgement made"
    exit 0
fi

echo "==> [run] watching for ${watch}s; close the title on the console when done"
sleep "$watch"

after=$(ctl_cmd procs)
echo "==> [run] console now holds: ${after:-<no answer>}"
echo
echo "==> [run] what the console said, from my launch onward:"
tail -n +"$mark" "$out" \
    | grep -viE 'ResArbitrator|SceShellCore\] FMEM|JS thread was busy|SceSystemStateMgr|AppDb\]\[AppBrowse' \
    | grep -iE "$title|EXEC |signal|exception|App Crash|Lack of|0x8[0-9a-f]{7}|RetroArch|error" \
    | tail -25 || echo "    (nothing matching)"

echo
if grep -q '0x80940010' <<<"$result"; then
    echo "==> [run] VERDICT: refused - another title was running (0x80940010)"
elif grep -q 'App Crash' "$out"; then
    echo "==> [run] VERDICT: started and crashed; the crash block above names why"
elif [[ -n $after ]]; then
    echo "==> [run] VERDICT: still running - it started and stayed up"
else
    echo "==> [run] VERDICT: started, then closed; see the lines above"
fi
