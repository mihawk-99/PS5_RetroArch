#!/usr/bin/env bash
# PS5 RetroArch - start the title on the console and capture what it prints.
#
#   tools/console-launch.sh            launch and stream the output to the terminal
#   tools/console-launch.sh --capture  launch and keep the output in klog/
#   tools/console-launch.sh --check    ask the launcher what it is, change nothing
#
# The console runs websrv, whose homebrew launcher exposes the one call this
# needs: /hbldr with a path, its arguments, its environment and a working
# directory. Those three are not free choices -- they are exactly what the
# launcher manifest title/homebrew.js declares, and the payload expects them:
#
#   HOME            so RetroArch keeps its configuration, cores and saves in the
#                   title folder rather than somewhere the console clears
#   LD_LIBRARY_PATH so a library placed beside the payload is found
#   -f -c <config>  fullscreen, with the staged configuration file
#
# The call streams the payload's own output back and stays open for as long as
# the payload runs, so this is also the capture path: the text RetroArch prints
# is the evidence, and _capture_ keeps it.
#
# The console address is never hardcoded; it comes from .env (docs/DEPLOYMENT.md).

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

remote_dir="/data/homebrew/PS5_RetroArch"
remote_payload="$remote_dir/retroarch.elf"
remote_config="$remote_dir/retroarch.cfg"
base_path="${PS5RA_LAUNCH_PATH:-$remote_payload}"
capture_dir="$root/klog"
timeout_s="${PS5RA_LAUNCH_TIMEOUT:-45}"

mode=stream
case "${1:-}" in
    "")        mode=stream ;;
    --capture) mode=capture ;;
    --check)   mode=check ;;
    -h|--help) sed -n '2,22p' "${BASH_SOURCE[0]}" | sed 's/^# \?//'; exit 0 ;;
    *)         echo "usage: ${0##*/} [--capture|--check]" >&2; exit 2 ;;
esac

say() { printf '==> [console] %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

[[ -f .env ]] || die "no .env; copy .env.example and set PS5_HOST (docs/DEPLOYMENT.md)"
# shellcheck disable=SC1091
set -a; . ./.env; set +a
[[ -n ${PS5_HOST:-} ]] || die "PS5_HOST is not set in .env"
ws_port="${WEBSRV_PORT:-8080}"
ws="http://$PS5_HOST:$ws_port"

if [[ $mode == check ]]; then
    say "launcher   $ws"
    printf 'version:   '
    timeout 15 curl -fsS "$ws/version" 2>/dev/null || echo "(no /version endpoint)"
    printf 'filesystem '
    timeout 15 curl -fsS -o /dev/null -w 'listing: HTTP %{http_code}\n' "$ws/fs$remote_dir?fmt=json" \
        || say "the launcher did not list $remote_dir"
    exit 0
fi

# A payload that presents a window stops printing once it is up; the timeout is
# therefore the run's length, not a failure. curl's own timeout is what ends it.
query="pipe=1&daemon=0&path=$(printf '%s' "$base_path" | sed 's|/|%2F|g')"
query+="&args=-f%20-c%20$(printf '%s' "$remote_config" | sed 's|/|%2F|g')"
query+="&env=HOME%3D$(printf '%s' "$remote_dir" | sed 's|/|%2F|g')%20LD_LIBRARY_PATH%3D$(printf '%s' "$remote_dir" | sed 's|/|%2F|g')"
query+="&cwd=$(printf '%s' "$remote_dir" | sed 's|/|%2F|g')"

say "launching $base_path"
say "watching for ${timeout_s}s; close the title on the console when you are done"

if [[ $mode == capture ]]; then
    mkdir -p -- "$capture_dir"
    stamp=$(date +%Y%m%d-%H%M%S)
    out="$capture_dir/launch-$stamp.log"
    say "capturing to ${out#$root/}"
    timeout "$timeout_s" curl -sS --no-buffer "$ws/hbldr?$query" > "$out" 2>&1 || true
    say "captured $(wc -l < "$out") lines"
    grep -E '\[INFO\]|\[WARN\]|\[ERROR\]|RetroArch|driver|Video|Menu' "$out" | head -20 \
        || sed -n '1,20p' "$out"
else
    timeout "$timeout_s" curl -sS --no-buffer "$ws/hbldr?$query" || true
fi
