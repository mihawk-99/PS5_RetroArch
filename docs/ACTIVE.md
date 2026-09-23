# Active work

_Updated: 2026-09-23_

## Now: 120 Hz by default, on the current driver

**RetroArch presents at 119.88 Hz where the display allows it, and at 59.94 Hz where
it does not, with cores at their own speed either way.** The title declares
high-frame-rate output (`sce_sys/param.json` `attribute3` 0x80040), so
`../PS5_Vulkan` (8577153, jobs/r51-output-mode) offers a 3840x2160 119.88 Hz mode
first and selects it at swapchain creation. Patch 0084 makes the display context
pick the largest mode and then the highest refresh (upstream ignored refresh, or
rejected any mode more than 1 Hz from a saved 59.94); 0085 makes
`video_refresh_rate` follow the mode it got and turns a saved swap interval of 1
into automatic above 100 Hz, so a 60 FPS core presents each frame for two
vblanks. The seed config ships `video_refresh_rate 119.88` and
`video_swap_interval 0` for fresh installs; an existing config is corrected at run
time. Evidence: `evidence/output-120hz` (FCEUmm, 1,800 frames in ~30 s at 1,199
presents per 10 s) and `evidence/output-60hz-fallback` (same build without the
metadata: 59.94 Hz, 600 presents per 10 s, same speed).

**The title now links the current driver**, hundreds of driver commits past
`8b311e3`: the mapped-only target flush, the bounded marker spin, parallel blits,
the NIR cache, the per-build shader cache and the output-mode selection. Game and
menu launched and ran on it; a systematic per-core recheck on it has not been done.

**`args.txt` can launch content.** `-L core` and a path in `/app0/args.txt` now
start a game directly (main.cpp drops `--menu`, which RetroArch refuses beside
content), which is how a run measures a core without a pad.

**Rebuilds take seconds** (docs/PHASE_LOG.md): a no-change rebuild 269 s -> 2.2 s,
a port-file or driver change ~2.4 s; deployment uploads only changed files
(`tools/deploy-title.py --all` for everything).

## Next

1. Recheck each core on the current driver (menu, FCEUmm, mGBA, Snes9x, FBNeo,
   Genesis Plus GX), colours and audio included.
2. PPSSPP from scratch (the current core crashes on its first frame, parked below).
3. Simplify the port without losing behaviour.

## PPSSPP Track A: parked

Plan: [PPSSPP_Implementation_Plan.md](../PPSSPP_Implementation_Plan.md); platform
analysis: [PPSSPP_Core_Plan.md](../PPSSPP_Core_Plan.md). The core builds from pinned
`f293b10`, is ABI-checked, linked and loaded on the console (`evidence/ppsspp-native/`),
and crashes on its first frame: a SIGSEGV on a worker thread during
`ThreadManager::Init`, inside `libkernel.sprx` (`pthread_get_specificarray_np+0x3b`
from `pthread_create_name_np`).

## Accepted baselines

- Native byte order on driver `8b311e3`: I accepted the colours with the red/blue
  compensation gone (`evidence/driver-1.0-native-byte-order/`).
- XMB: large-list allocation failures fixed (`601e575`), crash-free navigation
  (`docs/XMB_LIST_SAFETY.md`, `evidence/xmb-safe-list-run/`).
- Gameplay: Genesis Plus GX colours, sound, controls and transitions accepted;
  FCEUmm, mGBA, Snes9x and FBNeo evidence in `native-core-loading/`, `mgba-native/`,
  `snes9x-native/`, `fbneo-native/`.
- Thumbnail and configuration-reset fixes (patches 0081/0082).

## Named errors and remaining limits

- First frontend snapshot has five intentional missing-core recovery errors and two
  archive-extraction failures; I identified the content as Sega System 16 & 32,
  which belongs to FBNeo. No general archive fix is inferred.
- Console verification does not cover every supported system, BIOS/disc/CHD, NTSC and
  interlace options, SRAM/state round trips, long-run A/V or performance. Zero backend
  errors is not proof of uninterrupted audio across all workloads.
- Upstream compiler warnings (old zlib prototypes, Tremor's long-to-int abs conversion,
  an unused blip helper's missing return) are neither suppressed nor treated as
  acceptance.
- The loader still rejects TLS, legacy init/fini, nonempty preinit and unsupported
  relocations/dependencies; cores share the frontend process, so a rejected load is safe
  but an arbitrary core fault is not isolated.
- The withdrawn path has the console history; the native byte order does not yet. A
  colour regression would show as red and blue exchanged in the menu, in core frames or
  in thumbnails, and is the first thing to check on the next run.

## Preserved platform and file locations

XMB stays default and RGUI selectable. Vulkan remains the presentation route; video_ps5
stays selectable as CPU fallback. Native audio/input/binding, config saving, FTP
permissions and directory browsing remain. Deployments preserve live settings/games.

FTP base `/data/homebrew/PPSA99169/` corresponds to in-title `/app0`. Use content/,
system/, cores/, config/retroarch.cfg, savefiles/, savestates/. Genesis Plus GX Sega CD
BIOS files use system/ root; FBNeo uses system/fbneo/. See `docs/DEPLOYMENT.md`.

## Operating notes

Uploads are authorized; check console idle before deployment. A launch without args.txt needs my
intervention. Start klog first, preserve old logs, and capture allocation, frontend and
trace logs. Never interrupt another title. No new core or unrelated driver work is
assigned by this file.
