# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-18_

## Now

**The PPSA title folder is complete: `dist/PPSA99005/`.** It holds the signed
application image `eboot.bin` (49,968,821 bytes), the loader module
`sce_module/libc.prx`, the title's identity and 512x512 icon under `sce_sys/` (generated from
`title/assets/retroarch.png`),
the configuration seed, the payload beside them and a digest manifest.
`tools/stage-ppsa.sh` assembles it and exits 0 only when every piece is there.
Evidence: the folder's `manifest.sha256` and the converter's own inspection of
the image — `container: signed, plaintext; segments: 12; authority:
0x3100000000000002; program type 0x1; integrity: valid`. What it still owes: a
console run. The folder is built and validated here, not yet installed.

**Getting there took three image-format requirements, all now satisfied and
recorded.** The link is not the compiler driver's link any more: it goes through
`linker/ps5-pie.ld` (the layout that leaves room for the console's process
parameters), the SDK's startup objects (without them there is no `_start` and the
entry point stays 0), `platform/ps5_image_symbols.S` (the image's own boundary
symbols, which no stub can export) and `--exclude-libs=ALL` (without it the
linker exports symbols pulled from static libraries and the converter refuses the
image). `tools/prospero-clang-link` is the shim that applies all of it, and
`docs/FINDINGS.md` carries each requirement with the exact error that revealed it.

**The Option 1 baseline remains verified.** `evidence/m1-baseline-loads/` holds
the console run: payload started under the homebrew launcher, menu on screen,
configuration written into the title folder.

**The console is shared with the PS5_Vulkan session; every launch, upload and
install is asked for first** (`docs/DEPLOYMENT.md`). Nothing has been sent.

## Next

1. Ask for a console window, then install `dist/PPSA99005/` on the console and
   capture the run as `evidence/m-ppsa/`: the home screen listing the title, the
   title starting from it, and the menu on screen.
2. If the installed title does not start, the first thing to check is the
   loader's own message: it names the reason rather than failing silently
   (`docs/TROUBLESHOOTING.md`).
3. Option 2: the Vulkan driver. `../PS5_Vulkan` implements every WSI entry point
   RetroArch's display-based Vulkan path calls, so the remaining questions are
   the driver's consumer package and what RetroArch's renderer needs beyond it.

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
