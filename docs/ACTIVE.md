# Active work

_Updated: 2026-09-19_

## PPSSPP Track A: the core builds

Plan: [PPSSPP_Implementation_Plan.md](../PPSSPP_Implementation_Plan.md); platform
analysis: [PPSSPP_Core_Plan.md](../PPSSPP_Core_Plan.md). Track A is PPSSPP on its
software GPU core, with no PS5_Vulkan dependency.

**Done — A1, A2: platform branch and cross build.** `tools/build-ppsspp.sh` fetches
pinned `f293b10` (29 submodules), applies two zero-fuzz patches, configures the tree
with `tooling/ppsspp/ps5-toolchain.cmake` and ABI-checks the result: 18,485,448 bytes,
25 exports, **zero `PT_TLS`**, three 16 KiB segments (no W+X), `NEEDED` exactly the
three allowed modules. Reports: `build/cores/ppsspp/{abi.json,build.json}`.
`thread_local` needed no patch — the SDK uses `-femulated-tls`; two flag shims
(`static_assert`, `ZSTD_TRACE`) and a four-call libc shim carry the vendored
third-party code. Detail: `docs/PHASE_LOG.md`.

**Done — A3: the title link.** `ppsspp` joined `core_names` and the identity inputs;
all 479 of the core's imports resolve (`localtime_r` through the existing
`rtime_localtime` alias, `__emutls_get_address` from the clang builtins the title
already links), the frontend and title link, and `core imports` grows to **497
bindings for 6 cores**. Title identity `ed3f78147e015696…`, `eboot.bin` 34,655,444
bytes, six cores staged in `dist/PPSA99169/`.

**Done — reproducibility, and the five cores are undisturbed.** The varying input was
libpng's `__DATE__`/`__TIME__` banner string; `SOURCE_DATE_EPOCH` is now derived from
the pinned commit and two consecutive builds are byte-identical
(`8346e010a8781f8c…`). Rebuilding fceumm and fbneo at the pre-change commit
`b1dced9` gives the same two hashes as this tree, and mgba, snes9x and Genesis Plus
GX are byte-identical to their committed evidence, so nothing the PPSSPP work did
changes a shipped core.

**Next — A4: the first console load.** `--core-test=ppsspp` now arms the loader
diagnostic (eight load/unload cycles of the 18.5 MB core, no `retro_init`, so no
Vulkan device is created), which is the console step that needs the owner's launch.
Then A5: `system/PPSSPP/` assets plus a PSP homebrew, both owner-supplied.

## Now

**Owner accepted the thumbnail and configuration-reset fixes: “Perfect. Commit”.**
Landing on `main`; evidence: `evidence/thumbnail-input-defaults/`.
Details and regression checks: `docs/THUMBNAIL_INPUT_DEFAULTS.md`.

- Async image loading discarded the requested RGBA flag; patch 0081 preserves it.
  Gameplay pixels and saved PNG encoding are unchanged.
- Compiled input was ps5 but joypad default was null; patch 0082 selects ps5.
  Reset also clears auto-binds without recreating the driver, so the next pad poll
  now reannounces the connected controller once to restore the built-in profile.
- Three regression tests failed before the fixes; all four focused tests pass
  afterward, including native-pad recovery without new samples or a device reopen.
- All five gates pass: 74 tests, 278 frontend sources; normal diagnostics mode.
  Identity: `e11018a79a62ce91fa723deaad6939c1e48a47bace21b3527ae84f4452148451`.
  Exact artifacts/gate logs: `klog/thumbnail-input-build/`; sanitized host report
  in evidence/thumbnail-input-defaults/. Driver archives and Mesa utility objects
  match the accepted normal XMB build. Logging and PS5_Vulkan unchanged.
- Uploaded eboot only after idle checks, runtime identity read back, live config
  unchanged. Capture: `klog/thumbnail-input-upload-20260919-205531/`. Saved joypad
  selection was empty (automatic), not null; no configuration repair was needed.
  No launch/kill sent. Owner accepted the fixes after deployment. No fresh console
  logs or individual checklist results were captured; acceptance is owner observation.

## Accepted XMB baseline

`601e575` fixed large-list allocation failures; `6b857a9` merged the remote README.
Owner confirmed crash-free navigation, then normal build removed five-second hitches.
Normal identity: `2d0743abbcf289efd0a549929d25ce7512968fc93cba8163467fdd3641c8fcf6`.
Both build modes passed all five gates and 71 tests; current logging was retained.
Evidence: `evidence/xmb-safe-list-run/`; detail: `docs/XMB_LIST_SAFETY.md`.
Diagnostic capture had zero allocation failures/drops/image or idle failures,
zero frontend ERROR/GPU refusals and clean quit. Native requested peak: 6,518,064
bytes. Kernel capture was post-run backlog, not a full launch-to-exit recording.
Normal acceptance was owner observation, without a fresh normal-run log capture.
The unsafe driver error logger under arbitrary OOM remains outside the frontend fix.

## Failure being addressed

Resolved and superseded. The XMB large-list crash (`601e575`), the hitches after it
and the thumbnail/input defaults (`e11018a`) are all accepted; the investigation
record is `docs/XMB_ALLOCATION_INVESTIGATION.md`, the fix notes
`docs/XMB_LIST_SAFETY.md` and `docs/THUMBNAIL_INPUT_DEFAULTS.md`, and the raw
captures are `klog/xmb-*` with evidence under `evidence/xmb-*` and
`evidence/thumbnail-input-defaults/`. The unsafe driver error logger under
arbitrary OOM remains outside the frontend fix.

## Previous console-verified baseline

Genesis Plus GX frontend: `ecfcddd57febbb484c2e0724e95f43bbffb3ddd9b9c22aa9c1cf3e37af4a9445`.
All five gates passed, 66 tests; owner confirmed gameplay colours, sound, controls
and clean menu/next-game transitions. Evidence: `evidence/genesis-plus-gx-native/`.
FCEUmm, mGBA, Snes9x and FBNeo evidence remains in `native-core-loading/`,
`mgba-native/`, `snes9x-native/` and `fbneo-native/` under evidence/.
Earlier core source pins, hashes, raw captures and acceptance limits are preserved
there and in docs/PHASE_LOG.md. No new core is assigned by this follow-up.

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

Uploads are authorized; check console idle before deployment. A new launch needs
owner intervention. Start klog first, preserve old logs and capture allocation,
frontend and trace logs. Never interrupt another title. Preserve crash/freeze logs
before relaunch. No new core or unrelated driver work is assigned by this file.
