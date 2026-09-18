# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-18_

## Now

**The console path is proven end to end.** ProsperoLight — built on this machine
from the native pipeline — was placed on the console and **started flawlessly**.
That closes the question the last several hours were circling: sources compiled
for the title pipeline produce a title the console runs. Evidence: the owner
observed it running; the kernel log for the run contains **zero** fatal signals
(`klog/PPSA99002-134510.log`), and the converter's inspector reports
`container: signed, plaintext`, twelve segments, `integrity: valid`.

**The build recipe is one command.** `tools/build-native-app.sh <project>`
captures everything it took: the project's own vendored SDK, `PS5_CLANG=/usr/bin/clang`
(the wrapper's `clang-18` default is not installed and is not needed), the
`jsonschema` stand-in in `tooling/pystub/`, and a verified `runtime/libc.prx`.

**What the swap targets, measured on both sides.** RetroArch 1.22.2 is cloned
fresh here, and the pipeline's graphics layer is not a GPU stack at all: the
boilerplate's renderer calls `sceVideoOutOpen`, `sceVideoOutRegisterBuffers`,
`sceVideoOutSetBufferAttribute`, `sceVideoOutSetFlipRate` and
`sceVideoOutSubmitFlip` against its own direct memory. That is enough for a CPU
framebuffer and not enough for RetroArch's menu, which draws through a GPU display
context — so the frontend milestone has to be paired with a video driver.

**The graphics backend exists and is linkable.** `../PS5_Vulkan` provides
`build/driver/ps5/libps5vk.ps5.a` with `ps5vk_CreateInstance`,
`ps5vk_GetInstanceProcAddr`, `ps5vk_EnumerateInstanceExtensionProperties` and
`ps5vk_CreateDevice` defined, and the Vulkan headers live in
`../ps5-opengl-sdk-0.2.0/third_party/Vulkan-Headers/include/vulkan`. Both are
reached the same way the sibling project does it: `APP_INCLUDE_PATHS` for the
headers and `APP_STATIC_ARCHIVES` for the archive.

## Next

1. Stand up the RetroArch project: the native pipeline's scaffolding plus
   RetroArch 1.22.2's sources reached by include path, with the project's own
   `runtime/`, `tooling/` and `tools/build.sh` unchanged. First acceptance is that
   it *compiles* in that shape and links, with the frontend entering its main loop
   under a null video driver.
2. Write the video driver pair — a `gfx_ctx_driver_t` and the `video_driver_t`
   around it — against `libps5vk.ps5.a`, so the menu has a GPU context. That is the
   step that turns "it links" into "it draws".
3. Place and launch under its own title id, with `tools/console-run.sh`.
4. Only then bring the pad and audio drivers across.

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
