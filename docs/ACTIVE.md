# Active work

_Updated: 2026-09-19_

## Now

**FTP uploads work in all seven RetroArch-managed directories.** The user's
permission request takes precedence over the earlier filesystem follow-up scope.
Startup creates and repairs `config`, `cores`, `content`, `system`, `savefiles`,
`savestates` and `playlists` to `0777`, including existing folders and restrictive
umasks. This changes directory modes only; user file modes are not recursive.

`0775` applied successfully but real FTP uploads still returned `550 Permission
denied` in every managed folder. The user-authorized `0777` fallback passed
upload, byte-for-byte readback and cleanup in all seven. No probe files remain.

## Verified build and evidence

- Identity: `347655429cf4a60f3d7608c3ba4e46ec1c1972f81704cbea9f9e654c9942cfed`.
- Commands: `tools/run-title.sh --no-build --watch 30`, then
  `python3 tools/check-ftp-write.py`. Full 30 seconds alive; script-closed.
  No mkdir/chmod errors, Vulkan refusals or GPU API failures. Presentation,
  native audio startup and XMB assets/fonts ready are recorded.
- Evidence: `evidence/ftp-directory-permissions/` retains both the failed `0775`
  FTP test and successful `0777` test. Raw captures stay in ignored `klog/`:
  `permissions-{run,open-run}.log`, `retroarch-131931.log`, `ftp-write-*.json`.
- Host fixture covers old `0755`/`0775` directories, `umask(0077)`, and config
  preservation. Former mkdir-only code fails the permission regression.
- All five `tools/verify.sh` gates PASS; 35 unit tests; 16 evidence records.
- PS5_Vulkan remains unchanged at **6f0ce0d**. No sibling project was modified.

Configuration saving and directory browsing were verified in the previous step,
`evidence/native-paths/`. The owner confirmed visible folders and the `.cfg`
file. That step supplies native argv/defaults, preserves the live config across
updates, avoids procfs during config saving and uses 64 KiB SDK directory reads.
Its 90-second capture and failed intermediate attempts remain in that evidence.

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
