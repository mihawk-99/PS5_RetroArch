#!/usr/bin/env bash
# PS5 RetroArch - build, publish, run and read back, in one command.
#
#   tools/run-title.sh                 build, deploy, launch, watch, close, report
#   tools/run-title.sh --no-build      use what is already in dist/
#   tools/run-title.sh --no-deploy     run whatever the console already holds
#   tools/run-title.sh --watch 90      how long to let it run (default 30)
#   tools/run-title.sh --audio-test --watch 45  native PCM tones and queue checks
#   tools/run-title.sh --gpu-profile 60 --watch 80  buffered timing, then collect logs
#   tools/run-title.sh --relaunch-test=5 --watch 60  restart the title 5 times, then RetroArch
#   tools/run-title.sh --relaunch-test=1 --relaunch-image=probe/eboot-copy.bin --watch 45
#                                       restart through a copy of eboot.bin at /app0/probe/...
#   tools/run-title.sh --display-modes-test=180 --watch 60  every display size, then RetroArch
#   tools/run-title.sh --relaunch-test=1 --relaunch-image=es-de/es-de.bin --frontend-capture=20,40 --watch 60
#                                       hand over to EmulationStation, picture its frames at 20 and 40 s
#                                       (--frontend-scroll: and press Right every 400 ms meanwhile;
#                                       --frontend-profile: and sample the CPU, for tools/esde-profile.py;
#                                       --frontend-quit: then quit it, back to eboot.bin;
#                                       --frontend-launch: start its first game at 6-8 s, in game mode)
#   tools/run-title.sh --relaunch-test=1 --relaunch-image=es-de/es-de.bin --frontend-capture=4,20 \
#       --frontend-launch --retroarch-frames=600 --watch 75
#                                       ES-DE starts a game in RetroArch (game mode), which quits
#                                       after 600 frames: back in ES-DE, on that game
#   tools/run-title.sh --picker-test=300:retroarch --retroarch-frames=600 --watch 60
#                                       the picker chooses RetroArch, which quits after 600
#                                       frames: the title goes back to the picker
#   tools/run-title.sh --picker-test=300:es-de --frontend-capture=20 --watch 60
#                                       the picker, as the home screen starts it: 300 frames, a
#                                       picture of the last, then it chooses EmulationStation
#   tools/run-title.sh --pad-script=tests/pad/manual-scan-reentry.txt --watch 40
#                                       RetroArch with the pad scripted (src/input_ps5.cpp says
#                                       how); its SCREENSHOT pictures are collected afterwards
#   tools/run-title.sh --picker-test=300:es-de:remember --frontend-capture=20 --watch 50
#                                       the same, with the picker's Remember switch on: from
#                                       then on the title starts EmulationStation straight away
#   tools/run-title.sh --home-launch --watch 30
#                                       a launch as the home screen makes it: no test-run marker,
#                                       so the remembered frontend or the picker starts, and the
#                                       title clears every test file; this script closes it
#   tools/run-title.sh --pad-monitor --watch 120
#                                       trace each controller port's buttons as they change, to
#                                       tell several controllers apart in the trace
#   tools/run-title.sh --game=fceumm:/app0/tests/netplay-test.nes --retroarch-arg=--host --watch 60
#                                       one more RetroArch argument a time (repeatable), after the
#                                       game in /app0/args.txt: netplay (--host, --connect=IP),
#                                       an --appendconfig=FILE for a test's settings
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
relaunch_test=0
display_modes_test=0
relaunch_image=
frontend_capture=
frontend_scroll=
picker_test=
retroarch_frames=0
game=
pad_script=
pad_monitor=0
menu_capture=0
home_launch=0
retroarch_args=
while (( $# )); do
    case "$1" in
        --no-build)  build=0 ;;
        --no-deploy) deploy=0 ;;
        --watch)     shift; watch=${1:?--watch needs seconds} ;;
        --audio-test) audio_test=1 ;;
        --core-test) core_test=fceumm ;;
        --core-test=*) core_test=${1#*=} ;;
        --gpu-profile) shift; profile=${1:?--gpu-profile needs seconds} ;;
        --relaunch-test) relaunch_test=5 ;;
        --relaunch-test=*) relaunch_test=${1#*=} ;;
        --relaunch-image=*) relaunch_image=${1#*=} ;;
        --display-modes-test) display_modes_test=180 ;;
        --display-modes-test=*) display_modes_test=${1#*=} ;;
        --frontend-capture=*) frontend_capture=${1#*=} ;;
        --frontend-scroll) frontend_scroll=${frontend_scroll:+$frontend_scroll,}scroll ;;
        --frontend-profile) frontend_scroll=${frontend_scroll:+$frontend_scroll,}profile ;;
        --frontend-quit) frontend_scroll=${frontend_scroll:+$frontend_scroll,}quit ;;
        --frontend-launch) frontend_scroll=${frontend_scroll:+$frontend_scroll,}launch ;;
        --picker-test=*) picker_test=${1#*=} ;;
        --retroarch-frames=*) retroarch_frames=${1#*=} ;;
        --game=*) game=${1#*=} ;;
        --pad-script=*) pad_script=${1#*=} ;;
        --pad-monitor) pad_monitor=1 ;;
        --menu-capture=*) menu_capture=${1#*=} ;;
        --home-launch) home_launch=1 ;;
        --retroarch-arg=*) retroarch_args+="${1#*=}"$'\n' ;;
        *) echo "usage: ${0##*/} [--no-build] [--no-deploy] [--watch SECONDS] [--gpu-profile 1..60] [--audio-test] [--core-test[=fceumm|mgba|snes9x|fbneo|genesis_plus_gx|ppsspp]] [--relaunch-test[=1..20] [--relaunch-image=PATH]] [--display-modes-test[=frames]] [--frontend-capture=SECONDS[,SECONDS...] [--frontend-scroll] [--frontend-profile] [--frontend-quit] [--frontend-launch]] [--picker-test=FRAMES:retroarch|es-de|none] [--retroarch-frames=N] [--game=CORE:PATH] [--pad-script=FILE] [--pad-monitor] [--menu-capture=FRAMES] [--home-launch] [--retroarch-arg=ARG ...]" >&2; exit 2 ;;
    esac
    shift
done
case "$core_test" in none|fceumm|mgba|snes9x|fbneo|genesis_plus_gx|ppsspp) ;; *) echo "unknown core diagnostic: $core_test" >&2; exit 2 ;; esac

[[ $watch =~ ^[0-9]+$ && $profile =~ ^[0-9]+$ ]] || { echo "durations must be integers" >&2; exit 2; }
(( profile <= 60 )) || { echo "GPU profile duration must be 1..60 seconds" >&2; exit 2; }
if (( profile > 0 && watch < profile + 15 )); then
    echo "--watch must allow the profile duration plus 15 seconds for startup/reporting" >&2
    exit 2
fi

# The relaunch test (src/relaunch_ps5.cpp) restarts the title, then RetroArch starts.
[[ $relaunch_test =~ ^[0-9]+$ ]] && (( relaunch_test <= 20 )) ||
    { echo "--relaunch-test takes 1..20 restarts" >&2; exit 2; }
if (( relaunch_test && watch < 30 )); then
    echo "--relaunch-test requires --watch of at least 30 seconds" >&2
    exit 2
fi
relaunch_run=relaunch-$(date +%Y%m%d-%H%M%S)
# Another image of the title, a path under /app0: a copy of this build's eboot.bin.
if [[ -n $relaunch_image ]]; then
    (( relaunch_test )) || { echo "--relaunch-image needs --relaunch-test" >&2; exit 2; }
    [[ $relaunch_image =~ ^[A-Za-z0-9_][A-Za-z0-9_./-]*$ && $relaunch_image != *..* ]] ||
        { echo "--relaunch-image takes a path under /app0 without '..'" >&2; exit 2; }
    # A frontend's executable is never stood in for by a copy of eboot.bin: with dist/
    # built without that frontend (the build gate builds none), the copy replaced
    # ES-DE on the console and the cleanup then deleted it (2026-10-04).
    if [[ $relaunch_image == es-de/* || $relaunch_image == picker/* ]]; then
        frontend_title=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' \
            "$root/sce_sys/param.json")
        [[ -f dist/$frontend_title/$relaunch_image ]] || {
            echo "--relaunch-image $relaunch_image is a frontend's, and dist/ has none: build with PS5_FRONTENDS" >&2
            exit 2
        }
    fi
fi
# The display modes test (src/display_modes_ps5.cpp): frames a mode, every mode, then RetroArch.
[[ $display_modes_test =~ ^[0-9]+$ ]] && (( display_modes_test <= 1200 )) ||
    { echo "--display-modes-test takes 1..1200 frames a mode" >&2; exit 2; }
if (( display_modes_test && watch < 30 )); then
    echo "--display-modes-test requires --watch of at least 30 seconds" >&2
    exit 2
fi

# The frontend capture (frontends/es-de/ps5/capture_ps5.cpp): pictures of the frames
# EmulationStation presents that many seconds after its first.
capture_run=frontend-$(date +%Y%m%d-%H%M%S)
if [[ -n $frontend_capture ]]; then
    [[ $frontend_capture =~ ^[0-9]+(,[0-9]+)*$ ]] ||
        { echo "--frontend-capture takes seconds, comma-separated" >&2; exit 2; }
    (( ${frontend_capture##*,} + 15 <= watch )) ||
        { echo "--watch must allow the last capture plus 15 seconds" >&2; exit 2; }
fi

# The picker test (frontends/picker/picker.cpp): the frontend picker, as a launch from
# the home screen starts it, draws FRAMES frames without the pad and chooses.
if [[ -n $picker_test ]]; then
    [[ $picker_test =~ ^[0-9]+:(retroarch|es-de|none)(:(remember|forget))?$ ]] ||
        { echo "--picker-test takes FRAMES:retroarch, FRAMES:es-de or FRAMES:none, then :remember or :forget" >&2; exit 2; }
    (( ${picker_test%%:*} >= 10 && ${picker_test%%:*} <= 36000 )) ||
        { echo "--picker-test takes 10..36000 frames" >&2; exit 2; }
fi

[[ $retroarch_frames =~ ^[0-9]+$ ]] && (( retroarch_frames <= 100000 )) ||
    { echo "--retroarch-frames takes 1..100000 frames" >&2; exit 2; }

# --game=CORE:PATH starts RetroArch straight into a core and its content: CORE is
# the libretro name (flycast for cores/flycast_libretro.so), PATH the content's
# path as the title sees it (/app0/content/...). With --retroarch-frames it quits
# after that many frames and pictures the last one, collected as klog/game-<stamp>.png.
if [[ -n $game ]]; then
    [[ $game =~ ^[a-z0-9_]+:/.+ ]] ||
        { echo "--game takes CORE:PATH, PATH as the title sees it (/app0/...)" >&2; exit 2; }
fi

# The pad script (src/input_ps5.cpp): presses and actions timed from RetroArch's
# first poll, at most 128 lines.
if [[ -n $pad_script ]]; then
    [[ -f $pad_script ]] || { echo "--pad-script: no file $pad_script" >&2; exit 2; }
    (( $(grep -cvE '^[[:space:]]*(#|$)' "$pad_script") <= 128 )) ||
        { echo "--pad-script holds more than 128 presses and actions" >&2; exit 2; }
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
python3 - "$title_id" "$profile" "$audio_test" "$core_test" "$relaunch_test" "$relaunch_run" "$display_modes_test" "$relaunch_image" "$frontend_capture" "$capture_run" "$frontend_scroll" "$picker_test" "$retroarch_frames" "$pad_script" "$pad_monitor" "$home_launch" "$game" "$menu_capture" "$retroarch_args" <<'PY'
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
    control = f"/data/homebrew/{sys.argv[1]}/relaunch-test.txt"
    remove_if_present(ftp, control)
    if int(sys.argv[5]):
        remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/relaunch-test.jsonl")
        image = ""
        if sys.argv[8]:
            from pathlib import Path
            from ps5_ftp import upload_atomic
            image = f" /app0/{sys.argv[8]}"
            if Path(f"dist/{sys.argv[1]}/{sys.argv[8]}").is_file():
                print(f"    the image is this build's /app0/{sys.argv[8]}, deployed with the title")
            else:
                upload_atomic(ftp, Path(f"dist/{sys.argv[1]}/eboot.bin"), f"/data/homebrew/{sys.argv[1]}/{sys.argv[8]}")
                print(f"    uploaded a copy of this build's eboot.bin to /app0/{sys.argv[8]}")
        ftp.storbinary(f"STOR {control}", io.BytesIO(f"{sys.argv[5]} {sys.argv[6]}{image}\n".encode()))
        print(f"    armed relaunch test: {sys.argv[5]} restarts, run {sys.argv[6]}{image}")
    control = f"/data/homebrew/{sys.argv[1]}/display-modes-test.txt"
    remove_if_present(ftp, control)
    if int(sys.argv[7]):
        remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/display-modes-test.jsonl")
        ftp.storbinary(f"STOR {control}", io.BytesIO(f"{sys.argv[7]}\n".encode()))
        print(f"    armed display modes test: {sys.argv[7]} frames a mode")
    control = f"/data/homebrew/{sys.argv[1]}/es-de/capture-test.txt"
    remove_if_present(ftp, control)
    if sys.argv[9]:
        remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/es-de/capture-test.jsonl")
        ftp.storbinary(f"STOR {control}", io.BytesIO(f"{sys.argv[9]} {sys.argv[10]} {sys.argv[11]}\n".encode()))
        print(f"    armed frontend capture at {sys.argv[9]} s, run {sys.argv[10]}"
              f"{', pressing Right every 400 ms' if 'scroll' in sys.argv[11] else ''}"
              f"{', sampling the CPU every ms' if 'profile' in sys.argv[11] else ''}"
              f"{', then quitting it' if 'quit' in sys.argv[11] else ''}"
              f"{', starting its first game at 6-8 s' if 'launch' in sys.argv[11] else ''}")
    control = f"/data/homebrew/{sys.argv[1]}/picker/picker-test.txt"
    remove_if_present(ftp, control)
    if sys.argv[12]:
        frames, choice, *switch = sys.argv[12].split(":")
        remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/picker/picker-test.jsonl")
        remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/picker/screenshots/picker.ppm")
        ftp.storbinary(f"STOR {control}", io.BytesIO(f"{frames} {choice} {' '.join(switch)}\n".encode()))
        print(f"    armed picker test: {frames} frames, then {choice}{', Remember ' + switch[0] if switch else ''}")
    # RetroArch quits by itself after that many frames (its --max-frames), from
    # /app0/args.txt, which a test run's launch keeps.
    # --game: the core and the content, one argument a line (paths have spaces),
    # and with a frame count a picture of the last frame.
    lines = []
    if sys.argv[17]:
        core, path = sys.argv[17].split(":", 1)
        lines += ["-L", f"/app0/cores/{core}_libretro.so", path]
        remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/game-shot.png")
        print(f"    RetroArch starts {core} with {path}")
    # --menu-capture: the whole frame at that frame count, menu included (patch 0031's
    # read-back), written to /app0/shot.ppm and collected after the run.
    if int(sys.argv[18]):
        lines.append(f"--ps5-capture={sys.argv[18]}")
        remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/shot.ppm")
        print(f"    the frame at {sys.argv[18]} is captured, menu included")
    # --retroarch-arg: each as given, after the game (netplay, an appended config).
    for extra in sys.argv[19].splitlines():
        lines.append(extra)
        print(f"    RetroArch argument {extra}")
    if int(sys.argv[13]):
        lines.append(f"--max-frames={sys.argv[13]}")
        if sys.argv[17]:
            lines += ["--max-frames-ss", "--max-frames-ss-path=/app0/game-shot.png"]
        print(f"    RetroArch quits after {sys.argv[13]} frames")
    if lines:
        ftp.storbinary(f"STOR /data/homebrew/{sys.argv[1]}/args.txt",
                       io.BytesIO(("\n".join(lines) + "\n").encode()))
    # The pad script, and the pictures an earlier one took: a test launch keeps
    # /app0/pad-script.txt, so one left behind would drive this run.
    remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/pad-script.txt")
    from ps5_ftp import list_names
    for name in sorted(list_names(ftp, f"/data/homebrew/{sys.argv[1]}")):
        if name.startswith("pad-shot-") and name.endswith(".png"):
            remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/{name}")
    if sys.argv[14]:
        with open(sys.argv[14], "rb") as script:
            ftp.storbinary(f"STOR /data/homebrew/{sys.argv[1]}/pad-script.txt", script)
        print(f"    armed pad script {sys.argv[14]}")
    # The pad monitor (src/input_ps5.cpp): each controller port's buttons traced.
    remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/pad-monitor.txt")
    if int(sys.argv[15]):
        ftp.storbinary(f"STOR /data/homebrew/{sys.argv[1]}/pad-monitor.txt", io.BytesIO(b"pad monitor\n"))
        print("    armed the pad monitor: each controller port's buttons go to the trace")
    # The launch is a test run's (src/main.cpp): it keeps the test files this run
    # armed, and starts RetroArch rather than the frontend picker
    # (src/frontend_mode_ps5.cpp) unless a picker test is armed.
    if int(sys.argv[16]):
        remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/test-run.txt")
        print("    a launch as the home screen makes it: no test-run marker")
    else:
        ftp.storbinary(f"STOR /data/homebrew/{sys.argv[1]}/test-run.txt", io.BytesIO(b"tools/run-title.sh\n"))
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

# --- the picker's record and picture -----------------------------------------
if [[ -n $picker_test ]]; then
python3 - "$title_id" "$picker_test" "$stamp" <<'PY'
import importlib.util, json, sys
from pathlib import Path
sys.path.insert(0, "tools")
from ps5_ftp import connect, remove_if_present
spec = importlib.util.spec_from_file_location("dt", "tools/deploy-title.py")
dt = importlib.util.module_from_spec(spec); spec.loader.exec_module(dt)
base = f"/data/homebrew/{sys.argv[1]}/picker"
frames, choice, *switch = sys.argv[2].split(":")
remember = {"remember": 1, "forget": 0}.get(switch[0], -1) if switch else -1
out = Path("klog") / f"picker-{sys.argv[3]}"
out.mkdir(parents=True, exist_ok=True)
expected = {"retroarch": "/app0/eboot.bin", "es-de": "/app0/es-de/es-de.bin", "none": ""}[choice]
with connect(**dt.load_settings()) as ftp:
    remove_if_present(ftp, f"{base}/picker-test.txt")
    rows = []
    for name in ("picker-test.jsonl", "screenshots/picker.ppm"):
        target = out / Path(name).name
        try:
            with target.open("wb") as stream:
                ftp.retrbinary(f"RETR {base}/{name}", stream.write)
            print(f"    saved picker/{name} to {target}")
        except Exception as error:
            target.unlink(missing_ok=True)
            print(f"    could not retrieve picker/{name}: {type(error).__name__}")
records = out / "picker-test.jsonl"
if records.exists():
    for line in records.read_text().splitlines():
        try:
            rows.append(json.loads(line))
        except ValueError:
            print(f"    picker: a record line that is not JSON: {line!r}")
picture = out / "picker.ppm"
if picture.exists():
    try:
        from PIL import Image
        Image.open(picture).save(picture.with_suffix(".png"))
        picture.unlink()
        picture = picture.with_suffix(".png")
    except ImportError:
        pass
for row in rows:
    print(f"    picker: {row['frames']} frames, chose {row['choice']}, next '{row['next']}'")
if rows and rows[-1] == {"frames": int(frames), "choice": choice, "remember": remember, "next": expected} and picture.exists():
    print(f"    picker test PASS: {frames} frames, a picture of the last ({picture}), then {choice}")
else:
    print(f"    picker test FAILED: wanted {frames} frames then {choice} ({expected!r}), got {rows}")
PY
fi

# --- the game's last frame (--game with --retroarch-frames) -------------------
if [[ -n $game ]] && (( retroarch_frames )); then
python3 - "$title_id" "$stamp" <<'PY'
import importlib.util, sys
from pathlib import Path
sys.path.insert(0, "tools")
from ps5_ftp import connect, list_names, remove_if_present
spec = importlib.util.spec_from_file_location("dt", "tools/deploy-title.py")
dt = importlib.util.module_from_spec(spec); spec.loader.exec_module(dt)
base = f"/data/homebrew/{sys.argv[1]}"
out = Path("klog") / f"game-{sys.argv[2]}.png"
with connect(**dt.load_settings()) as ftp:
    if "game-shot.png" in list_names(ftp, base):
        with out.open("wb") as stream:
            ftp.retrbinary(f"RETR {base}/game-shot.png", stream.write)
        remove_if_present(ftp, f"{base}/game-shot.png")
        print(f"    game: the last frame saved to {out}")
    else:
        print("    game: no picture of the last frame (RetroArch did not reach its frame count)")
PY
fi

# --- the pad script's pictures ------------------------------------------------
if [[ -n $pad_script ]]; then
python3 - "$title_id" "$stamp" "$pad_script" <<'PY'
import importlib.util, shutil, sys
from pathlib import Path
sys.path.insert(0, "tools")
from ps5_ftp import connect, list_names, remove_if_present
spec = importlib.util.spec_from_file_location("dt", "tools/deploy-title.py")
dt = importlib.util.module_from_spec(spec); spec.loader.exec_module(dt)
base = f"/data/homebrew/{sys.argv[1]}"
out = Path("klog") / f"pad-{sys.argv[2]}"
out.mkdir(parents=True, exist_ok=True)
shutil.copyfile(sys.argv[3], out / "pad-script.txt")
with connect(**dt.load_settings()) as ftp:
    remove_if_present(ftp, f"{base}/pad-script.txt")
    shots = sorted((name for name in list_names(ftp, base) if name.startswith("pad-shot-") and name.endswith(".png")),
                   key=lambda name: int(name[9:-4]) if name[9:-4].isdigit() else 0)
    for name in shots:
        with (out / name).open("wb") as stream:
            ftp.retrbinary(f"RETR {base}/{name}", stream.write)
        remove_if_present(ftp, f"{base}/{name}")
print(f"    pad script: {len(shots)} picture(s) saved to {out}")
PY
fi

# --- the menu capture (--menu-capture) -----------------------------------------
if (( menu_capture )); then
python3 - "$title_id" "$capture_run" <<'PY'
import importlib.util, io, sys
from pathlib import Path
sys.path.insert(0, "tools")
from ps5_ftp import connect, remove_if_present
spec = importlib.util.spec_from_file_location("dt", "tools/deploy-title.py")
dt = importlib.util.module_from_spec(spec); spec.loader.exec_module(dt)
out = Path("klog") / (sys.argv[2].replace("frontend-", "menu-") + ".png")
with connect(**dt.load_settings()) as ftp:
    data = io.BytesIO()
    try:
        ftp.retrbinary(f"RETR /data/homebrew/{sys.argv[1]}/shot.ppm", data.write)
    except Exception as error:
        print(f"    no menu capture on the console ({error})")
        sys.exit(0)
    remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/shot.ppm")
from PIL import Image
data.seek(0)
Image.open(data).save(out)
print(f"    menu capture: {out}")
PY
fi

# --- the frontend's pictures and logs ----------------------------------------
# Collected before RetroArch's logs, whose checks stop the script on a failure.
if [[ -n $frontend_capture ]]; then
python3 - "$title_id" "$capture_run" "$frontend_capture" <<'PY'
import importlib.util, json, sys
from pathlib import Path
sys.path.insert(0, "tools")
from ps5_ftp import connect, remove_if_present
spec = importlib.util.spec_from_file_location("dt", "tools/deploy-title.py")
dt = importlib.util.module_from_spec(spec); spec.loader.exec_module(dt)
base = f"/data/homebrew/{sys.argv[1]}/es-de"
out = Path("klog") / sys.argv[2]
out.mkdir(parents=True, exist_ok=True)
with connect(**dt.load_settings()) as ftp:
    remove_if_present(ftp, f"{base}/capture-test.txt")
    for name in ("capture-test.jsonl", "es-de-ps5.log", "stdout.txt", "ES-DE/logs/es_log.txt",
                 f"profile-{sys.argv[2]}.txt"):
        target = out / Path(name).name
        try:
            with target.open("wb") as stream:
                ftp.retrbinary(f"RETR {base}/{name}", stream.write)
            print(f"    saved es-de/{name} to {target}")
        except Exception as error:
            target.unlink(missing_ok=True)
            print(f"    could not retrieve es-de/{name}: {type(error).__name__}")
    records = out / "capture-test.jsonl"
    rows = [json.loads(line) for line in records.read_text().splitlines() if line.strip()] if records.exists() else []
    rows = [row for row in rows if row.get("run") == sys.argv[2]]
    for row in rows:
        remote = row["file"].replace("/app0/", f"/data/homebrew/{sys.argv[1]}/", 1)
        local = out / Path(remote).name
        if row["written"]:
            with local.open("wb") as stream:
                ftp.retrbinary(f"RETR {remote}", stream.write)
            remove_if_present(ftp, remote)
            try:
                from PIL import Image
                Image.open(local).save(local.with_suffix(".png"))
                local.unlink()
                local = local.with_suffix(".png")
            except ImportError:
                pass
        print(f"    {'process ' + str(row['pid']) + ', ' if 'pid' in row else ''}"
              f"frame {row['frame']} at {row['seconds']} s: {row['width']}x{row['height']}, "
              f"GL error {row['gl_error']}, sound {row.get('audio_driver') or 'none'} "
              f"({row.get('audio_playing', 0)} playing), {'saved to ' + str(local) if row['written'] else 'not written'}")
        if "interval_ms" in row:
            gap, swap = row["interval_ms"], row["swap_ms"]
            print(f"      {row['interval_frames']} frames before it ({row['presses']} presses): between swaps "
                  f"mean {gap['mean']} ms, p95 {gap['p95']}, max {gap['max']}, {gap['over_25']} over 25 ms; "
                  f"in the swap mean {swap['mean']} ms, p95 {swap['p95']}, max {swap['max']}")
            if row.get("slow"):
                print(f"      frames over 100 ms (seconds:ms@presses): {' '.join(row['slow'])}")
    wanted = [int(value) for value in sys.argv[3].split(",")]
    # Each ES-DE process (a game in between makes two) captures on its own clock; one
    # of them must have taken every picture, and none may have failed.
    by_process = {}
    for row in rows:
        by_process.setdefault(row.get("pid", 0), []).append(row["seconds"])
    if rows and all(row["written"] and row["gl_error"] == 0 for row in rows) and \
            any(seconds == wanted for seconds in by_process.values()):
        print(f"    frontend capture PASS: {len(rows)} pictures from {len(by_process)} process(es) in {out}")
    else:
        print(f"    frontend capture FAILED: wanted {wanted} from one process, got {by_process}")
PY
fi

# --- preserve development logs and optional buffered timing ------------------
python3 - "$title_id" "$profile" "$stamp" "$audio_test" "$expected_identity" "$core_test" "$relaunch_test" "$relaunch_run" "$display_modes_test" "$relaunch_image" "$picker_test" "$frontend_scroll" "$home_launch" <<'PY'
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
    remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/relaunch-test.txt")
    remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/display-modes-test.txt")
    from pathlib import Path
    if sys.argv[10] and not Path(f"dist/{sys.argv[1]}/{sys.argv[10]}").is_file():
        remove_if_present(ftp, f"/data/homebrew/{sys.argv[1]}/{sys.argv[10]}")
        # The deployment's record must not go on vouching for a file this removed.
        record = dt.deployed_record_path(dt.load_settings(), sys.argv[1])
        if record.is_file():
            entries = json.loads(record.read_text())
            if entries.pop(sys.argv[10], None) is not None:
                record.write_text(json.dumps(entries, indent=1, sort_keys=True))
    # RetroArch logs by mode (src/frontend_mode_ps5.cpp): a game a frontend started
    # (game mode) in retroarch-game.log, RetroArch's own in retroarch.log.
    game_mode = "launch" in sys.argv[12]
    names = ["retroarch.log"] + (["retroarch-game.log"] if game_mode else [])
    if int(sys.argv[7]):
        names.append("relaunch-test.jsonl")
    if int(sys.argv[9]):
        names.append("display-modes-test.jsonl")
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
            # RetroArch's own runs unless the session hands over to EmulationStation (a
            # relaunch to it, a picker test that chooses it or nothing) or is a home launch;
            # a game EmulationStation starts logs to retroarch-game.log.
            choice = sys.argv[11].split(":")[1] if sys.argv[11] else ""
            retroarch_runs = (not sys.argv[10].startswith("es-de/") and choice in ("", "retroarch")
                              and not int(sys.argv[13]))
            if (name == "retroarch.log" and retroarch_runs) or name == "retroarch-game.log":
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
            if name == "relaunch-test.jsonl":
                count, run = int(sys.argv[7]), sys.argv[8]
                rows = [json.loads(line) for line in target.read_text().splitlines() if line.strip()]
                rows = [row for row in rows if row.get("run") == run]
                events = [row for row in rows if "event" in row]
                entries = [row for row in rows if "event" not in row]
                for row in events:
                    print(f"    generation {row['generation']}: LoadExec did not replace the process: {row}")
                for row in entries:
                    print(f"    generation {row['generation']}: arguments name {row['argument_generation']}, "
                          f"{row['flexible_free'] / 1048576:.1f} MiB flexible free, pid {row['pid']}, {row['action']}")
                gaps = [(b["monotonic_ns"] - a["monotonic_ns"]) / 1e9 for a, b in zip(entries, entries[1:])]
                if gaps:
                    print(f"    restart to restart: {min(gaps):.2f} to {max(gaps):.2f} s")
                # A frontend image (es-de/...) is not eboot.bin: the chain ends when it starts,
                # and an eboot.bin it starts later (game mode) finds the test done.
                if sys.argv[10].startswith("es-de/"):
                    steps = [(row["generation"], row["action"]) for row in entries]
                    if events or not steps or steps[0] != (0, "restart") or any(step != (1, "continue")
                                                                               for step in steps[1:]):
                        raise SystemExit("Relaunch test failed: see the generations above")
                    print(f"    relaunch test PASS: handed over to /app0/{sys.argv[10]}")
                    print(f"    saved {name} to {target}")
                    continue
                checks = (
                    not events,
                    [row["generation"] for row in entries] == list(range(count + 1)),
                    all(row["argument_generation"] == row["generation"] for row in entries[1:]),
                    [row["action"] for row in entries] == ["restart"] * count + ["continue"],
                    # A restart starts as a cold launch does: no memory kept from the one before.
                    entries and all(abs(row["flexible_free"] - entries[0]["flexible_free"]) <= 16 << 20
                                    for row in entries),
                )
                if not all(checks):
                    raise SystemExit("Relaunch test failed: see the generations above")
                print(f"    relaunch test PASS: {count} restarts, every one with its arguments")
            if name == "display-modes-test.jsonl":
                rows = [json.loads(line) for line in target.read_text().splitlines() if line.strip()]
                frames = int(sys.argv[9])
                for row in rows[:-1]:
                    if "width" in row:
                        rate = row["frames"] / row["seconds"] if row["seconds"] else 0
                        print(f"    {row['width']}x{row['height']} at {row['refresh_millihertz'] / 1000:.2f} Hz: "
                              f"{row['frames']} frames in {row['seconds']:.2f} s ({rate:.1f} a second), "
                              f"{row['images']} images, {row['result']}")
                    else:
                        print(f"    setup failed: {row}")
                modes = [row for row in rows if "width" in row]
                checks = (
                    rows and rows[-1] == {"result": "PASS"},
                    len(modes) >= 2 and all(row["result"] == "ok" and row["frames"] == frames for row in modes),
                    (1920, 1080) in [(row["width"], row["height"]) for row in modes],
                )
                if not all(checks):
                    raise SystemExit("Display modes test failed: see the modes above")
                print(f"    display modes test PASS: {len(modes)} presentations; confirm each size filled the screen")
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
