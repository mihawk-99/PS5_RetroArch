# RetroAchievements and NetPlay on PS5

Online startup needs the qualified external Lapy service already running before
RetroArch launches. The application uses its existing cooperative SDK request;
no daemon or kernel code is linked into RetroArch. Without service readiness,
normal sandbox startup remains available, but outgoing online features do not.

Prepare the service using evidence/network-elevation/README.txt and apply
evidence/installed-paths/daemon-readiness.patch as described in that directory.
Use the daemon built for PPSA99169 and this SDK, with all preflight guards enabled.
The one-shot form serves one launch. The previously qualified bounded service
also covers the picker returning to RetroArch. No autoload installation is made
by this change, and a service from another title is not interchangeable.

In WebUI > global settings > Achievements, save both username and password,
enable achievements, and restart with the service ready. The prior login failure
came from a different saved username, not password punctuation or URL encoding.
Passwords, tokens, settings, game paths and saves stay on the console.

For NetPlay, both peers need compatible RetroArch/core versions and identical
content. A host must accept incoming TCP on its chosen port (default 55435),
or use RetroArch's relay. The PC firewall was active during this verification;
its inbound rules were not available, so the console joining a PC was verified
through the official relay. Console hosting was verified over the LAN. Router
settings and Sony domain blocking were not changed. Public lobby listing and
long Internet gameplay sessions are outside these captures.

## Reproduce the checks

Build all shipped targets:

    PS5_FRONTENDS='es-de picker' PS5_VULKAN_TEMPLATE="$PWD/build/picker-template" \
      bash tools/verify.sh format unit build integration evidence

Follow evidence/installed-paths/README.txt to prepare console.py and its runner
under ignored build/. Use private .env for console access. Keep every raw capture
outside the repository; do not copy saved configs into evidence. Verify console
idle and the previous daemon's completion before every new launch.

The console harness uses /usr/bin/retroarch, a matching FCEUmm core at
build/netplay-host/fceumm/fceumm_libretro.so and the existing minimal pc.cfg in
that directory. It generates its own diagnostic NES content, scripts three
controller presses, and uses temporary console settings with saving disabled.
The fixture polls both controllers into RAM and increments a counter. No game,
BIOS or save data is part of the fixture.

From the repository root, using fresh /tmp output directories:

    python3 -B evidence/online-features/console_netplay.py /tmp/netplay-join relay-join
    python3 -B evidence/online-features/analyse_netplay.py /tmp/netplay-join relay-join
    python3 -B evidence/online-features/console_netplay.py /tmp/netplay-host lan-host
    python3 -B evidence/online-features/analyse_netplay.py /tmp/netplay-host lan-host

The relay test starts a private PC-hosted session through the standard relay
protocol. A loopback forwarding socket captures its session identifier and wire
traffic. The console connects directly to the official relay. Nothing is posted
to the public lobby. The LAN-host test forwards the PC client's connection to the
console. Each direction's 24-byte RetroArch handshake is followed by framed
commands; the analyser counts input frames, nonzero controller values and CRC
messages. The console client's debug log records the actual local/remote state
CRC comparison, including mismatches; a handshake alone is not a passing result.

For RetroAchievements, use the owner's existing supported content and console
credentials with the installed-paths runner, overriding OUT to a fresh /tmp
folder. Run an achievements label and a badge-download label. Require successful
login, session start and achievement-set load; delete only the test thumbnail
00000.png before the badge check and validate its downloaded PNG. Do not log or
commit account values, tokens, content names, configuration or save files.

Capture.json contains sanitized results and raw-log hashes. The raw logs and
wire dumps are private /tmp artifacts. The executable hash ties these checks to
the shipped build. The additional patch is debug logging at upstream's existing
CRC comparison; it changes neither the wire protocol nor synchronization rules.

## Failures retained

The first full gate attempt was sandbox-blocked in 13 local-server test setups;
rerunning with local networking enabled resolved those errors. Adding the trace
required updating the intentional patch-count assertion from 253 to 254. The
24 focused frontend/patch checks passed after that update.

The first final-build relay capture proved frame/input traffic but contained no
state-comparison traces: the initial hook covered deferred checks, while this
session checked received CRCs directly. The hook was moved to that existing
comparison, the 24 focused checks passed again, and all shipped targets were
rebuilt and read-back verified before the final console tests.

Two forced-close host trials exceeded the one-shot daemon's 60-second teardown
observation window (wait_title_exit, error 60, counts 75/74). Closing the PC peer
first did not resolve that timeout. Subsequent preflights returned to the original
73/72 counts. Normal RetroArch quit passed cleanup; the timeout is retained as a
force-close limitation, not treated as either verified cleanup or a proven leak.
No daemon guard or timeout was weakened.
