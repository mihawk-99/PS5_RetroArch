# Active work

_Updated: 2026-09-19_

## Now

**mGBA GB/GBC/GBA archive loading and clean content transitions are verified.**
Owner confirms colours, native Quit, and the final “menu and next game are clean”
result. This closes the mGBA port step; no next core is selected by this file.

## Verified build and evidence

- Frontend identity: `82024b89e74bbad4ad2ce2e2916d1a49baf26ac325fef20fc71a2c168b862337`.
- mGBA source: `7a12d6d4b9acb14c0ae62c9166b6a2f3d08007f6`.
- mGBA SHA-256: `feb1922c9fe9dd3424b42f538a80362d0574b4dfa0134980d3fd14c52593cff6`.
  Core size 3,255,056 bytes; 25 libretro callbacks; 21 bounded init callbacks.
  Build/ABI/source/metadata hashes are in `evidence/mgba-native/`.
- All five host gates pass, 58 tests. Final build log:
  `/tmp/mgba-uv-format-final-verify.log`. ELF: `klog/mgba-82024b89.elf`.
- Console command: `tools/run-title.sh --no-build --core-test=mgba --watch 180`.
  Eight load/export/API/identity/unload cycles and live-menu recovery pass.
  Deliberate missing-core errors belong to the successful recovery test.
- Final owner confirmation: “Menu and next game are clean.” Captured sequence:
  dummy 320x240 -> GBA 240x160 -> dummy -> GB/GBC 160x144 -> dummy. Earlier owner
  tests individually confirmed GB, GBC and GBA archives load, correct colours,
  and working native Quit. Do not infer measured FPS from core-declared rates.
- Raw logs: `klog/mgba-uv-crop-run.log`, `klog/run-PPSA99169-155435.log`,
  `klog/mgba-transition-live-retroarch.log`, `klog/mgba-transition-live-trace.txt`.
  Frontend returned status 0 and called native exit before the runner's window
  ended; this is not 180 seconds of uninterrupted gameplay.
- PS5_Vulkan was only read, never edited by this task. The owner develops it
  concurrently. `evidence/mgba-native/driver-archives.json` records the actual
  archive hashes, unchanged through this build; no source revision is inferred.

## What changed

- `make mgba`: pinned upstream CMake libretro build, native SDK, GB/GBC/GBA,
  official metadata in info/ and cores/, no SDL/Qt or payload CRT in the core.
- Native loader validates relocated C init arrays before running callbacks;
  generated imports cover both cores, native directory rewind and local time.
- Explicit `/app0/config/mgba` avoids the crashing native getcwd import.
  Mapped allocations >=1 MiB allow the previously failing 16 MiB 7z decode.
- mGBA native XBGR is converted to declared XRGB without changing its renderer
  buffer. Vulkan menu textures consistently use RGBA; writable software
  framebuffer sharing is disabled to preserve cached source pixels.
- Patch 0078 crops core UVs to initialized pixels in padded textures, using
  per-sync VBOs. It fixes the observed stale image/flicker after Close Content.
- Native Quit calls `sceSystemServiceLoadExec("exit", nullptr)` after cleanup.
  Failed iterations and their evidence are retained in `evidence/mgba-native/`.

## Limits

- Save RAM/state round trips, RTC correctness, long-run A/V sync and measured
  performance are not verified. Native-owned realloc remains on the native heap.
- mGBA's 16-bit-only colour correction/interframe blending paths are not enabled
  in this XRGB8888 build. Arbitrary Slang presets/history and linear-filter edge
  behaviour are separate acceptance work.
- The final transition capture uses mGBA and RGUI; it does not independently
  establish a fresh FCEUmm/XMB transition matrix. FCEUmm remains included with
  its earlier verified core unchanged (`016a47d`, `evidence/native-core-loading/`).
- Loader rejects TLS, legacy init/fini, nonempty finalizer/preinit arrays,
  unsupported relocations and additional dependencies. It is not a general
  dynamic linker; a faulty core can still crash the shared process.

## Preserved platform and files

XMB remains default, RGUI selectable; Vulkan remains the active GPU route and
`video_ps5` remains the selectable CPU fallback. Native audio, analog menu
navigation/binding capture, config persistence and directory browsing remain.
Useful frontend/core/audio/error logs are retained. Managed directories use 0777;
deployment preserves live settings and games. No ROM or BIOS is shipped.

FTP base `/data/homebrew/PPSA99169/` corresponds to in-title `/app0`.
Use cores/, content/, system/, config/retroarch.cfg, savefiles/ and savestates/.
See `docs/DEPLOYMENT.md` for the build/upload procedure.

## Operating notes

Uploads/runs are authorized without further permission. Never interrupt an
existing title; the runner checks idle before deployment and launch. Core/game
selection is manual. Keep private filenames and console details in ignored logs.
Trace appends across launches: separate `bss check=` sections and verify identity.
Preserve live frontend logs before relaunch replaces them. Owner can supply
images; a matching camera cadence can hide flicker. Do not infer duration or
manual closure solely from the runner's final process count.
