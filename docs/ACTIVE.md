# Active work

_Updated: 2026-09-19_

## Now

**XMB allocation diagnostic is built on `codex/xmb-allocation-diagnostics`.**
This is instrumentation, not a crash fix. Owner explicitly requests no automatic
upload or launch while working on another project. Wait for their intervention.
The baseline's next-core work is not the assignment; this crash investigation is.

- Build identity: `000adf3edc2538bc563f833f0ccd2e91a01053aa6dac19dd568a580cdbbbb3cb`.
- Output: `dist/PPSA99169/`; preserved executable/symbols/map/inspection under
  `klog/xmb-memory-000adf3edc25/`. Evidence: `evidence/xmb-memory-diagnostic/`.
- `PS5_MEMORY_DIAGNOSTICS=1 bash tools/verify.sh` passed all five host gates.
  Final format/unit check passed; 68 tests. Raw: `klog/xmb-memory-verify.log`,
  `klog/xmb-memory-final-checks.log`. `tools/check-memory-diagnostics.py` passes.
- No upload, launch, console settings change or PS5_Vulkan modification.
  Current libps5vk differs from the crashing build; local archive snapshots and
  their hashes identify the diagnostic's inputs. Other three archives match.
- Log: `/app0/memory-diagnostics.log`; periodic requested live bytes/counts/peaks,
  caller groups, image/queue-idle counters; immediate allocation failure records.
  Metadata saturation/coverage gaps are explicit. No allocator policy change.
- Reproduction and interpretation: `docs/MEMORY_DIAGNOSTICS.md`. Console validation
  remains pending, including whether the crash reproduces with the newer driver.

## Crash evidence

Owner reproduced rapid XMB navigation crash. Raw capture:
`klog/xmb-crash-20260919-181233/` (kernel, frontend, trace, config, diagnosis).
Matching baseline symbols locate SIGSEGV at runtime `0x45b1ae` in
`__vk_log_impl`, reached through `vk_sync_create` allocation failure inside
`vkQueueWaitIdle`, called from XMB texture unload. Logger's second allocation
returns NULL and is dereferenced. Why allocation failed is unresolved: heap
exhaustion, fragmentation, retention/leak, unobserved usage or corruption remain
possible. Do not call this a GPU hang or a resolved frontend/driver leak.

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

Console testing is paused by the owner's explicit instruction for this step.
When they are ready, start passive klog before reproduction and preserve current
logs. Never interrupt another title. After a crash, capture logs before relaunch.
No new core or unrelated driver work is authorized by the active-file context.
