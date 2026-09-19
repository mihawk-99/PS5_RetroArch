# Active work

_Updated: 2026-09-19_

## Now

**Snes9x 1.63 is ported and owner-confirmed working.** The owner answered
“Works flawlessly” to gameplay colours/audio/input and menu/content-transition
checks. Build and console evidence is in `evidence/snes9x-native/`.
No next core is selected by this file.

## Verified build and evidence

- Frontend identity: `64bef2e7826e5a63a6644a933e24ef5403d28c754230c0038eb8a76b4baa1c53`.
- Snes9x source: `fae2fea08f74180759ef540ee94259213f503480`.
- Snes9x SHA-256: `e8b66c5f6656afe927181e3fb4fb5d13152ae525fc64705b59af5d9a847a7cac`.
  Core size 3,392,576 bytes; 25 callbacks; 4 initializers and 1 finalizer.
  Source, metadata, port-input and SDK hashes are in the evidence build report.
- All five host gates pass, 60 tests: `/tmp/snes9x-verify.log`.
  Preserved ELF: `klog/snes9x-64bef2e7.elf`.
- `tools/run-title.sh --no-build --core-test=snes9x --watch 180` verifies upload,
  eight load/export/API/identity/unload cycles, and failed-load recovery with the
  live frontend. Both diagnostic reports pass; missing-core errors are deliberate.
- Owner-loaded SNES archive negotiated XRGB8888, 256x224, 60.10 Hz and 32040 Hz
  audio; these are declared rates, not measured throughput. Live log also records
  return to the 320x240 dummy menu. Owner: “Works flawlessly.”
- Final trace: zero Vulkan refusals and GPU API failure records; kernel has zero
  fatal signal reports. Title remained open for 180s; runner closed it. This is
  a menu/game/menu window, not 180 seconds of uninterrupted gameplay.
- Raw: `klog/snes9x-first-run.log`, `klog/run-PPSA99169-161400.log`,
  `klog/snes9x-first-live-retroarch.log`, `klog/snes9x-first-live-trace.txt`.
- FCEUmm and mGBA core bytes are unchanged from their accepted builds. Their
  prior evidence is in `evidence/native-core-loading/` and `evidence/mgba-native/`.
- PS5_Vulkan was read only. Its four linked archive hashes stayed unchanged
  during this build and are recorded in `evidence/snes9x-native/driver-archives.json`.
  The owner develops that repository concurrently; no source revision is inferred.

## What changed

- `make snes9x`: pinned upstream libretro Makefile, explicit native SDK tools,
  official `.info` in info/ and cores/, integrated title/import-table build.
- Snes9x keeps RGB565 internally. Its video callback converts into a separate
  bounded XRGB8888 buffer for the existing frontend RGBA GPU upload path.
  All 65,536 RGB565 colours are host-tested end to end; padded pitch, cached
  source immutability, bounds and 256/512/602/1024 widths are covered too.
- Core-local C++ destructor registration prevents stale process-exit callbacks.
  Loader validates bounded fini arrays before any initializer, runs cleanup in
  reverse order at last close, then unmaps. Reload and malformed-file tests pass.
- Existing texture cropping, immutable cached uploads and native Quit remain.
  Vulkan is still the GPU presentation route; no frontend video patch changed
  for Snes9x and no change to PS5_Vulkan was needed.

## Limits

- Ordinary game/return-to-menu acceptance does not establish special-chip,
  subsystem BIOS, NTSC filter, HD Mode 7, hires/interlace or every-game coverage.
  The owner confirmed the requested test, but the captured geometry sequence
  does not independently establish a full cross-core transition matrix.
- Save RAM/state round trips, RTC, long-run A/V sync, measured performance,
  arbitrary Slang presets/history and linear-filter edges remain separate work.
- Loader still rejects TLS, legacy DT_INIT/DT_FINI, nonempty preinit arrays,
  unsupported relocations and additional dependencies; no unwind registration.
  Lifecycle callbacks must not reenter the loader. Faulty cores share the process.
- mGBA's 16-bit-only colour correction/interframe blending remain disabled in
  its XRGB build. Native-owned realloc retains native allocation ownership.
- Upstream Snes9x metadata still says display_version 1.61; runtime accurately
  reports 1.63 plus source revision. Official metadata was not rewritten.

## Preserved platform and file locations

XMB stays default and RGUI selectable. Vulkan remains active; video_ps5 stays
registered/selectable as CPU fallback. Native audio/input/binding, config saving,
FTP permissions (0777) and directory browsing remain. Useful logs are retained.
Deployments preserve live settings/games. No ROM or BIOS is shipped.

FTP base `/data/homebrew/PPSA99169/` corresponds to in-title `/app0`.
Use cores/, content/, system/, config/retroarch.cfg, savefiles/ and savestates/.
See `docs/DEPLOYMENT.md` for core build and upload procedures.

## Operating notes

Uploads/runs are authorized without further permission. Never interrupt an
existing title; the runner checks idle before deployment and launch. Core/game
selection is manual. Keep private filenames and console details in ignored logs.
Trace appends across launches: separate `bss check=` sections and verify identity.
Preserve live frontend logs before relaunch replaces them. Owner supplies images;
a matching camera cadence can hide flicker. Do not infer gameplay duration or
manual closure solely from the runner's final process count.
