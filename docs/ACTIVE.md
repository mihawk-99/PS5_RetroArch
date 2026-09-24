# Active work

_Updated: 2026-09-24_

## Now: Dolphin (GameCube) with Wind Waker

**Goal:** Wind Waker correct, full speed, with audio, input and saves, stable
in long play; then the 6x/UberShader/EFB/MSAA torture profiles. Driver work it
needs goes into ../PS5_Vulkan as general fixes with their own probes.

**Where it is.** The core (libretro/dolphin `c6630001e0`, Dolphin 2609) builds
with `tools/build-dolphin.sh` and one patch, `patches/dolphin/ps5-port.patch`,
and ships in the title. Wind Waker boots to its title screen and plays its
intro with JIT64 and fastmem, zero driver refusals and clean audio
(`evidence/dolphin-wind-waker-boot`); save states save and load
(`evidence/dolphin-save-states`).

What the bring-up needed, by layer:

- **Platform (core patch):** guest RAM is one direct-memory allocation mapped
  at every mirror (a shared-memory object's views were charged to flexible
  memory and ran out); the fastmem arena is a kernel range reservation away
  from the GPU window; JIT code is mapped RW, made RWX, at a hint of
  0x3_0000_0000 (the console refuses an unplaceable hint); the fault handler
  reads the console's shifted mcontext and takes SIGBUS; a 32 MiB code cache;
  thread naming skipped; the large entry-point map off. libc entry points the
  console lacks are in `tooling/dolphin/ps5-libc-shims.cpp`.
- **Title:** `openat`/`fdopendir`/`unlinkat`/`fchmodat` and libc's `opendir`
  family are implemented or rerouted for the title's libc++
  (`src/ps5_directory.cpp`, `--wrap`); `utimensat` over `utimes`; small
  allocations go to direct memory first, and a refused libc realloc moves the
  block there (`src/memory_ps5.cpp`).
- **Frontend patches:** 0089 enables VK_KHR_get_physical_device_properties2 on
  the instance; 0090 keeps the synchronous readback inside the caller's
  viewport (the save-state thumbnail overran and crashed).
- **Driver (../PS5_Vulkan):** R57 clamp-to-border samplers, R58 primitive
  restart, one-layer array depth views, an opt-in SPIR-V dump.

**Open:** the mountain on the title screen is missing (a texture path, not yet
located); performance and long-play stability are unmeasured; the torture
profiles have not started. Test runs: `/app0/dolphin-options.txt` overrides
core options for one run (test file, removed on any other launch).

## Next

1. Find the missing mountain: A/B the driver's mip, EFB-copy and texture paths
   against a saved in-game state.
2. Measure speed at 1x with JIT64 and fastmem; then long play.

## Accepted baselines

- Native byte order on driver `8b311e3`: I accepted the colours with the red/blue
  compensation gone (`evidence/driver-1.0-native-byte-order/`).
- XMB: large-list allocation failures fixed (`601e575`), crash-free navigation
  (`docs/XMB_LIST_SAFETY.md`, `evidence/xmb-safe-list-run/`).
- Gameplay: Genesis Plus GX colours, sound, controls and transitions accepted;
  FCEUmm, mGBA, Snes9x and FBNeo evidence in `native-core-loading/`, `mgba-native/`,
  `snes9x-native/`, `fbneo-native/`.
- Thumbnail and configuration-reset fixes (patches 0081/0082).
- PPSSPP (God of War: Ghost of Sparta, Yu-Gi-Oh! GX Tag Force at 10x): picture,
  full speed at 120 Hz, save states, fast-forward, close and reopen.

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
