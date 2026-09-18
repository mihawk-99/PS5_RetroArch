# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-18_

## Now

**M0.1, the documentation contract, is defined on this host and not yet
committed.** Every gate, milestone, invariant and environment fact in the read
path is written and the survey's four findings are recorded. Evidence: the
placeholder scan over `AGENTS.md docs tools .gitignore` returns nothing, and
`tools/verify.sh --list` prints the five gate commands. What it still owes: the
first commit, and the gate scripts those commands name — `tools/doctor.sh`,
`tools/fetch-upstream.sh`, `tools/stage.sh`, `tools/check-ps5-object.sh`,
`tools/lint-shell.sh`, `tools/check-manifest.sh`, `tools/check-gl-exports.sh`
and `tools/evidence.py` are specified in `docs/REFERENCE.md` and
`docs/TESTING.md` and do not exist yet.

**The build gate is a placeholder until that makefile exists.** It requires a
`Makefile` and runs `make app`, so today it fails naming the missing file
rather than building anything. It is red on a clean tree on purpose — a fresh
project whose gates all pass proves nothing — and M0.3 replaces it with the real
cross-compile. Named here so no later reader mistakes it for a passing gate.

## Next

1. M0.1's commit: the instantiating commit itself, with the survey evidence.
2. M0.2: `tools/doctor.sh` and `tools/fetch-upstream.sh`, pinning the SDK, the
   upstream tarball digest and `patches/series`.
3. M0.3: the cross-compile of `libretro-common` plus `tools/check-ps5-object.sh`.
4. M0.4: the staged title skeleton and its manifest.
5. The packaging decision (`docs/REFERENCE.md`, "Shipping as a PPSA title"):
   our own signer in `tools/` is the default, and adopting the boilerplate's
   pipeline is a step of its own if it is ever taken.
6. M2.1's measurement is the first step that needs a decision, not work: read
   the OpenGL package's export list and compare it with what RetroArch's GL
   driver asks for. It needs the OpenGL SDK built first — no archive exists in
   `../ps5-opengl-sdk-0.2.0` yet, only the source and the recipes.

## Working notes

- `source $PS5_PAYLOAD_SDK/toolchain/prospero.sh` before any build; prosperity
  clang is 22.1.8 targeting `x86_64-sie-ps5`, and the toolchain also provides
  `prospero-nm`, `prospero-objcopy` and `prospero-pkg-config`.
- The upstream 1.22.2 tarball is already extracted at `RetroArch-1.22.2/` in
  this directory and the pin's digest matches, so fetching is cheap here. Read
  it for what upstream does; it is a working copy, never an input — the build
  reads `vendor/retroarch`, and nothing under `RetroArch-1.22.2/` is committed.
- `readelf -dW` on a shipped ELF is the fastest way to catch the silent load
  failure: `libkernel_web.sprx` must be there and `libkernel_sys.sprx` must not.
- The console's address and credentials come from the ignored `.env`. The
  resident control payload and the console tooling are `../PS5_Vulkan`'s; this
  repository does not own a second one.

## Last verified

| Check | Result |
| --- | --- |
| `$CC -o t.elf t.c` with `prospero.sh` sourced | ELF 64-bit LSB pie executable, x86-64, version 1 (FreeBSD), 110,712 bytes |
| `tools/verify.sh --list` | prints format, unit, build, integration, evidence |
| `bash -n tools/verify.sh` | no syntax error |
| Placeholder scan over `AGENTS.md docs tools .gitignore` | no matches |
| `tools/verify.sh` (all gates) | not yet green: build is the M0.3 placeholder |
| Console run of anything | none: no step has reached the target layer yet |

## Open findings

- Whether the OpenGL package exports every entry point RetroArch's GL driver
  resolves through `glsym` is unmeasured. M2.1 measures it; if the difference is
  not empty, the Vulkan driver's rung is the blocking dependency for a presented
  frame, and the finding is recorded rather than worked around.
- Nothing in this repository has run on a console yet, so every target-layer
  claim in the docs is a specification, not a result.
