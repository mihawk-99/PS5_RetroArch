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
| build | `tools/verify.sh build` | `bash tools/build-title.sh` fetches nothing, compiles RetroArch's sources with the cross toolchain, archives them, compiles `src/` with the same `-D` flags the archive was compiled with, links it with the pipeline's CRT, signs the result and records the folder's manifest. It is not bare `make app`: the things the link needs, and the defines the two sides must share, are set by that script and by nothing else. |
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
