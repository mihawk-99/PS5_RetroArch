# Active work

_Updated: 2026-09-19_

## Now

**Native FCEUmm loading and first gameplay are verified.** The owner answered
“Works flawlessly!” to the manual video/audio/controls test. This implements the
requested native core loading and failed-load recovery step. No PS5_Vulkan
changes were made by this task. The sibling advanced independently during the
work (currently **085c632**); the accepted title identity includes its exact
linked archive bytes and is not attributed to an assumed driver commit.

The title loads this pipeline's shared ELF through a bounded in-process loader,
with explicit native runtime imports and separate RX/R/RW segments. Missing or
unsupported cores return an error. Menu selection checks success before tearing
down the frontend; a late initialization failure stops driver polling safely.
FCEUmm's XRGB8888 frames are converted to matching RGBA8 staging/sampled textures
and presented through the existing Vulkan path.

## Verified build and evidence

- Frontend identity: `9b287ca7e0d4a04721c68d30fb88b678a62cc568c1f2d44b75716b464c21b6d5`.
- FCEUmm source revision: `236ccdfc911e84c60fea6b9d0699c2d440a8de14`.
- Core SHA-256: `fec7dc4eb7ec6cae937ae778f0b59365d8bd7570927a73353424345e13589d9c`.
- Core size: 4,836,368 bytes; 25 callbacks; 71 native bindings; 16,001
  relocations; mapped image 9,076,736 bytes. No payload loader hooks/static core.
- All five host gates pass, with 52 tests. `evidence/native-core-loading/`
  contains loader/recovery reports, ABI report, failed iterations and acceptance.
- Final command: `tools/run-title.sh --no-build --core-test --watch 180`.
  Eight load/export/API/identity/unload cycles passed. A second load with XMB
  resident passed. Missing-core selection and content startup were rejected
  through actual task functions while preserving the initialized menu context.
- The owner manually loaded an NES archive. The core negotiated XRGB8888,
  256x224, 60.10 Hz and 48 kHz audio. The declared rate is not a speed measurement.
- Current-build trace: three launches, zero Vulkan refusals, zero GPU API failure
  records; kernel capture has zero fatal signals. The owner closed/reopened the
  title, and the runner closed a later menu instance. Do not describe this as
  180 seconds of uninterrupted gameplay.
- Raw captures: `klog/native-core-game-rgba-run.log`,
  `klog/run-PPSA99169-143921.log`, `klog/core-rgba-live-retroarch.log` and
  `klog/core-rgba-final-trace.txt`. The live frontend log preserves the gameplay
  session; later menu launches replaced `/app0/retroarch.log`.

## Limits and remaining acceptance

- Return from gameplay to XMB and repeated game unload/reload are **not verified**:
  the owner had not configured a menu shortcut and manually closed the app.
  Repeated ELF load/unload is verified separately by the diagnostic.
- Save RAM, save states, long-run A/V sync and measured core performance remain
  separate milestones. RGB565 content, other cores, runahead and multiple
  controllers are not covered by this FCEUmm acceptance.
- The loader explicitly rejects TLS, constructors/finalizers, unsupported
  relocation types and additional shared dependencies. It is not a general
  system dynamic linker; a faulty core can still crash the shared process.
- Failed-core guards cover the tested menu selection/new-content paths. The
  late-initialization safeguard exits instead of polling freed drivers; recovery
  from every possible content/core callback failure is not claimed.
- Tolerated log: `GET_VARIABLE: fceumm_hdpacks - Not implemented.` The optional
  HD-pack query returns false and upstream uses its default before checking for
  a pack. No pack is shipped/tested. Core-option/HD-pack support needs separate
  validation; this message did not prevent the accepted standard NES game.
- The old stdio/heap file read failed after XMB initialized. Bounded POSIX reads
  into mapped memory fixed the observed failure; the precise libc/allocator
  cause was not isolated. Earlier failed runs remain in the evidence history.

## Preserved platform functionality

Native joypad/menu analog navigation and binding capture are verified in
`evidence/native-joypad/`. Native audio/buffering are in `evidence/native-audio/`.
XMB stays default with statically linked Vulkan; `video_ps5` and RGUI remain
registered/selectable. Useful frontend/core/audio/error logging is retained.

Config saving and directory browsing are verified in `evidence/native-paths/`.
Managed directories are repaired to `0777`; FTP upload/readback/cleanup is in
`evidence/ftp-directory-permissions/`. Deployments preserve live config/content.

## User file locations

FTP base: `/data/homebrew/PPSA99169/`; in-title mount: `/app0`.
Use `cores/` for compatible cores, `content/` for games, `system/` for BIOS,
`config/retroarch.cfg` for settings, and `savefiles/` / `savestates/` for saves.
The build ships FCEUmm and `.info` metadata in both `info/` and `cores/`, with
cache refresh on deployment. No game or BIOS is shipped. `make fceumm` builds
only the core; see `docs/DEPLOYMENT.md` for procedures.

## Operating notes

Uploads/runs are authorized without further permission. Never interrupt an
existing title; the runner checks idle before upload/launch. Core/game selection
is manual. Keep private filenames and console details in ignored captures.
Trace appends across launches: separate `bss check=` sections and verify identity.
A manual close/relaunch may replace the frontend log during a capture window;
save the live gameplay log before it is lost. Do not infer gameplay duration
from the runner's final process count.
