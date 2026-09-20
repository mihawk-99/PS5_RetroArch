# Active work

_Updated: 2026-09-19_

## Now

**Owner confirms XMB crashes and periodic stutter are fixed.**
Landing on `main` with current logging unchanged. Details: `docs/XMB_LIST_SAFETY.md`.
Acceptance and build evidence: `evidence/xmb-safe-list-run/`.

- Tested diagnostic: `88d30106ce625e783e899c97ddeab43d4ca16949ddd98cfb129a06190e0c4ec6`.
  Owner ran it manually and reports brief hitches roughly every five seconds.
  Read-only capture: `klog/xmb-stutter-20260919-200720/`; sanitized evidence:
  `evidence/xmb-safe-list-run/`. No launch or kill was sent by the agent.
- The matching 55.422-second session has zero allocation failures/dropped records,
  zero failed image creations or queue-idle calls, and clean frontend/native quit.
  First sample includes 7,385 compact XMB nodes and 7,386 callbacks. Native peak
  is 6,518,064 requested bytes; tracked mapped allocations return to zero at exit.
  Frontend ERROR lines and trace GPU API failure/refusal records are zero.
- Post-run kernel collection includes backlog; it is not a full launch-to-exit
  capture. Repeated native dlopen failures at Vulkan init are the existing
  libvulkan.so.1/libvulkan.so probes, followed by the statically linked entry point.
- Diagnostic tick runs before presentation: every five seconds it holds the
  observer mutex, scans 131,072 slots and synchronously writes summary/caller rows.
  This matches the reported hitch cadence; exact stall duration was not measured.
- Normal identity: `2d0743abbcf289efd0a549929d25ce7512968fc93cba8163467fdd3641c8fcf6`.
  PS5_MEMORY_DIAGNOSTICS=0; all five gates pass, 71 tests, 278/278 sources. ELF
  inspection confirms observer functions/hooks absent and safe allocator present.
  `klog/xmb-normal/` preserves exact artifacts, gate log and inspection.
  Uploaded/readback verified; config preserved; no launch. Capture:
  `klog/xmb-normal-upload-20260919-201708/`.
- All four saved driver archives match the tested diagnostic. Mesa utility object
  bytes differ only in debug compilation-directory information; stripping debug
  info makes all three byte-identical. PS5_Vulkan source remains unchanged.
- Accepted changes: nodes 15,488 -> 96 bytes, visible-only optional icon paths,
  reclaimable mapped node/callback slabs, checked append/prepend/copy operations,
  first-failure population stop and retry reset on clear. Shared patched headers
  participate in the full frontend build fingerprint. No driver sources changed.
- Diagnostic passed all five gates, 71 tests, 278/278 frontend sources. Exact ELF,
  map, title, manifest, archive hashes and log: `klog/xmb-safe-lists/`.
  Linked libps5vk is 11ff0f410c80 (older crash used 8c2a1c46a38e).
- Owner confirmed the normal build fixed the hitches and requested the commit.
  No new normal-run logs were captured; this acceptance is the owner observation.
  RetroArch.log, trace and passive klog remain; the observer stays opt-in.
  Broader RGUI/XMB and game/menu visual checks remain useful follow-ups.
  The separate unsafe driver error logger under genuine OOM is not fixed here.

## Failure being addressed

- Prior identity: `e60deca9412fcedaa59162cc5254fc8e0b13df5c2b3fb4a8187e0b059b0acf00`.
  Raw: `klog/xmb-first-failure-run-20260919-192044/`; evidence:
  `evidence/xmb-playlist-allocation-crash/`.
- First failure: native xmb_list_insert requests 15,488 bytes at 6,509 ms,
  custom tab 7/index 339/list size 340. Old nodes fall 4 -> 3; then 339 new nodes
  appear in 23 ms. Native requests rise 4,497,239 -> 11,616,951 bytes. The custom
  playlist contains 7,384 entries. Rapid input is not needed to trigger this.
- XMB inserts own 5,265,920 native bytes; playlist array 1,179,664; list arrays
  473,152; callbacks 220,968. The later SIGSEGV is the Vulkan error logger's NULL
  write during texture unload/queue-idle allocation failure. Previous collector
  stopped locally; title was already gone. No second launch or title kill.
- Main history review e080d81..a18cec6 found unchanged XMB node/list logic and
  upstream pin. Config/filesystem support may have exposed a larger workload;
  the early 45-second clean XMB run did not verify this same playlist. No exact
  regression commit is proven. See docs/XMB_ALLOCATION_INVESTIGATION.md for the
  earlier lifecycle probe and docs/MEMORY_DIAGNOSTICS.md for observer limits.

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
