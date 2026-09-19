# Testing and evidence

How this project decides that something is true, and what it keeps as proof.
`AGENTS.md` lists the gate commands; this file says what they mean, how to add to
them, and what counts as evidence.

## The four layers

| Layer | Runs | Answers | Must be |
| --- | --- | --- | --- |
| Format and static | every save | is it shaped right? | fast, no network |
| Unit | every commit | is the logic right in isolation? | deterministic, milliseconds |
| Integration | every commit | do the parts work together? | deterministic, seconds, no console |
| Target | every step | does it work where it ships? | scripted, repeatable, captured |

The first three run on the development machine and are the agent's own feedback
loop. The fourth runs on the console and is the only layer that can close a
step. A step is never closed by a mock of the console.

## What each gate runs

| Gate | Command | What it proves |
| --- | --- | --- |
| format | `tools/verify.sh format` | `tools/lint-shell.sh` parses every script in `tools/` (`bash -n`) and every python tool (`ast.parse`), rejects CRLF and byte-order marks, and refuses a committed console address or credential; `tools/lint-format.sh` then runs `clang-format --dry-run --Werror` over `src/`, `tests/` and `tooling/native/` against the repository's `.clang-format`. Formatters are not installed for shell or python on this machine, so this gate is syntax and policy rather than style. |
| unit | `tools/verify.sh unit` | `make test-unit` runs `tests/test_frontend.py`. Four kinds of check, none of which needs a console: the built `gfx_video_driver.c.o` really lists `video_ps5` before `video_null` in `video_drivers[]` (read from its relocations); the title and the frontend agree on `video_driver_t`, by compiling RetroArch's header with the frontend's own defines from `tools/retroarch-flags.sh` and comparing `sizeof` and the `poke_interface` offset against the object the title links - the fault that kept the menu off the screen, see `docs/FINDINGS.md`; the frame-layout function from `src/display.cpp` is compiled from the source by the host compiler, compared against `../PS5_Vulkan`'s own addressing over all 2,073,600 pixels, and pinned against a table of values so a change to it cannot be silent; and the built `dist/<TITLE_ID>/` is a fake self wrapping a 64-bit x86-64 ELF whose `param.json` names its own folder. |
| build | `tools/verify.sh build` | `bash tools/build-title.sh` fetches verified core inputs on first use, compiles RetroArch's sources with the cross toolchain, archives them, compiles `src/` with the same `-D` flags the archive was compiled with, links it with the pipeline's CRT, signs the result and records the folder's manifest. It is not bare `make app`: the things the link needs, and the defines the two sides must share, are set by that script and by nothing else. |
| integration | `tools/verify.sh integration` | `make test-integration` (the same tests as `unit`, from the other side of the build) followed by `tools/check-manifest.sh` on the built tree: every recorded file present and unchanged, nothing extra, `eboot.bin` a fake self, and `param.json` naming the folder it sits in. |
| evidence | `tools/verify.sh evidence` | `tools/evidence.py compare evidence/` replays every committed capture against its expectation and exits non-zero on a difference. It never contacts the console. |

`tools/verify.sh` runs them in that order and stops at the first failure. The
order is deliberate: a formatting failure is cheaper to fix than a staged
artifact that cannot load.

## Evidence

Evidence is a committed artifact plus the command that reproduces it. The shape
this project uses: **capture on the console, replay on the development machine,
compare the two.**

- **Capture.** A console run is captured by the target-layer tooling — the run's
  own stdout through the SDK's stdio, plus the console's kernel log for the
  window of the run. Raw captures are large and console-specific, so they stay in
  the git-ignored `klog/`; the distilled artifact is committed under
  `evidence/<step>/`.
- **Distil.** `tools/evidence.py distil` turns a raw capture into a small,
  machine-readable record: the ordered identity and event lines, a frame digest,
  a present count, and the driver and core names, with timestamps and process ids
  replaced by placeholders. A distilled record must let a stranger tell pass from
  fail by reading it.
- **Replay.** `tools/evidence.py compare` re-runs the deterministic part on this
  host — the same argument builders, the same config round-trip, the same digest
  computation — and compares its output with the committed record.
- **Compare.** A comparison is exact unless the record declares a tolerance, and
  a tolerance states its unit, its value and what makes it necessary, in
  `docs/FINDINGS.md`. A frame digest is compared exactly; a timing line is
  compared only when the record says which fields are bounded.

An artifact is only evidence if a stranger can tell pass from fail by reading it.
A screenshot, a green check with no input recorded, or a log line that says "ok"
is not evidence.

## Adding a test

- A bug fix lands with the test that fails without the fix. Write the test
  first, watch it fail, then fix — and say in the commit that it was seen
  failing.
- A new test goes next to the code it covers, follows the naming of its
  neighbours, and is added to the gate that runs it.
- A test that needs the console is split: the deterministic part runs in the
  integration gate, the console part becomes a captured artifact.
- Never weaken, skip or special-case a test to make a gate pass. If the test is
  wrong, fix the test in its own commit with the reason.

## Determinism

Unit and integration tests are deterministic: no wall-clock reads, no sleeps to
synchronise, no unseeded randomness, no network, no console, and no dependence on
the order tests run in. Time, randomness and I/O enter through an injectable seam
so the test can pin them. A test that cannot be made deterministic is a
target-layer test, not a unit test.

A capture is the one artifact that comes from an uncontrolled environment, so its
distillation removes everything that is not a decision: timestamps, pids, buffer
addresses, and the console's own boot noise.

## Tolerance and flakiness

- A comparison with a tolerance states the tolerance, its unit, and what makes
  it necessary.
- A flaky test is a bug in the test or in the code, and it is named in
  `docs/ACTIVE.md` under Open findings until it is fixed. It is never dealt with
  by retrying until green.
- A known-benign console message (a service warning, a busy-device notice) is
  recorded once in `docs/TROUBLESHOOTING.md` with its exact text, and every later
  run may say "known benign" only if the text matches.

## Native audio backend

The unit gate compiles the real `src/audio_ps5.cpp` against an explicitly clocked
AudioOut mock (`tests/audio_ps5_test.cpp`). It checks native arguments, 48 kHz rate
negotiation, byte counts, FIFO order across wrap, full/partial nonblocking writes,
blocking backpressure, zero-filled tails, pause/resume and output-error wakeup.
Condition barriers coordinate the test; timeouts only bound a deadlock failure.

`tools/run-title.sh --no-build --audio-test --watch 45` arms a consumed
`/app0/audio-test.txt` file. Before the frontend starts, the same `audio_ps5`
callbacks play four one-second PCM tones: left 440 Hz, right 660 Hz, repeated, at
12.5% peak with 10 ms boundary ramps. Announce the tones before launch and obtain
an audible/channel-order confirmation from the console owner. The test uses
1/255/257/1000-frame writes, drains and pauses/resumes between phases, and then
checks that an oversized nonblocking write accepts exactly one queue capacity.
It closes its port before RetroArch opens its own normal audio driver.

The runner retrieves `audio-test.json` into the ignored capture directory and
rejects failure, stale build identity, wrong format, lost frames, native errors
or queue-count mismatches. Expected: 48 kHz, 256-frame grains, four bytes/frame,
1,536-frame queue, 193,536 accepted and played frames, zero errors, 6,144 bytes
accepted from the oversized nonblocking write. The report cannot establish
speaker audibility; that confirmation is recorded separately in the evidence.
Normal launches remove leftover control files and never generate test tones.

## Platform paths and directory browsing

`tests/test_platform_paths.py` runs the actual native platform setup against an
isolated host filesystem, checking first-install config seeding, preservation
across a changed seed, startup argument preservation and accessible browser roots.
The real SDK directory adapter receives synthetic FreeBSD records to test multiple
batches, directory/file names and types, deleted records, EOF, denied opens,
valid fd zero, truncated/oversized records and missing string terminators. The
mounted-directory fixture rejects small reads; the previous 4 KiB variant fails
and the 64 KiB adapter passes.
Application-path tests compile the patched upstream function with procfs-related
calls replaced by aborts, checking the known executable path and small buffers.

On the console, retain the current build identity, platform directory summaries,
configuration load/save logs, GPU API results, and the owner's browser observation.
Read back the live config over FTP and verify its selected paths/settings. Launch
again without overwriting it to prove parsing/persistence across runs. No core or
ROM execution is implied by seeing directory entries. Raw filenames, settings,
crash dumps and console information stay in ignored captures; commit sanitized
counts, known port paths, crash symbols and acceptance results only.

The platform fixture also creates existing `0755` cores and `0775` content
directories and uses umask `0077`; every managed directory must become `0777` after initialization.
This test fails against the former mkdir-only implementation. After console
startup, `python3 tools/check-ftp-write.py` requires both mode `0777` and successful
FTP upload/readback/cleanup in all seven managed directories. Mode listings alone
are insufficient evidence that the FTP process can write.

The native input fixture compiles `src/input_ps5.cpp` against mocked pad services
and the actual upstream `input_state_wrap` and analog helpers. It checks raw button
capture, all stick directions, trigger scaling, invalid ports/axes/buttons,
user binding priority without the old direct mapping, menu deadzone behavior,
zero-sample repeat polling, interception/disconnect and driver teardown/reinit.
ELF relocations verify joypad registration and the built-in profile in the shipped
frontend. On console, confirm left-stick menu navigation, button/axis capture in
Settings > Input > RetroPad Binds > Port 1 Controls, and saved bindings after a
restart. Preserve the owner's live configuration when deploying.


## Libretro core artifacts

`tools/check-core.py <core.so> --report <report.json>` requires an ELF64 x86-64
FreeBSD/PS5 shared object, 16 KiB-compatible load segments, native runtime imports
including `libkernel_web.sprx`, and all 25 libretro callbacks in the dynamic symbol
table. It rejects Linux ELF ABI, payload kernel imports and absent callbacks.
The report deliberately keeps `console_loading_verified` false: ELF structure
cannot establish runtime loading. The title manifest check also checks the
staged FCEUmm artifact and requires identical `.info` metadata in `info/` and
`cores/`, covering both explicit and legacy empty core-info paths.

`tests/test_core_abi.py` builds real host fixture ELFs, marking only test fixtures
with the PS5 OSABI. Positive and negative cases exercise the parser through
readelf: missing callback, wrong runtime library, wrong ABI, 4 KiB segments and
truncated header. These fixtures are never staged or run on the console.

For the target test, upload a verified build, open RetroArch normally, and let
the owner select FCEUmm under Load Core and then a game under Load Content.
Collect `retroarch.log`, trace and kernel log with the build identity. Record
core load, content initialization and presented frames separately; a visible
core entry or `.info` name is not proof of loaded executable code. Owner game
paths remain in ignored logs. Keep normal XMB, Vulkan and audio startup working
when no core is selected.


## Native core loader diagnostics

`tests/test_core_loader.py` compiles and executes the real loader against a small
host ELF fixture. It tests relocation/import execution, reference counts,
reloads, missing imports, malformed header/table bounds, forbidden segment flags,
TLS, and invalid relocation targets/types. The fixture's OSABI byte is adjusted
only for this host test; shipped cores must pass the actual cross-build checker.
`tests/test_core_recovery.py` runs the upstream core-selection function before
and after the patch and checks rejection without marking the core as selected.

`tools/run-title.sh --no-build --core-test --watch 30` arms an opt-in no-game
console diagnostic. It executes eight FCEUmm load/symbol/API/identity/unload
cycles, rejects a missing core/export, and loads the core again with the frontend
resident. It then attempts missing-core selection/content startup through the
actual task functions and checks that the initialized menu context survives.
The runner collects `core-loader-test.json` and `core-recovery-test.json`, checks
their build identity and pass status, captures logs, and closes the title. Normal
runs clear the control file; diagnostics never run without the flag.

These diagnostics do not establish gameplay. The owner selects the core and
content manually in a separate captured run and confirms video, audio, controls
and return to XMB. Keep user content paths in ignored captures, not evidence.


`tests/test_core_frame.py` executes the actual XRGB8888 upload conversion with
red/green/blue and mixed colours, checks opaque alpha, distinct input/output row
pitches, untouched padding and in-place conversion. Real-core acceptance also
requires a trace with no Vulkan refusals or failed command buffers, rather than
inferring correctness from the core's successful initialization.
