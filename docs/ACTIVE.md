# Active work

_Updated: 2026-09-19_

## Now

**Left-stick menu navigation and input binding capture work on the PS5.** The
owner tested both on the native joypad build and replied: "Everything works
flawlessly!" This follows the new input issue rather than continuing the previous
permission step. No driver work or core implementation was needed.

The old input-only backend bypassed RetroArch's joypad interface. Menu sticks and
the binding screen therefore had nothing to poll, although direct menu buttons
worked. `ps5_joypad` now provides raw buttons/axes and a built-in controller
profile; normal RetroArch binding/deadzone logic handles the resulting input.
`input_ps5` no longer ORs hardcoded controls back into remapped input.

## Verified build and evidence

- Identity: `e6211dea9fd0bb512e83a9979a6a44c0c8c5fdf175564c753e3fd2a25067e0a9`.
- Command: `tools/run-title.sh --no-build --watch 120`; the owner confirmed
  left-stick navigation and button/stick binding capture on this build.
- Follow-up: `tools/run-title.sh --no-build --no-deploy --watch 30` stayed alive
  for the full window and was script-closed. Both runs selected/configured the PS5
  joypad; Vulkan presentation, XMB and audio startup passed with zero GPU errors.
  The owner manually closed the interactive run, explaining its early-exit verdict.
- All five gates PASS; 37 unit tests; 17 evidence records.
- Host tests use actual upstream input mapping and analog helpers with mocked
  native samples. They cover button/axis bindings, deadzones, trigger scaling,
  extra binding-screen polls, interception/disconnect and teardown/reinit.
- Fresh patch replay: 107 applied + one upstream marker; second pass 108 present,
  bytes unchanged. Three new edits register the joypad and built-in profile.
- Evidence: `evidence/native-joypad/`; raw console capture stays in ignored
  `klog/input-joypad-run.log`. Verification: `tools/verify.sh`.
- PS5_Vulkan remains unchanged at **6f0ce0d**. No sibling project was modified.

The previous step verified actual FTP upload/readback/cleanup in all seven managed
folders at `0777`; `0775` was insufficient (`evidence/ftp-directory-permissions/`).
Startup still repairs those directory modes. Configuration saving and browsing
remain documented in `evidence/native-paths/`; deployments preserve live settings.

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

Input currently supports the initial user’s single controller. Rumble, multiple
controllers and saved remapping across a console restart remain unverified. USB
access and real core loading require separate verification on available mounts
and compatible core artifacts.

## Operating notes

The user authorized console uploads/runs without further permission and necessary
driver fixes with documentation plus a PS5_Vulkan commit. Never interrupt an
existing title. The runner checks idle state before upload and launch. For appended
trace files, select the last `bss check=` substring and verify its build identity.
Never substitute the CPU route to conceal a GPU refusal.
