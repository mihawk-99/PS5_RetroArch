# Active work

_Updated: 2026-09-19_

## Now

**Configuration saving and directory browsing work on the PS5.** The owner
confirmed saving configuration, then on the final build: "Folders are visible,
and when I select Load configuration file I could see the .cfg file".
The title stayed alive for 90 seconds and was closed by the script. XMB/Vulkan
and native audio initialization remain working, with zero GPU refusals/errors.

The new prompt assigned configuration/content browsing despite the previous
active file's audio-only scope; this step follows that assignment. Actual core
execution is not claimed or newly assigned by this filesystem fix.

## Verified build and evidence

- Identity: `26139a294d687ccf21c8fa779e5ad3883273b873b71ce67e1df48f48589f8f76`.
- Final command: `tools/run-title.sh --no-build --watch 90`.
  `/app0` enumerated 96 entries and `/app0/cores` two (empty directory), errno 0.
  XMB assets/fonts ready, native audio initialized, zero Vulkan refusals/API
  failures or frontend ERROR lines. Full 90 seconds alive; script-closed.
- Live configuration was saved successfully and the same 108,188-byte file
  survived the next upload/restart byte-for-byte. Its selected drivers and core,
  browser and asset paths match the port. Raw configs remain ignored.
- All five `tools/verify.sh` gates PASS; 35 unit tests; 15 evidence records.
  Fresh patch replay: 104 applied + one upstream marker; second pass: 105 present,
  bytes unchanged. Old 4 KiB adapter fails the mounted-directory regression;
  current 64 KiB adapter passes.
- Evidence: `evidence/native-paths/`. Raw captures:
  `klog/paths-{first,native,large}-run.log`, final `klog/retroarch-130100.log`,
  `klog/paths-config-readback.json` and before/after config readbacks.
- PS5_Vulkan remains unchanged at **6f0ce0d**. No sibling project was modified.

## What changed

`frontend_ctx_ps5` preserves startup argv and sets native directory defaults and
browser roots. Live config is `/app0/config/retroarch.cfg`; a packaged seed is
copied only if it does not exist. The config-save crash came from Unix executable
path discovery during path abbreviation; the port now supplies `/app0/eboot.bin`.

The libc directory wrapper returned EPERM. The SDK `open/getdents/close` adapter
works, using 64 KiB directory reads: 4 KiB worked on `/` but failed on the mounted
application filesystem. Unavailable external roots are omitted. No permission
bypass is attempted. The runner snapshots the selected build identity so a later
local build cannot falsely invalidate an earlier run's log.

## User file locations

FTP base is `/data/homebrew/PPSA99169/`; `/app0` is its in-title mount name.
Use `cores/` for PS5-compatible cores, `content/` for ROMs/content, `system/` for
BIOS files, and `config/retroarch.cfg` for live settings. Save directories are
`savefiles/` and `savestates/`. Ordinary updates preserve these user files.
See `docs/DEPLOYMENT.md`; no cores or ROMs were installed by this step.

## Preserved functionality and limits

Native `audio_ps5` is default; audible left/right tones, bounded buffering and
port lifecycle were verified in `evidence/native-audio/`. Real-core audio,
long-run A/V sync and content-based underruns remain unverified.

XMB stays default on the statically linked Vulkan path. Single-level menu textures
remain the tested compatibility path; general mip chains are unverified.
`video_ps5` and RGUI stay registered/selectable. Screenshots remain opt-in via the
parked recipe; GPU profiling stays opt-in. Useful startup/frontend/core/audio/error
logs remain enabled without routine frame-success chatter.

The first filesystem run crashed during config save; the second fixed saving
but could not enumerate `/app0`. Both failures are documented, not acceptance.
The final run resolves them. USB access and real core loading require separate
verification on the available mounts and compatible core artifacts.

## Operating notes

The user authorized console uploads/runs without further permission and necessary
driver fixes with documentation plus a PS5_Vulkan commit. Never interrupt an
existing title. The runner checks idle state before upload and launch. For appended
trace files, select the last `bss check=` substring and verify its build identity.
Never substitute the CPU route to conceal a GPU refusal.
