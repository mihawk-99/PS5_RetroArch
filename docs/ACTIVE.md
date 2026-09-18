# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-18_

## Now

**The frontend compiles from RetroArch's own build.** `tools/retroarch-sources.sh`
runs RetroArch's `./configure` and `make info` and prints the objects a link needs;
`tools/build-retroarch.sh` compiled **225 of 244** with the pipeline's compiler. The
remainder is Linux-only (udev, xkb, linuxraw input) plus zstd, and is out of scope
for this console. Evidence: the object files under `build/ra/obj/`.

**The scaffold title runs on the console.** This repository's own title —
the pipeline's scaffolding, `src/display.cpp` and `src/main.cpp` — was deployed by
FTP and launched, and stayed up: the capture holds zero fatal signals and the
control payload reported it running. That is the display path RGUI will present
through.

**The video driver is written.** `src/video_ps5.cpp` implements the
`video_driver_t` and the `video_poke_interface_t` RGUI needs, and compiles against
RetroArch's headers.

**RGUI is the target, and that is a measured choice.** RGUI references the menu
display context zero times; XMB references it 72 times and every backend in
`gfx_display_ctx_drivers[]` is a GPU API with no software entry. So RGUI runs over
VideoOut alone, and XMB waits for `../PS5_Vulkan`'s driver.

**Everything websrv-derived is deleted.** The Option 1 baseline tree, its
artifacts, the ports cache and the conversion tools are gone, and the docs no
longer describe that route.

## Next

1. Register `&video_ps5` in RetroArch's `video_drivers[]` as a patch under
   `patches/`, and reconcile the entry point — `retroarch.c` defines its own `main`
   and the pipeline supplies `_start`.
2. Link the 225 objects with `src/` through the pipeline's CRT and runtime, so the
   title builds the way ProsperoLight's does.
3. Deploy by FTP, launch with `tools/console-run.sh`, read the console's log, fix,
   repeat — until RGUI is on screen.
4. Then XMB, which needs a Vulkan-backed display context over `../PS5_Vulkan`.

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
