# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-18_

## Now

**An existing PS5 RetroArch payload recipe is in the repository as a read-only
baseline.** `reference/ps5-retroarch/` holds the twelve files of
`ps5-payload-dev/websrv`'s `homebrew/RetroArch/` at commit `1afd476`, with
`PROVENANCE.txt` naming the source, the revision and every file's digest. It
already solves the fetch-patch-build shape, the title icon and the config seed;
it renders through a software SDL2 driver with every GPU path switched off, and
it launches through that project's own loader. Evidence: the clone, the file list
and the digests are recorded in `docs/FINDINGS.md` and in the recipe's
`PROVENANCE.txt`. What it still owes: the two differences named below, and a
decision on whether to run it once as-is for a known-good console baseline.

**Its first prerequisite is missing here, and that is the next thing to settle.**
The recipe enables SDL2; this host has no SDL2 for the PS5 build —
`prospero-pkg-config --exists sdl2` exits 1 and the sysroot's `user/homebrew`
holds an empty `include/`. Either the ports image that provides it is installed,
or the build drops that switch for our own graphics flags. Recorded in
`docs/FINDINGS.md`; the answer decides whether the console baseline runs the
existing software path or ours.

**M0.1, the documentation contract, is defined on this host and committed.**
Every gate, milestone, invariant and environment fact in the read path is
written and the survey's findings are recorded. `tools/verify.sh` exists and
fails at its first gate naming the script that gate waits for. What it still
owes: `tools/doctor.sh`, `tools/fetch-upstream.sh`, `tools/stage.sh`,
`tools/check-ps5-object.sh`, `tools/lint-shell.sh`, `tools/check-manifest.sh`,
`tools/evidence.py` and our own makefile, all specified in `docs/REFERENCE.md`
and `docs/TESTING.md`.

**The build gate is a placeholder until our makefile exists.** It requires a
`Makefile` and runs `make app`, so today it fails naming the missing file rather
than building anything. It is red on a clean tree on purpose — a fresh project
whose gates all pass proves nothing — and M0.3 replaces it with the real
cross-compile. Named here so no later reader mistakes it for a passing gate.

## Next

1. Settle SDL2: install the ports image the recipe needs, or take the decision to
   drop `--enable-sdl2` for our own graphics flags. One step, one recorded
   answer.
2. M0.2: `tools/doctor.sh` and `tools/fetch-upstream.sh`, pinning the SDK, the
   upstream tarball digest and `patches/series`, with the recipe's fetch step as
   the starting point and its pin reviewed rather than copied.
3. M0.3: the cross-compile of `libretro-common` plus `tools/check-ps5-object.sh`.
4. M0.4: the staged title folder, reusing the recipe's icon and config seed.
5. The signer: our own `eboot.bin` wrapping step, which is what turns the payload
   into a PPSA title (`docs/REFERENCE.md`, "Shipping as a PPSA title").

## Working notes

- `source $PS5_PAYLOAD_SDK/toolchain/prospero.sh` before any build; the clang is
  22.1.8 targeting `x86_64-sie-ps5`, and the toolchain also provides
  `prospero-nm`, `prospero-objcopy` and `prospero-pkg-config`.
- The upstream 1.22.2 tarball is already extracted at `RetroArch-1.22.2/` in this
  directory and the pin's digest matches, so fetching is cheap here. Read it for
  what upstream does; it is a working copy, never an input — the build reads
  `vendor/retroarch`, and nothing under `RetroArch-1.22.2/` is committed.
- The recipe pins upstream **1.21.0**; our pin is **1.22.2**. Ours is newer, so
  its configure switches and its two source edits are inputs to review, not
  values to copy.
- `readelf -dW` on a shipped ELF is the fastest way to catch the silent load
  failure: `libkernel_web.sprx` must be there and `libkernel_sys.sprx` must not.
- The console's address and credentials come from the ignored `.env`. The
  resident control payload and the console tooling are `../PS5_Vulkan`'s; this
  repository does not own a second one.

## Last verified

| Check | Result |
| --- | --- |
| Sparse clone of `homebrew/RetroArch`, `1afd476` | 12 files, 48 KB, copied to `reference/ps5-retroarch/` with digests |
| `prospero-pkg-config --exists sdl2` | exits 1: SDL2 is not visible to the PS5 build on this host |
| `$CC -o t.elf t.c` with `prospero.sh` sourced | ELF 64-bit LSB pie executable, x86-64, version 1 (FreeBSD), 110,712 bytes |
| `tools/verify.sh --list` | prints format, unit, build, integration, evidence |
| Placeholder scan over `AGENTS.md docs tools .gitignore` | no matches |
| `tools/verify.sh` (all gates) | not yet green: build is the M0.3 placeholder |
| Console run of anything | none: no step has reached the target layer yet |

## Open findings

- The recipe's SDL2 path cannot be built here until the ports it needs are
  installed, and whether its software renderer is worth a console baseline run
  is undecided. Both are settled by the first step in "Next".
- Whether a graphics backend exports every entry point RetroArch's driver
  resolves is unmeasured. M2.1 records it; if the difference is not empty, it is
  a worklist item for `../PS5_Vulkan`, not a reason to change backend.
- Nothing in this repository has run on a console yet, so every target-layer
  claim in the docs is a specification, not a result.
