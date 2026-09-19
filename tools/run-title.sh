#!/usr/bin/env bash
# PS5 RetroArch - build, publish, run and read back, in one command.
#
#   tools/run-title.sh                 build, deploy, launch, watch, close, report
#   tools/run-title.sh --no-build      use what is already in dist/
#   tools/run-title.sh --no-deploy     run whatever the console already holds
#   tools/run-title.sh --watch 90      how long to let it run (default 30)
#   tools/run-title.sh --audio-test --watch 45  native PCM tones and queue checks
#   tools/run-title.sh --gpu-profile 60 --watch 80  buffered timing, then collect logs
#
# Why this exists. Every earlier round of the console loop was four hand-driven
# steps that needed a person: build, upload, launch, read. Two things went wrong
# with that, both recorded in docs/PHASE_LOG.md. Runs overlapped - a trace file
# from one launch was read as the result of another, and a probe build that had
# been left in place was mistaken for a finding. And the loop was slow enough that
# the console's state between rounds was guesswork.
#
# So one command owns the whole sequence, and it is responsible for the three
# things a person was doing:
#
#   deploy   publish dist/<TITLE_ID>/ over the console's FTP, then read every file
#            back and compare digests. Size is not evidence here: this console's
#            FTP has served a file's previous bytes under its new name.
#   watch    start the kernel-log listener BEFORE the launch, record its position
#            in the stream, and judge only the lines after that mark.
#   close    the title is closed by this script, not by hand, so a run cannot be
#            left running and a trace cannot belong to somebody else's launch.
#
# The trace in the title's own folder (/app0/trace.txt, read over FTP) is the
# result: it is written by the title as it starts, so it answers "how far did it
# get" in a way the kernel log cannot.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

build=1
deploy=1
watch=30
profile=0
audio_test=0
core_test=none
while (( $# )); do
    case "$1" in
        --no-build)  build=0 ;;
        --no-deploy) deploy=0 ;;
        --watch)     shift; watch=${1:?--watch needs seconds} ;;
        --audio-test) audio_test=1 ;;
        --core-test) core_test=fceumm ;;
        --core-test=*) core_test=${1#*=} ;;
        --gpu-profile) shift; profile=${1:?--gpu-profile needs seconds} ;;
        *) echo "usage: ${0##*/} [--no-build] [--no-deploy] [--watch SECONDS] [--gpu-profile 1..60] [--audio-test] [--core-test[=fceumm|mgba]]" >&2; exit 2 ;;
    esac
    shift
done
case "$core_test" in none|fceumm|mgba) ;; *) echo "unknown core diagnostic: $core_test" >&2; exit 2 ;; esac

[[ $watch =~ ^[0-9]+$ && $profile =~ ^[0-9]+$ ]] || { echo "durations must be integers" >&2; exit 2; }
(( profile <= 60 )) || { echo "GPU profile duration must be 1..60 seconds" >&2; exit 2; }
if (( profile > 0 && watch < profile + 15 )); then
    echo "--watch must allow the profile duration plus 15 seconds for startup/reporting" >&2
    exit 2
fi

if (( audio_test && watch < 20 )); then
    echo "--audio-test requires --watch of at least 20 seconds" >&2
    exit 2
fi

[[ -f .env ]] || { echo "error: no .env; set PS5_HOST in it" >&2; exit 2; }
set -a; . ./.env; set +a
host=${PS5_HOST:?PS5_HOST is not set in .env}
ctl=${PS5_CTL_PORT:-9111}
klog=${KLOG_PORT:-3232}

say() { printf '==> [run] %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' \
    "$root/sce_sys/param.json")

# One connection, one command, one reply line: the resident control payload answers
# a single request per connection. Python rather than bash's /dev/tcp because the
# reply has to be read to its newline and the tooling already depends on python3.
ctl_cmd() {
    python3 - "$host" "$ctl" "$1" <<'PY' 2>/dev/null || true
import socket, sys
host, port, cmd = sys.argv[1], int(sys.argv[2]), sys.argv[3]
try:
    with socket.create_connection((host, port), timeout=10) as sock:
        sock.sendall((cmd + "\n").encode())
        sock.settimeout(10)
        out = b""
        while b"\n" not in out:
            chunk = sock.recv(4096)
            if not chunk:
                break
            out += chunk
    print(out.decode("utf-8", "replace").strip())
except Exception as error:
    print(f"err {error}")
PY
}

running_count() {
    local reply
    reply=$(ctl_cmd procs)
    sed -n 's/.*count=\([0-9]*\).*/\1/p' <<<"$reply" | head -1
}

# --- build ------------------------------------------------------------------
if (( build )); then
    say "building"
    "$root/tools/build-title.sh" >"$root/build/run-title-build.log" 2>&1 ||
        { tail -20 "$root/build/run-title-build.log"; die "the build failed"; }
    say "built $(stat -c %s "dist/$title_id/eboot.bin") bytes of eboot.bin"
fi

[[ -f dist/$title_id/eboot.bin ]] || die "nothing built at dist/$title_id/"
# Bind validation to the artifact selected now, even if a later local build starts.
expected_identity=$(python3 - <<'PYID'
import re
from pathlib import Path
print(re.search(r"build identity: ([a-f0-9]{64})", Path("build/title_build_identity.h").read_text())[1])
PYID
)

# --- the console must be free ------------------------------------------------
held=$(ctl_cmd procs)
count=$(running_count)
say "console holds: ${held:-<no answer>}"
[[ $count == 0 ]] || die "the shared console is not confirmed idle; no upload or launch"

# --- deploy, then prove it ---------------------------------------------------
if (( deploy )); then
    say "publishing to the console and reading it back"
    if ! python3 "$root/tools/deploy-title.py" >"$root/build/run-title-deploy.log" 2>&1; then
        tail -12 "$root/build/run-title-deploy.log"
        die "the upload did not take: the console serves different bytes than the build"
    fi
    say "the console's copy is byte for byte this build"
fi

[[ $(running_count) == 0 ]] || die "the console became busy during deployment; not launching"

# --- a run starts from a known console state ---------------------------------
# /app0/args.txt is not in dist/ and deploy never deletes anything, so a copy
# left on the console by an earlier probe survives every later upload and keeps
# changing what the title does. It did: a capture file left behind made a run
# take a picture and then quit itself 90 frames in, which reads on the console as
# a crash with a coredump, and looks from the sofa like a title that never
# appeared. A run that does not ask for extras must not inherit them, so the file
# is removed on every run - before the launch, because deleting it afterwards
# would leave it for the next one if this run dies.
python3 - "$title_id" "$profile" "$audio_test" "$core_test" <<'PY'
import importlib.util, sys
spec = importlib.util.spec_from_file_location("dt", "tools/deploy-title.py")
dt = importlib.util.module_from_spec(spec); spec.loader.exec_module(dt)
from ps5_ftp import connect, remove_if_present
import io
with connect(**dt.load_settings()) as ftp:
    try:
        ftp.delete(f"/data/homebrew/{sys.argv[1]}/args.txt")
        print("    cleared a leftover args.txt from the console")
    except Exception:
        pass
    control = f"/data/homebrew/{sys.argv[1]}/core-loader-test.txt"
    remove_if_present(ftp, control)
    if sys.argv[4] != "none":
        remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/core-loader-test.json")
        remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/core-recovery-test.json")
        ftp.storbinary(f"STOR {control}", io.BytesIO((sys.argv[4] + "\n").encode()))
        print(f"    armed native {sys.argv[4]} loader test (no game)")
    control = f"/data/homebrew/{sys.argv[1]}/gpu-profile.txt"
    remove_if_present(ftp, control)
    if int(sys.argv[2]):
        remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/gpu-profile.tsv")
        ftp.storbinary(f"STOR {control}", io.BytesIO((sys.argv[2] + "\n").encode()))
        print(f"    armed buffered GPU profile for {sys.argv[2]} seconds")
    control = f"/data/homebrew/{sys.argv[1]}/audio-test.txt"
    remove_if_present(ftp, control)
    if int(sys.argv[3]):
        remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/audio-test.json")
        ftp.storbinary(f"STOR {control}", io.BytesIO(b"native PCM test\n"))
        print("    armed audio test: left 440 Hz / right 660 Hz, repeated, 12.5% peak")
PY

# --- listen first, then launch ----------------------------------------------
mkdir -p klog
stamp=$(date +%H%M%S)
capture="klog/run-$title_id-$stamp.log"
say "listening to the kernel log, then launching"
( timeout "$((watch + 25))" bash -c "exec 3<>/dev/tcp/$host/$klog; cat <&3" >"$capture" 2>&1 & )
sleep 3
mark=$(wc -l < "$capture")

launch_reply=$(ctl_cmd "launch $title_id")
say "$launch_reply"
case "$launch_reply" in
    *0x80940010*) die "refused: another title was starting (0x80940010)" ;;
esac

# --- watch, then close it ourselves ----------------------------------------
say "watching for ${watch}s"
sleep "$watch"

alive_after=$(running_count)
if [[ ${alive_after:-0} -gt 0 ]]; then
    say "still running after ${watch}s (count=$alive_after); closing it"
    ctl_cmd "kill $title_id" >/dev/null || true
    sleep 4
else
    say "it is not running any more (count=${alive_after:-?})"
fi
sleep 1

# --- read the result ---------------------------------------------------------
say "what the console said, from the moment of the launch:"
tail -n +"$mark" "$capture" 2>/dev/null |
    grep -aiE "$title_id|EXEC /app0|fatal signal|# signal|fault address|calls exit" |
    grep -aviE 'ResArbitrator|JS thread|SceShellUI|AsyncStorage|NPXS|updateVibration' |
    tail -12 || echo "    (nothing)"

say "the title's own trace:"
python3 - "$title_id" <<'PY' || echo "    (no trace file: the title never reached main)"
import io, sys
sys.path.insert(0, "tools")
from ps5_ftp import connect
from pathlib import Path
import importlib.util
spec = importlib.util.spec_from_file_location("dt", "tools/deploy-title.py")
dt = importlib.util.module_from_spec(spec); spec.loader.exec_module(dt)
title = sys.argv[1]
try:
    with connect(**dt.load_settings()) as ftp:
        buf = io.BytesIO()
        ftp.retrbinary(f"RETR /data/homebrew/{title}/trace.txt", buf.write)
    for line in buf.getvalue().decode("utf-8", "replace").splitlines():
        print(f"    {line}")
except Exception as error:
    print(f"    unreadable: {error}")
    raise SystemExit(1)
PY

# --- preserve development logs and optional buffered timing ------------------
python3 - "$title_id" "$profile" "$stamp" "$audio_test" "$expected_identity" "$core_test" <<'PY'
import importlib.util, json, sys
from pathlib import Path
sys.path.insert(0, "tools")
from ps5_ftp import connect, remove_if_present
spec = importlib.util.spec_from_file_location("dt", "tools/deploy-title.py")
dt = importlib.util.module_from_spec(spec); spec.loader.exec_module(dt)
with connect(**dt.load_settings()) as ftp:
    remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/gpu-profile.txt")
    remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/audio-test.txt")
    remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/core-loader-test.txt")
    names = ["retroarch.log"]
    if sys.argv[6] != "none":
        names.extend(["core-loader-test.json", "core-recovery-test.json"])
    if int(sys.argv[4]):
        names.append("audio-test.json")
    if int(sys.argv[2]):
        names.append("gpu-profile.tsv")
    for name in names:
        target = Path("klog") / f"{name.rsplit('.', 1)[0]}-{sys.argv[3]}.{name.rsplit('.', 1)[1]}"
        try:
            with target.open("wb") as out:
                ftp.retrbinary(f"RETR /data/homebrew/{sys.argv[1]}/{name}", out.write)
            expected = sys.argv[5]
            if name == "retroarch.log":
                if f"build identity: {expected}" not in target.read_text(errors="replace"):
                    raise SystemExit("RetroArch log is stale or logging failed: current build identity absent")
            if name in ("core-loader-test.json", "core-recovery-test.json"):
                report = json.loads(target.read_text())
                if report.get("build_identity") != f"build identity: {expected}" or report.get("core") != sys.argv[6] or not report.get("passed"):
                    raise SystemExit("Native core loader test failed or has stale identity")
                print("    native core loader test PASS")
            if name == "audio-test.json":
                report = json.loads(target.read_text())
                checks = (
                    report["build_identity"] == f"build identity: {expected}",
                    report["passed"] is True,
                    report["rate"] == 48000,
                    report["grain_frames"] == 256,
                    report["frame_bytes"] == 4,
                    report["capacity_frames"] == 1536,
                    report["accepted_frames"] == 193536,
                    report["played_frames"] == report["accepted_frames"],
                    report["errors"] == 0,
                    report["peak_frames"] == report["capacity_frames"],
                    report["nonblocking_bytes"] == 6144,
                )
                if not all(checks):
                    raise SystemExit("Native audio test failed or belongs to another build")
                print("    native audio playback/buffering report PASS; audible confirmation still required")
            print(f"    saved {name} to {target}")
        except Exception as error:
            print(f"    could not retrieve {name}: {type(error).__name__}")
            raise SystemExit(f"Required development log {name} was not captured")
PY

# --- say plainly what happened ----------------------------------------------
if grep -aq 'fatal signal' "$capture"; then
    say "VERDICT: it started and then died; the signal block above names where"
elif [[ ${alive_after:-0} -gt 0 ]]; then
    say "VERDICT: it ran for ${watch}s and this script closed it"
else
    say "VERDICT: it exited on its own before ${watch}s"
fi
say "capture: $capture"
