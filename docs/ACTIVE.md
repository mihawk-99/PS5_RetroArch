# Active work

_Updated: 2026-09-19_

## Now

**Genesis Plus GX is ported and owner-confirmed working.** The owner reports
“Sega Genesis works flawlessly” and explicitly confirms “Yes, transitions and
next game are clean.” Build and console evidence: `evidence/genesis-plus-gx-native/`.
No next core is selected.

## Verified build and evidence

- Frontend identity: `ecfcddd57febbb484c2e0724e95f43bbffb3ddd9b9c22aa9c1cf3e37af4a9445`.
- Genesis Plus GX source: `c2838c7dc4236fc2fe94e5dbd08b41486067918e`.
- Core SHA-256: `881b5118e6afe700d8de21df679b7e51a458c0bb2fe11603b96373d87f6a2fe0`.
  13,388,248 bytes; 25 callbacks. Pins, imports and port-input hashes are in evidence.
- All five host gates pass, 66 tests: `/tmp/genesis-verify.log`.
  Post-build format check: `/tmp/genesis-format-final.log`.
  Symbol-bearing ELF/map: `klog/genesis-ecfcddd5-symbols.elf` and corresponding .map.
- `tools/run-title.sh --no-build --core-test=genesis_plus_gx --watch 240` verified
  deployment, eight load/export/API/name/unload cycles and failed-load/menu
  recovery. Both reports pass. Content was selected manually by the owner.
- Two current-build launches are in trace. Frontend snapshots show a .md member
  loaded from an archive, XRGB8888 callbacks, 256x192 geometry, and returns to
  the 320x240 dummy menu. Owner verified clean menu and next-game transitions.
  Core-declared FPS/audio rates are metadata, not performance measurements.
- Zero Vulkan refusals, GPU API failure records or kernel fatal signals.
  Eight audio close reports have errors=0. Both launches reached rarch_main
  return 0 and native quit status 0 before the watch window ended; this is not
  a claim of 240 seconds of continuous gameplay.
- Raw: `klog/genesis-first-run.log`, `klog/run-PPSA99169-171959.log`,
  `klog/genesis-first-1789852896044371016-retroarch.log` (first launch),
  `klog/genesis-first-live-retroarch.log` (second launch),
  `klog/genesis-first-live-trace.txt` (both). Relaunch truncates retroarch.log;
  preserved timestamped snapshots prevent losing the earlier failures.
- FCEUmm, mGBA, Snes9x and FBNeo bytes match their accepted builds. Their prior
  evidence remains in native-core-loading/, mgba-native/, snes9x-native/ and
  fbneo-native/ under evidence/.
- PS5_Vulkan was not modified. Four archive hashes matched before/after build;
  exact linked hashes are in the evidence. The owner develops it concurrently.

## What changed

- `make genesis-plus-gx`: pinned official source and metadata using explicit
  project SDK wrappers, native import union and .info in both lookup locations.
  Attached metadata was reference only. No ROM or BIOS is shipped.
- Preserve RGB565 software rendering, filters/cursors and cached bitmap; convert
  into a separate bounded XRGB8888 callback buffer. Honour cropped byte offsets
  and the 720-pixel row pitch. The frontend's existing RGBA upload/cropping path
  remains unchanged, as does the Vulkan driver.
- Disable host-detected physical CD-ROM support and optional zstd weak tracing
  hooks. Keep CHD/disc-image codecs and useful logs. Correct update_geometry's
  unused return type. Reasons/pins and procedures are in REFERENCE/TESTING/DEPLOYMENT.
- Host tests cover every RGB565 colour through upstream packing and the actual
  GPU upload helper, viewport changes/crops, cached frames and rejected bounds.

## Named errors and remaining limits

- First frontend snapshot has five intentional missing-core recovery errors and
  two archive-extraction failures. The owner identified the failing content as
  ARCADE - Sega System 16 & 32: these boards belong to FBNeo, not Genesis Plus GX.
  Archive structure was not inspected; no general archive fix is inferred.
- Second-launch frontend snapshot has zero ERROR lines. Neither run records an
  invalid bitmap viewport or rejected XRGB8888 format.
- Console verification does not cover every supported Sega system, BIOS/disc/CHD,
  NTSC/interlace options, SRAM/state round trips, long-run A/V or performance.
  Silence/discard counters include menu/start/stop; zero backend errors is not
  proof of uninterrupted audio across all workloads.
- Existing upstream compiler warnings include old zlib prototypes, Tremor's
  long-to-int abs conversion and unused blip_discard_samples_dirty missing return.
  That blip helper has no callers in this source. Disc codecs remain unverified
  on console; warnings are not suppressed or treated as runtime acceptance.
- Loader still rejects TLS, legacy init/fini, nonempty preinit and unsupported
  relocations/dependencies. General unwind registration is absent. Cores share
  the frontend process; safe load rejection cannot isolate arbitrary core faults.

## Preserved platform and file locations

XMB stays default and RGUI selectable. Vulkan remains the presentation route;
video_ps5 stays selectable as CPU fallback. Native audio/input/binding, config
saving, FTP permissions and directory browsing remain. Useful logs are retained.
Deployments preserve live settings/games.

FTP base `/data/homebrew/PPSA99169/` corresponds to in-title `/app0`.
Use content/, system/, cores/, config/retroarch.cfg, savefiles/, savestates/.
Genesis Plus GX Sega CD BIOS files use system/ root; FBNeo uses system/fbneo/.
See `docs/DEPLOYMENT.md` for procedures and metadata-listed BIOS filenames.

## Operating notes

Uploads/runs are authorized without further permission. Never interrupt an
existing title; the runner checks idle before deployment and launch. Core/game
selection is manual. Keep private filenames and console details in ignored logs.
Separate appended trace launches by bss check and build identity. Preserve live
logs before relaunch. Owner supplies visual confirmation; camera cadence can
hide flicker. Do not infer manual closure solely from the final process count.
