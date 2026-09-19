# Active work

_Updated: 2026-09-19_

## Now

**The first-failure run identifies a large-playlist allocation burst.** Branch:
`codex/xmb-allocation-diagnostics`. No crash fix is implemented yet.
Owner authorized launch, then reports the crash happened while scrolling naturally
and that older builds seemed stable. Rapid input is not required for this failure.

- Runtime identity: `e60deca9412fcedaa59162cc5254fc8e0b13df5c2b3fb4a8187e0b059b0acf00`.
- Raw: `klog/xmb-first-failure-run-20260919-192044/`. Evidence:
  `evidence/xmb-playlist-allocation-crash/`; exact symbols in `klog/xmb-first-failure/`.
- First failure: xmb_list_insert requests 15,488 bytes at 6,509 ms. Custom tab 7,
  insertion index 339, list size 340. Previous-tab cleanup reduces node count
  4 -> 3; new tab creates 339 nodes in 23 ms. No accumulation of old tabs here.
- Native live requests rise 4,497,239 -> 11,616,951 after old-node cleanup.
  XMB insert owns 5,265,920 bytes / 340 allocations; JSON playlist array owns
  1,179,664; file-list arrays 473,152; entry callbacks 220,968.
- Read-only console inspection finds one custom playlist with 7,384 entries.
  Complete nodes alone would request 114,363,392 bytes, plus 4,784,832 callback
  bytes. Native allocation failures precede the driver logger's NULL write.
- One post-launch SIGSEGV in __vk_log_impl via texture unload/queue-idle error
  reporting. Console reports no running title. Crash snapshots are saved and
  the local passive collector is stopped. No second launch or title kill.
- XMB's non-diagnostic source edits match its initial e080d81 commit. This does
  not rule out regressions elsewhere or establish an exact native heap limit.
  Asked whether older builds opened the same full playlist; answer pending.
- Next design should make large-list allocations safe on the native platform;
  merely reducing scroll speed or guarding the logger does not finish that job.
  Do not switch away from XMB/Vulkan or reduce the owner's playlist as a fix.

## Diagnostic readiness and limits

- Diagnostic d7ad1b3 passed all five host gates, 69 tests, 278/278 frontend sources;
  `evidence/xmb-first-failure-diagnostic/`. Log: `klog/xmb-first-failure-verify-final.log`.
- First failure captures per-route top owners, cached numeric XMB context/node
  counts, and eight transition boundaries without per-entry I/O. Injected 10,006
  failures produced one expanded snapshot / 9,404 bytes. Normal hooks are inert.
- Linked libps5vk is 8c2a1c46a38e, versus 900d496a9eb7 in the earlier bounded run;
  three other archives match. Full hashes preserved. PS5_Vulkan was not modified.
- Earlier a4a751e58339 run already reproduced the same NULL logger crash;
  evidence/xmb-memory-crash/. The first diagnostic's unbounded log flood was
  fixed before both later captures. This step did not build or deploy new code.
- Procedures and context limitations: `docs/MEMORY_DIAGNOSTICS.md`.
  Local lifecycle experiment: `docs/XMB_ALLOCATION_INVESTIGATION.md`.

## Previous console-verified baseline (Genesis Plus GX)

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
