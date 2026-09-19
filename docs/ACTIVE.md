# Active work

_Updated: 2026-09-19_

## Now

**The first-failure XMB diagnostic is built and host-verified.** Branch:
`codex/xmb-allocation-diagnostics`. It is instrumentation, not a crash fix.
Uploaded after confirming idle; readback passed and previous logs preserved.
A launch requires owner intervention. No new launch.

- Ready identity: `e60deca9412fcedaa59162cc5254fc8e0b13df5c2b3fb4a8187e0b059b0acf00`.
- Evidence: `evidence/xmb-first-failure-diagnostic/`; symbols, map, manifest,
  executable, host capture and inspection: `klog/xmb-first-failure/`.
- First failure emits live caller owners, ranked separately for native/mapped/
  aligned routes (up to 16 each), cached tab/list/progress/node counters and the
  previous eight transition boundaries with native usage. No per-entry log I/O.
- `PS5_MEMORY_DIAGNOSTICS=1 bash tools/verify.sh`: all five gates pass, 69 tests,
  278/278 frontend sources. Log: `klog/xmb-first-failure-verify-final.log`.
  Inspection proves the actual XMB object references all three hooks.
- Injected 10,006 failures: five ordinary failure records, one expanded snapshot,
  eight history rows, 48 owners, 9,404 bytes. Normal-build hooks are inert.
- Linked libps5vk hash is now `8c2a1c46a38ea935edf496c807c100e26b8a32d5aec725179983f46b3f626906`;
  the prior run used `900d496a9eb725857e4a70540d34ef262dcb1d5c85b7ed9325a28d16b460e246`.
  Other three archive hashes match. Stable local copies linked; driver unmodified.
- Initial gate attempt hit the deliberate patch-count check (139 -> 150 for 11
  new anchors). Updated count, reran successfully; no unexplained gate failure.
- Procedure, phase/counter meanings and stale-context limits:
  `docs/MEMORY_DIAGNOSTICS.md`. Next run: capture klog before launching; owner
  holds left/right between XMB tabs. Preserve logs before any relaunch.

## Prior failure and local attribution limits

- Bounded diagnostic `a4a751e58339` reproduced SIGSEGV / NULL write in Mesa
  `__vk_log_impl`, through white-texture unload and queue-idle allocation error.
  Raw: `klog/xmb-memory-run-20260919-184732/`; `evidence/xmb-memory-crash/`.
- First failed request: 15,488 bytes in xmb_list_insert at 6,429 ms. Native live
  requests rise 4,625,679 -> 11,616,951; tracked mappings remain 5,498,938 and net
  frontend image count 131. The 6,991,272-byte increase remains unattributed.
- `docs/XMB_ALLOCATION_INVESTIGATION.md`: 10,000 synthetic tab replacements show
  no ordinary-node accumulation. Each node holds 15,360 bytes of path arrays;
  missing wallpaper updates can recreate the white texture repeatedly.
- First unbounded diagnostic froze; owner manually closed it. Its failure-log
  flood was corrected. This does not prove a heap limit, fragmentation or leak.
- Do not switch away from XMB/Vulkan. Console runtime verification remains pending.

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
