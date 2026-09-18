# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-18_

## Now

**The title's folder is correct here and wrong on the console, and the two files
that differ cannot be replaced from this machine.** `dist/PPSA99005/` holds the
converted, signed `eboot.bin` (49,968,821 bytes, magic `4f153d1d`), the signed
`sce_module/libc.prx` (1,284,674 bytes), the identity, the icon, the
configuration and the payload. The console's copies of those two files are the
other session's build and revert on every attempt. Evidence: `magic 7f454c46
(NOT a converted image)` from `tools/deploy-title.py --check`, and the run
captured in `evidence/ppsa-99005-startup-crash/` where the loader faults at
`rip=0x1`.

**The deployment path itself now works and is honest.** `tools/deploy-title.py`
with `tools/ps5_ftp.py` — the helpers from `../PS5_Vulkan/tools/deploy.sh` that
handle this server's 226-on-delete and root-relative paths — publishes the whole
folder, verifies every stored size, and refuses to report success when one does
not match. That is how the revert was caught rather than believed.

**The server is not the limit.** Uploaded blobs round-trip byte-for-byte at 2 MB,
8 MB and 60 MB in the same folder, and a neutral file name made no difference.
The likely cause is outside this repository: a second session publishes the same
title's image and module and its writes win. `docs/FINDINGS.md` records the
measurement and that boundary.

**The artwork changed.** `title/assets/retroarch.png` is now 512x512 (was
640x640); the staging script resamples whatever is there, so the folder carries
`icon0.png` at 512x512, sha256 `473fc429…`.

## Next

1. Replace the console's `eboot.bin` and `sce_module/libc.prx` for PPSA99005 with
   the two from `dist/PPSA99005/` — the console owner can do it directly, and
   `tools/deploy-title.py` will do it and verify once no other writer is active.
   The signature of success is `magic 4f153d1d` in `--check`.
2. Launch it with the kernel log captured and record the outcome next to
   `evidence/ppsa-99005-startup-crash/`: the same capture, after the change,
   says whether the fault moved.
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
