# Active work

_Updated: 2026-09-20_

## Now: the title is relinked against the migrated driver, on its own byte order

**Driver (`../PS5_Vulkan` `8b311e3`) is at rung 1.0 with its SDK fork migrated.** The
format audit has no gaps (179 required, 58 reported, 0 missing, 55 conditional, split
0/0/0/0/0) and `driver/ps5vk_image.c` was not touched by the migration, so
`VK_FORMAT_B8G8R8A8_UNORM` still samples through the `8_8_8_8_UNORM` word with the
`ZYXW` selectors — the byte order libretro's little-endian XRGB8888 already is. What the
migration changed is the shader compiler (ps5-opengl 0.3.0's `opengnm-psbc` tree,
metadata v14), the compute dispatch words, a null shader module in `ps5vk_compile_stage`
that faulted every meta clear/blit/resolve, and a driver-owned 32 MiB compile stack. The
"ACO SIGFPE" is closed and was never ACO: rebased, it was the probe's own unfilled
`std::array` row dividing by a zero texel size. Archived in
`evidence/driver-1.0-native-byte-order/driver-archives.json`.

**Withdrawn here: the red/blue compensation.** 0014's format substitution, 0075's
core-frame RGBA upload and 0077's menu colour and cached-frame ownership existed only
because the driver could not sample the frontend's own byte order. They are now in
`tools/apply-port-patches.py`'s `WITHDRAWN` tuple, which exchanges each insertion for the
upstream text it replaced, so an existing build tree and a fresh one converge.
`src/core_frame_ps5.cpp` no longer converts channels. Kept, each for a reason that does
not depend on the sample order: **0027/0055** (the 16-bit RGUI frame is converted by hand
into R8G8B8A8, because a B4G4R4A4 view with a B/R swizzle is still refused), **0015**
(upstream's own `VK_REMAP_TO_TEXFMT` needs the bpp case) and **0081** (upstream's TODO:
`supports_rgba` follows the order a caller asked for).

**Verified here.** `tools/verify.sh` with the driver frozen into `build/frozen-driver/`
passes all five gates (format, unit 75 tests, build, integration, evidence); title
identity `f9e4ea6c…`, eboot 34,703,515 bytes sha256 `82534b86…`, two consecutive runs
byte-identical. The frozen archives were re-hashed unchanged after the build and the
printed identity was recomputed from its own input list and matched, so the eboot names
the driver it carries. Record: `evidence/driver-1.0-native-byte-order/`.

**Pending, and the only thing that decides the colours: a console run.** The withdrawn
path is the one the owner accepted earlier; the native byte order has host evidence and
the driver's own console `v0-formats` battery. Deploy, then have the owner launch:
`JOBS=14 bash tools/run-title.sh --no-build --deploy --watch 120`, and check menu
colours, core colours, and the Quick Menu over a paused game. Until that is recorded,
this step is host-verified only.

## PPSSPP Track A: parked at the owner's request

Plan: [PPSSPP_Implementation_Plan.md](../PPSSPP_Implementation_Plan.md); platform
analysis: [PPSSPP_Core_Plan.md](../PPSSPP_Core_Plan.md). A1–A4 are done: the core is
built from pinned `f293b10` (29 submodules, zero `PT_TLS`, the three allowed `NEEDED`
modules), ABI-checked, linked into the title, and loaded on the console with the runtime
assets staged (`cycles=8, exports=25, api=1`; `evidence/ppsspp-native/`).

**Blocker — the first frame.** A SIGSEGV on a worker thread during `ThreadManager::Init`
(fault address 0x70, frames inside `libkernel.sprx`, `pthread_get_specificarray_np+0x3b`
reached from `pthread_create_name_np`). Every isolation probe passes. The console's
libkernel can now be symbolized (NID = `SHA1(name || salt)`, byte-reversed, base-64),
which is what named the frame. Next attempt: remove PPSSPP's `thread_local` uses, since
core TLS compiles to `__emutls_get_address` → `pthread_getspecific`, exactly the family
in the backtrace.

## Accepted baselines

- XMB: `601e575` fixed large-list allocation failures, `6b857a9` merged the README;
  crash-free navigation and no five-second hitches. Detail: `docs/XMB_LIST_SAFETY.md`,
  `evidence/xmb-safe-list-run/`.
- Gameplay: Genesis Plus GX frontend `ecfcddd5…`, all five gates green, owner acceptance
  of colours, sound, controls and menu/next-game transitions. FCEUmm, mGBA, Snes9x and
  FBNeo evidence stays in `native-core-loading/`, `mgba-native/`, `snes9x-native/` and
  `fbneo-native/`. Core pins, hashes and acceptance limits are in `docs/PHASE_LOG.md`.
- Thumbnail and configuration-reset fixes accepted (patches 0081/0082):
  `evidence/thumbnail-input-defaults/`, `docs/THUMBNAIL_INPUT_DEFAULTS.md`.

## Named errors and remaining limits

- First frontend snapshot has five intentional missing-core recovery errors and two
  archive-extraction failures; the owner identified the content as Sega System 16 & 32,
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

Uploads are authorized; check console idle before deployment. A new launch needs owner
intervention. Start klog first, preserve old logs, and capture allocation, frontend and
trace logs. Never interrupt another title. No new core or unrelated driver work is
assigned by this file.
