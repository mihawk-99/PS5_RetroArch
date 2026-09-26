# Active work

_Updated: 2026-09-26_

## Now: v0.2.0-alpha.1 released; asynchronous shader compilation next

**Released.** Build `870bd1bb` (commits up to `3712ce1`; release notes
`docs/releases/v0.2.0-alpha.1.md`): eight cores, Dolphin, LRPS2 and PPSSPP on the
GPU, SDK fork `a110320`, ../PS5_Vulkan `40e9d30`, LRPS2 at its pinned revision
`6d14775` (6x). It is in `dist/PPSA99169`, staged in `handoff/PPSA99169`, kept in
`build/release/PPSA99169-870bd1bb` and on the console. The tester's three
problems -- half speed with V-Sync on a 60 Hz display, a crash re-creating the
video driver on a 64 KiB RetroArch thread, PPSSPP crashes in vkDestroyBuffer --
are fixed; the release battery (every core, boot, menu and a Quick Menu action,
Threaded Video on half) and a 10-minute PPSSPP soak had no crash
(docs/PHASE_LOG.md, 2026-09-26).

**Open from the release:**

1. **Asynchronous shader compilation** (my standing requirement: 100% speed
   while shaders compile). ../PS5_Vulkan compiles every pipeline, cache hits
   included, under one global mutex (`ps5vk_compile_mutex`), so a core's
   background compiles queue and the render thread waits behind them; Dolphin
   defaults to Synchronous. Parallel compiles in the driver, then asynchronous
   modes as the cores' defaults.
2. Rogue Leader's attract sequence at 72-85% with every shader cached: the
   driver's CPU cost on Dolphin's GPU thread (CPU copies that wait for the GPU,
   a cache flush per draw; profiled 2026-09-25).
3. The 60 Hz fallback has only run on a 120 Hz display; the tester's next trace
   confirms it.
4. LRPS2 after that: the upstream PCSX2 port in ../PS5_LRPS2's working tree
   (San Andreas' modes, texture replacement), 8x as the PS5 default, the
   lighting compared with the native picture.

**Test runs and core options.** RetroArch here uses per-core options
(`global_core_options = false` in my config), so a run's `core_options_path`
is ignored and each core reads `config/<core>/<core>.opt`. A test that needs
its own options sets `global_core_options = "true"` with its own path; my
per-core files are never edited.

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
