# Active work

_Updated: 2026-09-19_

## Now

**Bounded XMB allocation diagnostic is built on `codex/xmb-allocation-diagnostics`.**
Uploads are always authorized; console launches require the owner's intervention.
They authorized the first diagnostic run, which froze and was manually closed.
Do not launch the revised build automatically. No next core is assigned.

- Revised build identity: `a4a751e58339deb55fd8b7b4cdae01c3e8479e79c22be53d717a696413ce34be`.
- Uploaded and verified; not launched. Raw: `klog/xmb-memory-bounded-upload.log`.
- Output: `dist/PPSA99169/`; matching executable/symbols/map/inspection under
  `klog/xmb-memory-a4a751e58339/`. Evidence: `evidence/xmb-memory-bounded-logging/`.
- `PS5_MEMORY_DIAGNOSTICS=1 bash tools/verify.sh` passed all five host gates,
  68 tests; `tools/check-memory-diagnostics.py` passed. Raw build log:
  `klog/xmb-memory-bounded-verify.log`.
- First four failure records are immediate; subsequent details and summaries are
  limited to one per five seconds. Every failure still increments the counters.
  Injected-clock test: 10,006 failures, five records, 10,001 suppressed, <8 KiB.
- `/app0/memory-diagnostics.log` contains periodic requested live bytes/counts/
  peaks, caller groups and image/queue-idle counters. Allocator policy unchanged.
- Four archive hashes match the first diagnostic. PS5_Vulkan was not modified.
  No revised console launch. Procedure/coverage: `docs/MEMORY_DIAGNOSTICS.md`.

## Console findings and limits

- Original crash: `klog/xmb-crash-20260919-181233/`; SIGSEGV `0x45b1ae`
  in Mesa `__vk_log_impl`, after `vk_sync_create` allocation failure during
  XMB texture unload's `vkQueueWaitIdle`. Error logging dereferenced NULL.
- First diagnostic `000adf3edc25` was uploaded/verified and launched with owner
  authorization. Raw: `klog/xmb-memory-run-20260919-183305/`. Owner reports
  complete freeze, then explicitly confirms manual close. No post-launch kernel
  fatal signal. Preserve this outcome, not a passing crash reproduction.
- First failure at 13,187 ms: 15,488-byte XMB node allocation; later 648-byte
  menu callback allocation. Unbounded logging produced 12,793 failure records
  plus summaries / 5,404,264 bytes. It distorted the run and may amplify a stall.
- Observed native requested bytes rose from 4,559,423 (5 s sample) to 11,627,255;
  tracked mappings stayed 5,498,938 bytes. Image creations minus destructions
  stayed 131. Tracking dropped=0, but thousands of foreign frees expose incomplete
  native-heap coverage. These observations do not prove a leak or heap limit.
- Native malloc returned NULL with observed errno=22; errno may be stale. Do not
  label that as a proven invalid request. Underlying failure/freeze is unresolved.
- The bounded logger fixes diagnostic I/O flooding only. Console validation of
  the revision is pending. Do not claim it fixes XMB or the driver's OOM logger.

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

Uploads are authorized; check console idle before deployment. No automatic launch
of the revised diagnostic. When the owner authorizes a test, start klog first,
preserve old logs and capture memory-diagnostics.log during the run. Never
interrupt another title. Preserve crash/freeze logs before relaunch.
