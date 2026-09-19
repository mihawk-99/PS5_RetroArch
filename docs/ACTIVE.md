# Active work

_Updated: 2026-09-19_

## Now

**FCEUmm build and metadata staging pass; native core loading is blocked.**
The user requested a core build script using this project's SDK, with manual
core/game selection for console testing. This follows that request rather than
continuing the earlier input step. No PS5_Vulkan changes were needed.

`make fceumm` fetches SHA-256-verified source and metadata, builds with this
project's cross SDK, checks the shared ELF and stages the `.so` and `.info`.
`tools/build-title.sh` includes them in the title. Metadata is placed in both
`info/` and `cores/`: the owner's saved empty core-info path uses the latter.
Deployment refreshes the metadata cache and strictly checks core SHA-256.

## Verified build and evidence

- Frontend identity: `261d33099adec97c231205beb21d6011e1587436df099d4e6ac6f29949c50c8f`.
- FCEUmm upstream revision: `236ccdfc911e84c60fea6b9d0699c2d440a8de14`.
- Core SHA-256: `d1afcd2ea84de627f4a28d8ec4e81d63d23ca5b531811ba57357d98f8b31759f`.
- Core size 4,836,144 bytes; ELF64 x86-64 ET_DYN FreeBSD ABI, 25 callbacks,
  16 KiB-compatible segments, native libc/Posix/kernel_web imports.
- Repeated fresh builds give the same hash after replacing Clang's random
  PS5 build UUID with `--build-id=sha1`. FTP returns exactly those core bytes.
- Console command: `tools/run-title.sh --no-build --watch 120`; owner manually
  selected core/content. The decoded console cache reports FCEUmm's full name,
  `fds|nes|unif|unf` and `has_info: true`. Metadata discovery is verified.
- All five host gates pass; 45 tests. Evidence: `evidence/fceumm-build/` includes
  ABI report and the failed runtime result. This is not playable-core acceptance.
- Raw logs: `klog/fceumm-metadata-run.log`, `klog/retroarch-135742.log`,
  `klog/run-PPSA99169-135742.log`. User filenames remain in ignored captures.
- PS5_Vulkan remains unchanged at **6f0ce0d**.

## Open target blockers

1. **Core open fails:** both manual tests log `Failed to open libretro core`
   for `/app0/cores/fceumm_libretro.so`, followed by `Error(s): (null)`.
   Correct metadata and a byte-identical upload do not resolve native loading.
2. **Frontend recovery crashes:** after the owner starts content, initialization
   reports that the dynamic core path is unset. The main loop then calls
   `vulkan_alive` with null context and faults reading address `0x80`.
   RIP `0xab3066`, image base `0x400000`, ELF offset `0x6b3066`; symbolized with
   `addr2line -Cfipe build/llvm-pie.elf 0x6b3066 0xae4774 0xad7db3 0x3df4 0x190`.
   The owner explicitly confirmed this crash; it was not a manual close.

These failures are recorded rather than waived as core success. Build/staging is
verified, but M3 core execution remains incomplete. No game frames/audio/saves
were verified. Implementation and acceptance plan: `parked/native-core-loading/`.
Do not substitute a static core, donor SDK or CPU video route to hide the blocker.

## Preserved platform functionality

Native joypad/menu analog navigation and binding capture were confirmed by the
owner in `evidence/native-joypad/`. Native audio and buffering are verified in
`evidence/native-audio/`; real-core audio and long-run A/V sync remain unverified.
XMB stays default with statically linked Vulkan; `video_ps5` and RGUI remain
registered/selectable. Single-level menu textures remain the verified path.
Useful frontend/core/audio/error logs are retained without frame-success chatter.

Config saving and directory browsing are verified in `evidence/native-paths/`.
Managed directories are repaired to `0777`; FTP upload/readback/cleanup was
verified in `evidence/ftp-directory-permissions/`. Deployments preserve live config
and user content. Input remains limited to the initial user's single controller;
rumble, multiple controllers and persistent remapping need separate acceptance.

## User file locations

FTP base: `/data/homebrew/PPSA99169/`; in-title mount: `/app0`.
Use `cores/` for compatible cores, `content/` for games, `system/` for BIOS,
`config/retroarch.cfg` for settings, and `savefiles/` / `savestates/` for saves.
The build now ships experimental FCEUmm and metadata, but no game or BIOS.
See `docs/DEPLOYMENT.md` for build/deploy procedures and file locations.

## Operating notes

The user authorized uploads/runs without further permission and necessary driver
fixes with documentation plus a PS5_Vulkan commit. Never interrupt an existing
title. The runner checks idle before upload and launch. Core/game selection is
manual at the user's request; collect retroarch.log afterward. For appended trace
files, select the last `bss check=` substring and verify the build identity.
