# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-18_

## Now

**The installed title was launched and it crashes; the cause is measured.** The
console's `/data/homebrew/PPSA99005/eboot.bin` was a link-stage ELF (51,870,448
bytes) rather than a converted application image, and the loader faulted inside
it before `main()` ran: `SIGSEGV`, page fault at address `1`, thread `eboot.bin`,
with `/app0/sce_module/libc.prx` on the stack, then
`[Syscore App] App Crash`. The capture is committed as
`evidence/ppsa-99005-startup-crash/`, and the diagnosis is in `docs/FINDINGS.md`.
The converted image this repository produces is 49,968,821 bytes and validates
(`signed, plaintext`, 12 segments, `integrity: valid`).

**That folder then disappeared from the console.** The replace reported success
at every step and the directory was gone immediately afterwards; the neighbouring
homebrew folders were untouched and the control payload stayed healthy. Recorded
as its own finding, because it changes how an install must work here: write a
whole folder and verify it, never patch a file inside one.

**Everything needed to rebuild the title folder is local.** `dist/PPSA99005/`
holds the converted `eboot.bin`, the payload, the configuration, the loader
module, the identity and the icon, with a digest per file; the same payload also
survives on the console under `/data/homebrew/PS5_RetroArch/`. Nothing was lost
that cannot be regenerated or re-copied.

**The Option 1 baseline is still verified.** `evidence/m1-baseline-loads/` holds
that run, and `tools/evidence.py compare` now replays both records cleanly — with
one correction: a raw capture that has aged out of the ignored `klog/` tree is a
note, not a failure, since the distilled record is the evidence.

## Next

1. Rebuild `/data/homebrew/PPSA99005/` on the console from `dist/PPSA99005/`,
   whole folder first and verified after, then launch it with the kernel log
   being captured and record the result as the repaired run.
2. If it still faults, the next measurement is where: the earlier capture names
   the module on the stack, so the same capture with the new image says whether
   the crash moved.
3. Option 2 remains the Vulkan driver, and `../PS5_Vulkan` already implements
   every display entry point RetroArch's Vulkan path calls.

## Working notes

- `tools/fetch-ports.sh` provisions the ports image once (346 MB, digest-checked,
  cached in `.deps/pacbrew/`) and `tools/build-baseline.sh` runs `--check`
  without building anything. Both are cheap to re-run.
- The recipe's own build script is never edited: our changes are injected into a
  copy at `work/baseline/build.sh`, so `reference/ps5-retroarch/` stays the
  baseline it was measured against.
- This console's FTP service ignores the path in a listing command and answers
  deletes with 226. `tools/deploy.py` handles both; do not "simplify" that away.
- `source $PS5_PAYLOAD_SDK/toolchain/prospero.sh` before any manual PS5 compile;
  the clang is 22.1.8 targeting `x86_64-sie-ps5`.
- The console's address and credentials come from the ignored `.env`.

## Last verified

| Check | Result |
| --- | --- |
| `tools/build-baseline.sh` from cache | PASS in 34 s; 4 files staged; payload imports `libkernel_web.sprx`, not `libkernel_sys.sprx` |
| `tools/build-baseline.sh` after a source edit | PASS in 35 s |
| `tools/fetch-ports.sh` | PASS: v0.40.2 verified by SHA-256, SDL2 2.30.12 resolved |
| Console listing of `/data/homebrew/PS5_RetroArch/` | payload, config, manifest and launcher present; icon still at the folder root |
| Console FTP writes | FAILED: `550 Read-only filesystem` on the last attempt; reads fine |
| Console run of the baseline, `evidence/m1-baseline-loads/` | PASS: payload started under the homebrew launcher (pid 151), menu on screen, config written to the title folder |
| `tools/stage-ppsa.sh` | PASS: `dist/PPSA99005/` holds 6 files with a digest manifest |
| Image conversion | PASS: `eboot.bin` 49,968,821 bytes, `signed, plaintext`, 12 segments, `integrity: valid` |
| Launcher icon | PASS: `title/assets/retroarch.png` (640x640) resampled to 512x512, sha256 `65dcb224d62da0427f17589f16922918` |
| `tools/verify.sh` (all gates) | not yet green: the gate scripts it names still have to be written |

## Open findings

- The console's write path needs re-checking before the deploy can finish. If it
  stays read-only, the loader cannot see a new folder either.
- The recipe pins upstream 1.21.0 while our own tree is 1.22.2. The baseline is
  deliberately the recipe's version so it matches what the user already runs.
- Nothing in this repository has run on a console yet, so every target-layer
  claim in the docs is a specification, not a result.
