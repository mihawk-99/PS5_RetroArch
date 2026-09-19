# Active work

_Updated: 2026-09-19_

## Now

**The bounded allocation diagnostic captured the original XMB crash.** Branch:
`codex/xmb-allocation-diagnostics`. This is a diagnostic result, not a crash fix.
Uploads are authorized; launch only with owner intervention. Owner authorized this
run and reports crash reproduced. Final logs are preserved; capture is stopped.

- Running identity: `a4a751e58339deb55fd8b7b4cdae01c3e8479e79c22be53d717a696413ce34be`.
- Raw: `klog/xmb-memory-run-20260919-184732/`; evidence:
  `evidence/xmb-memory-crash/`. Exact symbols: `klog/xmb-memory-a4a751e58339/`.
- Kernel records one post-launch SIGSEGV: NULL write in Mesa `__vk_log_impl`.
  Stack matches the original crash: XMB wallpaper/context update, texture unload,
  `vkQueueWaitIdle`, allocation-error reporting, NULL dereference.
- Allocation log is 3,217 bytes. First failure: 6,429 ms, 15,488-byte request in
  `xmb_list_insert` (inlined `xmb_alloc_node`). Native requested bytes rise from
  4,625,679 at 5 s to 11,616,951 at first failure; tracked mappings stay 5,498,938.
  Image creations minus destructions stay 131. dropped=0; native coverage remains
  incomplete because system-internal allocations are not intercepted.
- Only first four failures are logged immediately, so subsequent failures before
  this crash may be suppressed. Their absence is not proof of successful calls.
- This reproduces the original failure without the first diagnostic's log flood.
  It does not establish heap limit, fragmentation, leak or corruption as cause.
- Owner clarifies reproduction: holding left/right between XMB tabs.
- Local investigation: `docs/XMB_ALLOCATION_INVESTIGATION.md`, evidence in
  `evidence/xmb-allocation-investigation/`. 10,000 synthetic list replacements
  show no ordinary-node accumulation; 15,360 of each 15,488-byte node are path
  arrays. Missing wallpaper updates can recreate the white texture every time.
  The 6,991,272-byte console rise is not yet attributed; next diagnostic needs
  first-failure caller totals and bounded tab/list/node counts. No new launch,
  application build or driver edit. Do not switch away from XMB/Vulkan.
- First diagnostic `000adf3edc25` froze; owner manually closed it. Raw:
  `klog/xmb-memory-run-20260919-183305/`. It emitted 12,793 failure records plus
  summaries / 5,404,264 bytes. Bounded logging corrected that diagnostic defect.

## Diagnostic readiness and limits

- `PS5_MEMORY_DIAGNOSTICS=1 bash tools/verify.sh` passed all five host gates,
  68 tests; `tools/check-memory-diagnostics.py` passed. Raw build log:
  `klog/xmb-memory-bounded-verify.log`. Output remains `dist/PPSA99169/`.
- First four failures are immediate; thereafter one detail/summary per five
  seconds, every failure counted. Injected-clock test: 10,006 failures, five
  records, 10,001 suppressed, <8 KiB. No allocator policy changes.
- Four linked archive hashes match the first diagnostic (which already used a
  newer libps5vk than the original baseline). PS5_Vulkan remains unmodified.
- Procedure/coverage: `docs/MEMORY_DIAGNOSTICS.md`. The diagnostic itself now ran
  and captured the crash; this does not accept application stability.

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
