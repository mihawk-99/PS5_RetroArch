# Active work

_Updated: 2026-09-24_

## Now: Dolphin (GameCube) with Wind Waker

**Goal:** Wind Waker correct, full speed, with audio, input and saves, stable
in long play; then the 6x/UberShader/EFB/MSAA torture profiles. Driver work it
needs goes into ../PS5_Vulkan as general fixes with their own probes.

**Where it is.** The core (libretro/dolphin `c6630001e0`, Dolphin 2609) builds
with `tools/build-dolphin.sh` and one patch, `patches/dolphin/ps5-port.patch`,
and ships in the title. Wind Waker boots with JIT64 and fastmem, save states
save and load, and from my save state on Outset Island the 3D scene, Link and
the HUD draw correctly (`evidence/dolphin-restart-strips`): a ten-frame FIFO
log of Link on the ship deck matches desktop Dolphin to resampling (mean
difference 1.13-1.16, from 64-71).

What the bring-up needed, by layer (details in `docs/PHASE_LOG.md`):

- **Platform (core patch):** guest RAM as one direct-memory allocation mapped at
  every mirror, the fastmem arena away from the GPU window, JIT code at
  0x3_0000_0000, the console's fault context, a 32 MiB code cache. Single-core
  FIFO playback (`Core/Core.cpp`) returns to retro_run like the CPU thread
  does; it hung before.
- **Title and frontend:** the `*at` directory family, direct-memory
  allocations, patches 0089 and 0090.
- **Driver (../PS5_Vulkan):** R57 border colours, R58 primitive restart, R59
  inverted depth, R60 large stages, R62 uniform buffers as byte ranges (fog and
  matrices), R64 restart state behind SQ_NON_EVENT (the corrupt 3D scene), R65
  restart off after a copy's split (stray triangles, minimap).

**Test aids** (never shipped; the title deletes their files on any launch that
is not a test run): `/app0/dolphin-options.txt` overrides core options for one
run; `/app0/dolphin-debug.txt` names a debug mode: `fog`, `fifo` (records a
FIFO log at frame 300), `dump`, `swloader`, `cmploader`, or `objects A B`
(plays only objects A to B of each FIFO frame, to bisect against desktop
Dolphin).

## Next

The acceptance goal (2026-09-25): Resident Evil 4, Super Smash Bros. Melee and
Wind Waker correct, full speed, with audio, input and states, then stable and
measured through the profiles in `tooling/dolphin-profiles/` (docs/PHASE_LOG.md).

1. Profile 0: RE4 and Wind Waker pass; Melee's remaining sub-100% windows
   (shader-compile hitches) and the grey band in one Melee shot (a FIFO log
   against desktop Dolphin decides whether it is scenery).
2. Profiles 1-11 on all three games: accurate, ubershaders, 6x torture, GPU and
   RAM EFB, the scaling ladder, MSAA, cold/warm shader cache, reloads, state
   stress, long soak.
3. The dual-core FIFO playback stall (single core plays).

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
