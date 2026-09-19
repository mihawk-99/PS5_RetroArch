# Active work

_Updated: 2026-09-19_

## Now

**FBNeo is ported and owner-confirmed working.** The owner answered
“Works flawlessly!” to colours, sound, controls and Quick Menu/Close Content/
next-game transitions. Logs show both native 32-bit and converted 16-bit games.
Build and console evidence: `evidence/fbneo-native/`. No next core is selected.

## Verified build and evidence

- Frontend identity: `41cb59a98907d3c382fa7c6c2282e64740a2d079364fd7f231dd0ddef7a2ca10`.
- FBNeo source: `6bb3167a044e19e7106a5110d5531aa9c6afa96f`.
- Core SHA-256: `0e4231a238af2e5cc2630568abcaec2d3fe5356d647ec87ab9d9c13e8da66e44`.
  85,234,664 bytes; 25 callbacks; 7 initializers and 1 finalizer.
  Source, official metadata, port-input and SDK hashes are in build.json evidence.
- All five host gates pass, 63 tests: `/tmp/fbneo-catalogue-verify.log`.
  Symbol-bearing ELF/map: `klog/fbneo-41cb59a9-symbols.elf` and corresponding .map.
- `tools/run-title.sh --no-build --core-test=fbneo --watch 240` verified deployment,
  eight load/export/API/name/unload cycles and failed-load/menu recovery.
  Both diagnostic reports pass. Content was selected manually by the owner.
- Captured native 32-bit 304x224 and 16-bit 384x224 games, both reporting XRGB8888
  callbacks, with returns to the 320x240 dummy menu and subsequent game loads.
  Core-declared FPS/audio rates are metadata, not performance measurements.
- Final current-build trace: zero Vulkan refusals or GPU API failure records;
  kernel: zero fatal signals. Audio: 11 close reports, all errors=0.
- Title exited before the 240s window ended: rarch_main returned 0 and native
  quit status 0. This is not a claim of 240 seconds of continuous gameplay.
- Raw: `klog/fbneo-catalogue-run.log`, `klog/run-PPSA99169-165611.log`,
  `klog/fbneo-catalogue-live-retroarch.log`, `klog/fbneo-catalogue-live-trace.txt`.
- FCEUmm, mGBA and Snes9x bytes match their accepted builds. Their evidence is
  in `evidence/native-core-loading/`, `evidence/mgba-native/`, `evidence/snes9x-native/`.
- PS5_Vulkan was not modified. Four archive hashes matched before/after builds;
  exact linked hashes are in the evidence. The owner develops it concurrently.

## What changed

- `make fbneo`: pinned full upstream build with explicit project SDK wrappers,
  native import union and official .info staged in info/ and cores/. The supplied
  metadata was a reference only. No ROM, BIOS or samples are shipped.
- 32-bit XRGB output remains direct. 16-bit-only drivers retain RGB565 internally
  and convert to a separate XRGB8888 buffer at the callback. The existing frontend
  RGBA upload/cropping fixes remain; PS5_Vulkan and frontend video code are unchanged.
- SDK compatibility removes obsolete GCC -fforce-addr, selects C++11 and disables
  exceptions/RTTI. A local bounded-read setjmp/longjmp escape replaces the MPEG
  decoder's exception. Original/patched layer 2 and AMM outputs match in host tests.
- Existing core-local C++ destructor ownership handles cleanup before unmapping.
- Writable catalogue names use two bulk allocations instead of ~86,000 small
  allocations. The existing large-buffer allocator handles the name store.
  Allocation/length validation precedes mutation; cleanup restores original
  pointers; initialization failure rejects content. All 28,910 names fit.

## Resolved failures and remaining limits

- First build passed host gates but failed console metadata queries: our static
  version-string patch retained upstream free(). Removed it; a 10,000-query
  ASan/UBSan regression now checks the actual patched function and archive flags.
- Second build passed loader/recovery but failed game initialization: catalogue
  allocations exhausted libc heap and BurnLibInit wrote through NULL. Bulk storage
  fixes this; 30,000-driver stress, allocation failures and reload tests pass.
  Both failed runs and exact identities are recorded in `evidence/fbneo-native/iterations.json`.
- Five ERROR lines in the accepted frontend log belong to deliberate missing-core
  recovery tests, whose reports pass. No unexpected error lines remain.
- Optional environment command 87 (SET_SERIALIZATION_QUIRKS) is unsupported by
  this frontend. FBNeo's endian-dependent state hint is not consumed; ordinary
  gameplay works. Cross-platform states/netplay are not accepted by this run.
- Not every arcade board/ROM set, CHD, samples, rotation, SRAM/state round trip,
  long-run A/V sync or performance is covered. Audio silence and queued-tail
  discard counters span menu/start/stop transitions; zero backend errors is not
  proof of uninterrupted audio across all workloads.
- Loader still rejects TLS, legacy init/fini, nonempty preinit and unsupported
  relocations/dependencies. General unwind registration is absent. Lifecycle
  callbacks must not reenter the loader; cores share the frontend process.

## Preserved platform and file locations

XMB stays default and RGUI selectable. Vulkan remains the presentation route;
video_ps5 stays selectable as CPU fallback. Native audio/input/binding, config
saving, FTP permissions and directory browsing remain. Useful logs are retained.
Deployments preserve live settings/games. FBNeo receives whole ZIP/7z archives.

FTP base `/data/homebrew/PPSA99169/` corresponds to in-title `/app0`.
Use content/, system/fbneo/, cores/, config/retroarch.cfg, savefiles/, savestates/.
See `docs/DEPLOYMENT.md` for build/upload procedures and BIOS placement.

## Operating notes

Uploads/runs are authorized without further permission. Never interrupt an
existing title; the runner checks idle before deployment and launch. Core/game
selection is manual. Keep private filenames and console details in ignored logs.
Separate appended trace launches by bss check and build identity. Preserve live
logs before relaunch. Owner supplies visual confirmation; camera cadence can
hide flicker. Do not infer manual closure solely from the final process count.
