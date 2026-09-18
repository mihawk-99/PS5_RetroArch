# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-18_

## Now

**One step from working: two files on the console are the wrong ones.** The title
crashes because `/data/homebrew/PPSA99005/eboot.bin` is a raw link ELF
(51,870,448 bytes, starts `7f454c46`) rather than the converted application image,
and `sce_module/libc.prx` is likewise the raw 1,335,962-byte module rather than
the signed 1,284,674-byte one. The correct pair sits in `dist/PPSA99005/` here,
and the working sibling title shows what a converted image looks like:
`PS5_Vulkan/dist/PPSA99988/eboot.bin` starts `4f153d1d`.

**The FTP route cannot place them, and the reason is recorded rather than
guessed.** The folder accepts everything else — markers, blobs of 2 MB, 8 MB,
60 MB, a blob of exactly the converted image's size, and the 512x512 icon, all
round-tripping byte-for-byte — but the converted image is returned as the other
file's bytes under its own name, an unused name, in place, and via a rename. A
fresh name receives `eboot.bin`'s bytes, which no ordering explanation covers.
The service also began refusing reads with `550 Broken pipe` under this load.
`docs/FINDINGS.md` carries the measurements and the two corrections to earlier
conclusions — including that the second session publishes `PPSA99988`, not this
title, which rules out the hypothesis first recorded here.

**Everything else is ready.** `tools/deploy-title.py` publishes and verifies,
`tools/console-launch.sh` launches and captures, `tools/evidence.py` replays the
two committed records, and the folder in `dist/PPSA99005/` is complete and
digest-checked.

## Next

1. Place `dist/PPSA99005/eboot.bin` and `dist/PPSA99005/sce_module/libc.prx` on
   the console by a route other than this FTP service — the console owner's own
   tooling, USB, or kstuff. Verify with `tools/deploy-title.py --check`: the line
   to look for is `eboot.bin magic: 4f153d1d`.
2. Launch it with the kernel log captured and record the outcome beside
   `evidence/psa-99005-startup-crash/`; the same capture after the change says
   whether the fault moved.
3. Then Option 2, the Vulkan driver, for which `../PS5_Vulkan` already implements
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
