# PS5 RetroArch 🎮

**Native RetroArch for jailbroken PlayStation 5 consoles, rendering through
[PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan)'s port of Mesa's RADV
Vulkan driver.**

Maintained by [Mihawk](https://github.com/mihawk-99). Based on
[RetroArch / libretro](https://github.com/libretro/RetroArch), with a native PS5
application foundation derived from
[ProsperoLight](https://github.com/blackbearreloaded/ProsperoLight).

> [!IMPORTANT]
> **Piracy is not condoned.** This project ships no games, no BIOS files, no
> console firmware and no decryption keys, and never will. Use only **legally
> obtained backups of games you own**, made yourself from your own discs,
> cartridges or digital purchases, and BIOS or firmware files dumped from
> **hardware you own**. Requests for, or links to, games, BIOS files, firmware
> or keys are not welcome in this project's issues or discussions.
>
> **PlayStation 3 (RPCS3) is not in any release.** RPCS3 is licensed
> GPL-2.0-only and this port is GPL-3.0-or-later: the two cannot be distributed
> together as one program. Its source is public, and **you must compile it
> yourself from source** for your own console (see
> [PlayStation 3 (RPCS3): build it yourself](#playstation-3-rpcs3-build-it-yourself)).
> Do not share or redistribute the binaries you build.

The title launches as a homebrew title, presents XMB through RetroArch's Vulkan
video driver, and runs sixteen native libretro cores in its releases, seven of
them (PSP, GameCube/Wii, PlayStation 2, PlayStation, Nintendo 64, Nintendo 3DS
and Dreamcast) rendering on the PS5's GPU. A seventeenth, RPCS3 (PlayStation 3),
is available only as source that you build yourself. Input, stereo audio,
configuration persistence and content browsing have been verified on a
console. This is an active development project; the tested paths below do not
imply complete core compatibility or Vulkan conformance.

**Latest release: v0.5.7-alpha.5** (a new 4K launcher background; otherwise
v0.5.6-alpha.5) — see its
[release notes](https://github.com/mihawk-99/PS5_RetroArch/releases/tag/v0.5.7-alpha.5)
and [every release](https://github.com/mihawk-99/PS5_RetroArch/releases).
v0.5.0-alpha.5 was the first release on RADV; v0.4.0-alpha.4 was the last on
ps5vk, the project's first driver.

## Table of contents

- [Current status](#current-status)
- [Available cores](#available-cores)
- [Graphics and native runtime](#graphics-and-native-runtime)
- [Roadmap](#roadmap)
- [Build from source](#build-from-source)
- [Install and file locations](#install-and-file-locations)
- [Testing and troubleshooting](#testing-and-troubleshooting)
- [Documentation](#documentation)
- [Authors and acknowledgements](#authors-and-acknowledgements)
- [License and third-party terms](#license-and-third-party-terms)

## Current status

| Feature | Status |
| --- | --- |
| Native title startup and Quit | ✅ Working, including splash dismissal and clean native exit |
| Vulkan video output | ✅ Menu and software-core frames presented through RADV, Mesa's Vulkan driver, linked into the title (PS5_Vulkan's port, reporting Vulkan 1.4). ps5vk remains a build option |
| XMB | ✅ Default menu, with icons, fonts and background rendering |
| RGUI | ✅ Alternative menu |
| Native controller input | ✅ Buttons, left-stick menu navigation and button/axis binding capture |
| Native audio | ✅ `audio_ps5` stereo PCM output, audible channel test and buffering diagnostics |
| Filesystem and configuration | ✅ Directory browsing, configuration loading/saving and FTP-writable application folders |
| Core loading | ✅ Native shared-core loader, official `.info` discovery and recovery from rejected loads |
| Content loading | ✅ Tested games and archives with the cores below. Load Content opens two roots: **INTERNAL**, the title's folder, and **EXTERNAL**, the console's `/mnt`, where USB drives and extended storage mount. The title's sandbox hides `/mnt` for now, so EXTERNAL is empty |
| Colour and menu transitions | ✅ Corrected pixel uploads; Quick Menu/Close Content/next-game transitions I verified on the console |
| Hardware-rendered cores | ✅ PPSSPP, Dolphin, LRPS2, Beetle PSX HW, Mupen64Plus-Next (ParaLLEl-RDP) and Azahar render on RADV through their Vulkan renderers, with JITs where they have them, at up to 18× internal resolution; PPSSPP's MSAA works |
| Shader cache | ✅ RADV keeps compiled pipelines in `radv-shader-cache/`: a game's next start reads them back instead of compiling |
| Save states and fast-forward | ✅ Save/load states (including `--entryslot`) and fast-forward, tested with PPSSPP and mGBA |
| 120 Hz output | ✅ 120 Hz by default where the display offers it; the refresh is measured, and a display that stays at 60 Hz gets 60 Hz |
| Stability | ✅ On RADV (v0.5.6-alpha.5): every core in the release with its game through boot, the menu opened and closed twice, Close Content and a reload, at full speed before and after, with no crash (Beetle Saturn without a BIOS, so up to its BIOS check); earlier releases also passed a 10-minute PPSSPP soak with 25 menu toggles |
| CPU video fallback | ✅ `video_ps5` remains registered and selectable |
| Development diagnostics | ✅ `retroarch.log`, startup/GPU trace, kernel captures and optional buffered frame timing |

The v0.5.6-alpha.5 checks recorded no crash and no kernel fatal signal, and
full-speed audio once each game had booted. These results apply to the tested
games, not every possible workload; the release notes list them, and
[committed evidence](evidence/) holds earlier captures.

## Available cores

The title build includes these cores and their official metadata. FCEUmm,
mGBA, Snes9x, FBNeo, Genesis Plus GX, Beetle Saturn, VICE, MAME and DeSmuME
**render emulated games in software**; RetroArch uploads their frames and presents
them through Vulkan. PPSSPP, Dolphin, LRPS2, Beetle PSX HW, Mupen64Plus-Next,
Azahar and Flycast **render on the GPU** through RADV, with their Vulkan
renderers.

Each core's PS5 defaults are its highest graphical settings that hold full speed
in the games I tested: the most internal resolution the core offers, unless a
lower one is the most that keeps full speed (DeSmuME).

| Core | Systems covered by the core | Console verification in this port |
| --- | --- | --- |
| [FCEUmm](https://github.com/libretro/libretro-fceumm) | NES / Famicom | ✅ Gameplay, audio and input; subsequent shared menu-transition fixes verified. [Evidence](evidence/native-core-loading/) |
| [mGBA](https://github.com/libretro/mgba) | Game Boy, Game Boy Color, Game Boy Advance | ✅ GB/GBC/GBA loading, corrected colours and clean menu/next-game transitions. [Evidence](evidence/mgba-native/) |
| [Snes9x](https://github.com/libretro/snes9x) | SNES / Super Famicom | ✅ Tested gameplay, colours, audio/input and menu transitions; not every special chip or video mode. [Evidence](evidence/snes9x-native/) |
| [FinalBurn Neo](https://github.com/libretro/FBNeo) | Supported arcade boards, including Neo Geo and Sega System 16/32 | ✅ Tested arcade games using both native 32-bit and converted 16-bit output; not every board or ROM set. [Evidence](evidence/fbneo-native/) |
| [Genesis Plus GX](https://github.com/libretro/Genesis-Plus-GX) | Mega Drive / Genesis, Master System, Game Gear, SG-1000, Sega CD | ✅ Genesis gameplay and clean transitions, which I confirmed on the console. Other Sega systems and disc/BIOS paths still need separate acceptance. [Evidence](evidence/genesis-plus-gx-native/) |
| [PPSSPP](https://github.com/hrydgard/ppsspp) v1.20.4 | PlayStation Portable | ✅ Tested games at 10× internal resolution (4800×2720), 16× anisotropy: correct picture, full speed at 120 Hz, save states, fast-forward, and closing and reopening games. MSAA renders on RADV (it needs render pass 2, which ps5vk lacked). |
| [Dolphin](https://github.com/libretro/dolphin) 2609 | GameCube, Wii | ✅ GameCube and Wii games tested for up to an hour of play, with the JIT and fast memory, save states and closing and reopening games. One tested game still slows to about 76–85% at some transitions (see the release notes). |
| [LRPS2](https://github.com/libretro/LRPS2) (PCSX2) | PlayStation 2 | ✅ Tested games at 6× internal resolution on the Vulkan hardware renderer, full speed, with multi-threaded VU1 and save states. Needs your own BIOS in `system/pcsx2/bios/`. |
| [Beetle PSX HW](https://github.com/libretro/beetle-psx-libretro) | PlayStation | ✅ 16× internal resolution on the Vulkan renderer, 32-bit colour, PGXP (no wobbling polygons), full speed, and closing and reopening the game. The disc image is read into memory at load. It runs with its built-in OpenBIOS; your own BIOS (`scph5501.bin` and the others its metadata lists) in `system/` is used when present. |
| [Mupen64Plus-Next](https://github.com/libretro/mupen64plus-libretro-nx) | Nintendo 64 | ✅ ParaLLEl-RDP at 4× upscaling (the default since v0.5.6-alpha.5; 8× stays an option) and ParaLLEl-RSP, both JITs on: 59.9 fps with clean audio in the tested game, and closing and reopening it. A new upscaling factor takes effect when the game is started again (Close Content, then load it). |
| [Beetle Saturn](https://github.com/libretro/beetle-saturn-libretro) | Sega Saturn | ⚠️ Loads, then needs your own BIOS in `system/`: `mpr-17933.bin` (US/EU) or `sega_101.bin` (JP). Without it the game refuses to load and the menu stays usable. Gameplay not yet tested. |
| [VICE](https://github.com/libretro/vice-libretro) x64sc | Commodore 64 | ✅ A `.d64` disk game at full speed, and closing and reopening it. |
| [MAME](https://github.com/libretro/mame) 0.289 | Arcade | ✅ A tested game from a 0.289 non-merged set, BIOS in the same folder: full speed, and closing and reopening it. Raster games render at their native size and are scaled on the GPU. Vector games are drawn at 4K by MAME's alternate renderer (not yet tested on the console). Sets must match 0.289. |
| [DeSmuME](https://github.com/libretro/desmume) | Nintendo DS | ✅ 5× (1280×960) with the JIT and eight rasterizer threads, full speed, and closing and reopening the game. 6× measured 93–95%. |
| [Azahar](https://github.com/azahar-emu/azahar) | Nintendo 3DS | ✅ 18× internal resolution (the most Azahar offers) on Vulkan, with asynchronous shader compilation and the JIT, full speed after boot, and closing and reopening the game. Decrypted games only. |
| [Flycast](https://github.com/sysfce2/libretro-flycast) | Dreamcast, NAOMI/NAOMI 2, Atomiswave | ⚠️ The core loads and boots the Dreamcast BIOS on the console; the dynarecs run on title-provided executable memory (`ps5platform/exec.h`). Full game verification is still pending. Needs your own BIOS in `system/dc/` (`dc_boot.bin`, `dc_flash.bin`; `naomi.zip`/`awbios.zip` for the arcade boards). |
| [RPCS3](https://github.com/mihawk-99/PS5_RPCS3) (my fork) | PlayStation 3 | ⚠️ **Not in any release: build it yourself from source** ([how](#playstation-3-rpcs3-build-it-yourself); its licence, see [License and third-party terms](#license-and-third-party-terms)). On my console build: 4K at 60 fps in gameplay in one tested game, and 4K held at 30 fps in another (a game with only an "Unlock FPS" patch runs it with RPCS3's frame limit at 30 by default, the "Frame-rate patches" option), steady where the emulation keeps up, with its busiest scenes still at 24–27 fps. Needs your own PS3 system software (`PS3UPDAT.PUP` in `system/RPCS3/`) and your own games; a PSN purchase installs with its `.rap` licence file. |

None of the games I tested with is provided.
**Use only legally obtained backups of games you own**, and BIOS files dumped
from your own hardware: piracy is not condoned.

Use **FBNeo for Sega System 16/32 arcade sets**, rather than Genesis Plus GX.
FBNeo needs compatible arcade sets and receives its ZIP/7z archives intact.
Archive support and BIOS requirements vary by core.

Core binaries must be built for **this native pipeline and SDK**. A desktop `.so`
or a core from a different PS5 RetroArch distribution is not automatically
compatible. No games or BIOS files are bundled.

## Graphics and native runtime

```text
Software core → video callback → RetroArch Vulkan video driver
                                → RADV, linked into the title → PS5 display
XMB / RGUI ──────────────────────┘
Hardware cores (PPSSPP, Dolphin, LRPS2, Beetle PSX HW,
Mupen64Plus-Next, Azahar, Flycast) → Vulkan through RetroArch's HW context → RADV
```

[PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan), which I also maintain,
is the separate GPU-driver project used here. Since v0.5.0-alpha.5 the title
links its port of RADV: Mesa's Vulkan driver and ACO compiler, unchanged but
where the console differs, over a PS5 winsys, built from my Mesa fork
[PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa); v0.5.6-alpha.5 links its
revision `0b2d6d1`. It reports Vulkan 1.4, and its conformance is that
project's milestone: the full Khronos CTS runs on the console, and its second
full run ended with no failure. Up to v0.4.0-alpha.4 the title linked ps5vk, the project's first
driver, which `PS5_VULKAN_DRIVER=ps5vk` still builds.

The driver is **linked into the title**. Updating a driver checkout does not
update the linked code: rebuild and redeploy the RetroArch title against the
intended driver artifacts. A `libvulkan.so.1` left in the title folder by an
earlier release is ps5vk's and is not used.

This repository supplies the frontend/platform integration, native audio and
input backends, core loader, build scripts and console validation. Core-side
pixel adapters preserve the renderer's buffers while matching the frontend's
upload format. The CPU video backend remains available as a fallback; the
current default is `video_driver = "vulkan"`, `menu_driver = "xmb"`.

PPSSPP's JIT runs: its code memory is mapped read-write and then made
executable, and its fast-memory fault handler reads the console's own signal
context layout. The port builds PPSSPP v1.20.4 with one patch
(`patches/ppsspp/ps5-port.patch`) and FFmpeg 3.0.2 for game videos. It starts
with the settings I test with (10× internal resolution, 16× anisotropy, auto
max-quality filtering, hardware transform, software skinning, no frameskip, no
speed hacks); an existing `PPSSPP.opt` is set aside once as
`PPSSPP.opt.before-ps5-profile`. The native loader has explicit limits, including
no TLS or general exception-unwind registration, and it waits for a core's
threads to finish before unmapping the core.

## Roadmap

**✅ = verified for the stated scope. ❌ = pending implementation or acceptance
in this port, even if upstream RetroArch already offers the feature.**

### Frontend and platform

- ✅ Native startup and clean exit.
- ✅ GPU presentation through PS5_Vulkan; selectable CPU fallback.
- ✅ XMB, with RGUI retained.
- ✅ Native input, analog menu navigation and remapping.
- ✅ Native stereo audio and buffering diagnostics.
- ✅ Filesystem browsing, configuration persistence and content loading.
- ✅ Native core loading, metadata and failed-load recovery.
- ❌ Save RAM and save-state persistence verified across restarts and core changes.
- ❌ Core-option persistence and per-game/per-core overrides fully validated.
- ❌ BIOS/system-file coverage, disc swapping and multi-disc acceptance tests.
- ❌ RetroAchievements and netplay; networking is disabled in the current frontend build.
- ❌ User Slang shader presets and multipass effects validated on PS5_Vulkan.
- ✅ 120 Hz output where the display offers it, with a 60 Hz fallback chosen by measuring the refresh.
- ❌ No core losing speed while a shader compiles. On RADV (Alpha 5), PPSSPP's compiles on its own threads no longer cost it speed, and a game's second start reads its pipelines from the cache; Dolphin's first start of a game still compiles its ubershaders at a cost (see the release notes).
- ✅ Save states and fast-forward, including PPSSPP.
- ❌ 4K output/upscaling, VRR and HDR validated in this application.
- ❌ Low-latency features, runahead and sustained per-core performance measurements.
- ❌ Broader compatibility testing and release qualification.

### Cores

| Status | Core / milestone |
| --- | --- |
| ✅ | FCEUmm — NES |
| ✅ | mGBA — GB / GBC / GBA |
| ✅ | Snes9x — SNES |
| ✅ | FBNeo — tested arcade games |
| ✅ | Genesis Plus GX — tested Genesis gameplay |
| ❌ | Beetle PCE — PC Engine / TurboGrafx-16, SuperGrafx and CD; next proposed addition |
| ❌ | Stella — Atari 2600; candidate |
| ❌ | PicoDrive — add Sega 32X coverage; candidate |
| ✅ | MAME 0.289 — arcade; tested games only |
| ✅ | Beetle PSX HW — PlayStation, Vulkan renderer at 16× |
| ✅ | Mupen64Plus-Next — Nintendo 64, ParaLLEl-RDP at 4× (8× selectable) |
| 🚧 | Beetle Saturn — Sega Saturn; needs a gameplay test with a BIOS |
| ✅ | VICE x64sc — Commodore 64 |
| ✅ | DeSmuME — Nintendo DS at 5× |
| ✅ | Azahar — Nintendo 3DS, Vulkan at 18× |
| ❌ | EXTERNAL storage (USB, extended storage) readable from inside the title's sandbox |
| ✅ | PPSSPP — PSP, Vulkan rendering and JIT; tested games only |
| ✅ | PPSSPP MSAA — render pass 2 and depth/stencil resolve, on RADV |
| ✅ | Dolphin — GameCube and Wii, Vulkan rendering and JIT; tested games, long play and the enhancement profiles |
| ✅ | LRPS2 — PlayStation 2, Vulkan hardware renderer at 4K; tested games only |
| 🚧 | LRPS2 — upstream PCSX2's newer renderer fixes, 8× internal resolution and texture replacement |
| 🚧 | RPCS3 — PlayStation 3 at 4K; source only, never in a release (build it yourself); the busiest scenes of a 30 fps game still below 30 |

Future entries are development targets, not a promised release order. Hardware
rendering introduces new Vulkan requirements beyond presenting software frames;
PPSSPP was the first core to exercise them, and six cores do now.

## Build from source

The current build uses Linux host tools and the project's cached public PS5 SDK.
Start with `bash tools/doctor.sh` for host-tool checks. You also need `curl`,
`patch`, `pkg-config`, ELF utilities such as `readelf`, and working host C/C++
compilers with sanitizer support for the tests. The target scripts currently use
`/usr/bin/clang` through the SDK wrappers.

Prepare and build [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) following
its own instructions, normally as a sibling directory:

```text
workspace/
├── PS5_RetroArch/
├── PS5_Vulkan/       # The RADV release archive, its link recipe and dependencies
├── PS5_Mesa/         # My Mesa fork, which PS5_Vulkan builds RADV from
└── PS5_PayloadSDK/   # My payload SDK fork and its platform layer, at a pinned revision
```

The cores that needed changes for the console build from my forks of them
([PS5_LRPS2](https://github.com/mihawk-99/PS5_LRPS2),
[PS5_BeetlePSX](https://github.com/mihawk-99/PS5_BeetlePSX),
[PS5_Mupen64Plus](https://github.com/mihawk-99/PS5_Mupen64Plus),
[PS5_BeetleSaturn](https://github.com/mihawk-99/PS5_BeetleSaturn),
[PS5_VICE](https://github.com/mihawk-99/PS5_VICE),
[PS5_MAME](https://github.com/mihawk-99/PS5_MAME),
[PS5_DeSmuME](https://github.com/mihawk-99/PS5_DeSmuME),
[PS5_Azahar](https://github.com/mihawk-99/PS5_Azahar) with
[PS5_Dynarmic](https://github.com/mihawk-99/PS5_Dynarmic), and, for builds you
make yourself, [PS5_RPCS3](https://github.com/mihawk-99/PS5_RPCS3) with
[PS5_LLVM](https://github.com/mihawk-99/PS5_LLVM)), each pinned by revision in
its build script. The script uses the sibling checkout when there is one, and
`github.com/mihawk-99/<fork>` otherwise.

The title build consumes PS5_Vulkan's RADV release archive
(`tools/build-radv.sh release` there, built from PS5_Mesa at the revision it
pins) and links it with that project's `tools/radv-link.sh`; it does not build
the driver for you. `PS5_VULKAN_DRIVER=ps5vk` links ps5vk's driver, Vulkan
runtime and shader-compiler archives instead, and `RADV_ARCHIVE` names another
RADV archive. `PS5_VULKAN_DIR` can select an alternative checkout for the
build; some host tests currently require the sibling layout above. Driver
dependencies and setup requirements are documented in the driver repository.

From the RetroArch repository root:

```bash
make deps
export PS5_PAYLOAD_SDK="$PWD/.deps/native/ps5-payload-sdk"
export PS5_CLANG=/usr/bin/clang
bash tools/fetch-retroarch.sh
bash tools/build-title.sh     # Create the artifacts inspected by the host tests
bash tools/verify.sh
```

The five gates are **format → unit → build → integration → evidence**. The build
pins RetroArch 1.22.2, fetches core sources/metadata with checked hashes, builds the
frontend and all sixteen cores, and stages the native title in `dist/PPSA99169/` (a
release build, `PS5_RELEASE_TAG` set, leaves RPCS3 out).
The initial dependency/source fetch requires network access.

For an already configured checkout:

```bash
bash tools/build-title.sh     # Build/stage the frontend and all shipped cores
make genesis-plus-gx         # Build and ABI-check one core only
# Other core targets: fceumm, mgba, snes9x, fbneo
bash tools/build-ppsspp.sh   # The larger cores have scripts of their own:
                             # build-ppsspp.sh, build-dolphin.sh, build-lrps2.sh,
                             # build-beetle-psx.sh, build-mupen64plus.sh,
                             # build-beetle-saturn.sh, build-vice.sh,
                             # build-mame.sh, build-desmume.sh, build-azahar.sh,
                             # build-rpcs3.sh (your own builds only)
```

When adding or updating a core, rebuild the title too: the frontend's native
import table and build identity depend on the shipped core binaries. Source
patches live in `patches/`; fetched and generated trees stay in ignored
`vendor/`, `.deps/`, `build/` and `dist/` directories.

### PlayStation 3 (RPCS3): build it yourself

No release of this title carries RPCS3, and none will while its licence stands
as it does: RPCS3 is **GPL-2.0-only**, and the title it runs in is
**GPL-3.0-or-later** (this port's runtime is linked into the core, and the core
runs against the title's GPL-3.0 platform code), so the two cannot be handed
out together as one program. What is public is the source: my fork
[PS5_RPCS3](https://github.com/mihawk-99/PS5_RPCS3) and this repository's build
scripts. **If you want RPCS3, you must compile it yourself**, for your own
console:

```bash
bash tools/build-title.sh     # a development build: the frontend and every core, RPCS3 included
```

Leave `PS5_RELEASE_TAG` unset: a release build (`PS5_RELEASE_TAG=...`) leaves
RPCS3 out, and `tools/check-notices.py --release` refuses a title with any
RPCS3 file in it. Build the whole title, not the core alone: the title's native
import table is made from the cores it is built with, so a release title cannot
load an RPCS3 core built on its own. `tools/build-rpcs3.sh` builds only the
core (from PS5_RPCS3, with LLVM from my fork PS5_LLVM, at their pinned
revisions), for work on it.

Keep what you build for your own console: **do not share, upload or
redistribute the binaries you build.** RPCS3 needs your own copy of the PS3
system software (`PS3UPDAT.PUP`, from Sony's official PS3 system software
update page) in `system/RPCS3/`, where it installs on the first start, and
your own games: a disc you own, dumped yourself, or a PSN purchase with its
`.rap` licence file. Piracy is not condoned.

## Install and file locations

The verified deployment is a **homebrew title folder**, not a retail package.
Copy the complete `dist/PPSA99169/` tree to the title location used by your
configured homebrew launcher. The current validation setup uses
`/data/homebrew/PPSA99169/`. This project does not install a jailbreak or launcher.

`/app0` is the running application's mount point. Over FTP, use the corresponding
title folder instead:

| Purpose | Under `/data/homebrew/PPSA99169/` | In RetroArch |
| --- | --- | --- |
| Native cores | `cores/` | `/app0/cores/` |
| Core metadata | `info/`, with compatibility copies in `cores/` | `/app0/info/` |
| Games | `content/` | `/app0/content/` |
| BIOS/system data | `system/` | `/app0/system/` |
| PS2 BIOS (your own dump) | `system/pcsx2/bios/` | `/app0/system/pcsx2/bios/` |
| PS3 system software, RPCS3's drives (your own build only) | `system/RPCS3/` | `/app0/system/RPCS3/` |
| Live configuration | `config/retroarch.cfg` | `/app0/config/retroarch.cfg` |
| Save RAM | `savefiles/` | `/app0/savefiles/` |
| Save states | `savestates/` | `/app0/savestates/` |
| RADV shader cache | `radv-shader-cache/` | `/app0/radv-shader-cache/` |

RADV creates `radv-shader-cache/` itself, open to FTP like the other folders;
deleting it only makes the next start compile again. `ps5vk-shader-cache/` and
`sce_module/libvulkan.so.1`, left by releases up to v0.4.0-alpha.4, are not used
since v0.5.0-alpha.5 and can be deleted.

Everything you put in `content/` and `system/` must be **your own legally
obtained backups**: games you own, and BIOS or firmware dumped from hardware you
own. Piracy is not condoned.

For FBNeo, use `system/fbneo/` for its system files. Genesis Plus GX's Sega CD BIOS
filenames belong in the configured `system/` root, as listed by its metadata.

The application creates its managed writable folders and seeds live settings
only when no live configuration exists. Ordinary scripted updates preserve user
content and saved settings. Existing settings can therefore keep RGUI selected
even though XMB is the packaged default. Back up user files before any clean
removal; `--clean` removes the entire title folder.

`tools/deploy-title.py` publishes a built `dist/PPSA99169/` over FTP and reads
every file back; console details belong in the ignored `.env`, based on
`.env.example`.

## Testing and troubleshooting

A successful build proves neither gameplay nor correct rendering. Core acceptance
includes native loading, gameplay, colour checks, audio/input, Quick Menu →
Close Content, and loading another game. I record my own visual confirmation on
the console alongside the logs; a camera can miss refresh-synchronous flicker.

Before each release every core in it runs its game on the console: boot, the
menu opened and closed twice, Close Content and a reload, with its frame rate,
audio and a screenshot checked before and after. The release notes give the
results; they are the tested games, not a core's upstream feature list.

For reports, include the core, game-file format, relevant settings, reproduction
steps and whether the application was closed manually. Preserve `retroarch.log`
and `trace.txt` before reopening: the frontend log is replaced on a new launch.
The test tools retain kernel captures in ignored `klog/`. Redact private paths,
console addresses and credentials before sharing logs.

Old `gpu-buffers-*.bin`, `gpu-stages-*.bin` and `gpu-tables-*.bin` files are temporary
rendering diagnostics from earlier investigations. They are not required runtime
assets and can be removed. Keep the normal development logs when reporting bugs.

## Documentation

Each release's notes on the
[Releases page](https://github.com/mihawk-99/PS5_RetroArch/releases) are the
public record of what changed, what was tested and what is known not to work.
The committed [evidence](evidence/) holds machine-readable captures and their
expected results. My design notes, logs and procedures are kept locally and are
not published.

## Authors and acknowledgements

This port builds on substantial upstream and PS5 homebrew work. Credits below
identify project authors and teams; their repositories retain the full contributor
lists and original notices.

### Frontend, platform and graphics

| Project / author | Contribution |
| --- | --- |
| [Mihawk](https://github.com/mihawk-99) — [PS5_RetroArch](https://github.com/mihawk-99/PS5_RetroArch), [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan), [PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa), [PS5_PayloadSDK](https://github.com/mihawk-99/PS5_PayloadSDK) | This native RetroArch port and core integration; the PS5 Vulkan drivers (the RADV port and ps5vk); the Mesa fork with the PS5 winsys; the payload SDK fork and its platform layer; the PS5 forks of the cores below |
| [RetroArch / libretro contributors](https://github.com/libretro/RetroArch) | Frontend, libretro API, menus, video pipeline and shared libraries |
| [BlackBearReloaded — ProsperoLight](https://github.com/blackbearreloaded/ProsperoLight) | Project starting point and reference for native PS5 input and audio integration |
| [BlackBearReloaded — PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) | Underlying native title tooling, ELF/FSELF conversion and runtime-shim foundation |
| [John Törnblom and ps5-payload-dev contributors](https://github.com/ps5-payload-dev/sdk) | Public PS5 Payload SDK, toolchain and API stubs; [PacBrew](https://github.com/ps5-payload-dev/pacbrew-repo) ports infrastructure |
| [John Törnblom / ps5-payload-dev — websrv](https://github.com/ps5-payload-dev/websrv) | Reference for per-core fetch/build/stage scripts; this port uses a separate native title pipeline |
| [BlackBearReloaded — ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl) | Shader-compiler and graphics foundations consumed by PS5_Vulkan; not the active RetroArch video backend |
| [Mesa contributors](https://gitlab.freedesktop.org/mesa/mesa) | RADV, the Vulkan driver the title renders through, with its ACO compiler, NIR, the Vulkan runtime and utilities |
| [Khronos Group](https://github.com/KhronosGroup/Vulkan-Headers) | Vulkan API headers and [specification](https://github.com/KhronosGroup/Vulkan-Docs) |
| [RetroArch assets contributors](https://github.com/libretro/retroarch-assets) and the [M+ Fonts project](https://mplusfonts.github.io/) | Packaged XMB assets and font; original notices retained |
| [LLVM / Clang contributors](https://github.com/llvm/llvm-project), [zlib authors Jean-loup Gailly and Mark Adler](https://github.com/madler/zlib) | Compilation and compression tooling |

### Emulator cores

| Core | Authors / maintainers credited by upstream |
| --- | --- |
| [FCEUmm](https://github.com/libretro/libretro-fceumm) | FCEU Team, CaH4e3 and contributors |
| [mGBA](https://github.com/libretro/mgba) | endrift and contributors |
| [Snes9x](https://github.com/libretro/snes9x) | Snes9x Team and contributors |
| [FinalBurn Neo](https://github.com/libretro/FBNeo) | Team FBNeo and contributors |
| [Genesis Plus GX](https://github.com/libretro/Genesis-Plus-GX) | Charles MacDonald, Eke-Eke and contributors |
| [PPSSPP](https://github.com/hrydgard/ppsspp) | Henrik Rydgård and contributors |
| [Dolphin](https://github.com/dolphin-emu/dolphin), [libretro/dolphin](https://github.com/libretro/dolphin) | Dolphin Emulator Project and contributors; libretro core maintainers |
| [PCSX2](https://github.com/PCSX2/pcsx2), [LRPS2](https://github.com/libretro/LRPS2) | PCSX2 Dev Team and contributors; libretro LRPS2 maintainers |
| [Beetle PSX HW](https://github.com/libretro/beetle-psx-libretro), [Beetle Saturn](https://github.com/libretro/beetle-saturn-libretro) | The Mednafen authors and contributors; libretro Beetle maintainers |
| [Mupen64Plus-Next](https://github.com/libretro/mupen64plus-libretro-nx) | Mupen64Plus team and contributors; libretro core maintainers; ParaLLEl-RDP and ParaLLEl-RSP by Hans-Kristian Arntzen (Themaister) and contributors |
| [VICE](https://github.com/libretro/vice-libretro) | The VICE Team and contributors; libretro core maintainers |
| [MAME](https://github.com/libretro/mame) | MAMEdev and contributors |
| [DeSmuME](https://github.com/libretro/desmume) | DeSmuME team and contributors |
| [Azahar](https://github.com/azahar-emu/azahar), [Dynarmic](https://github.com/azahar-emu/dynarmic) | Azahar contributors, building on Citra; Dynarmic by merryhime and contributors |
| [RPCS3](https://github.com/RPCS3/rpcs3) | RPCS3 Team and contributors |
| [libretro core-info](https://github.com/libretro/libretro-core-info) | Metadata maintainers and contributors |

## License and third-party terms

This repository's own code is **GPL-3.0-or-later** ([LICENSE](LICENSE)). Most source
files carry a copyright and SPDX notice; the ones that do not (for example
`src/memory_ps5.cpp` and the build scripts in `tools/`) are under the same licence.
Code inherited from BlackBearReloaded's ps5-native-app-boilerplate and ProsperoLight
is Copyright (C) 2026 BlackBearReloaded, GPL-3.0-or-later. The title's
`sce_module/libc.prx` is generated by this repository (`runtime/`).

Every built title folder carries `LEGAL.txt` (the legal notice above) and
`licenses/`: the licence texts each part requires, and `components.json`, which
ties every executable file to the source revision it was built from
([tooling/notices/components.json](tooling/notices/components.json)). Each
release also carries the source archives of everything in it. Releases up to
v0.5.0-alpha.5 were published without `licenses/`.

**RPCS3 is licensed GPL-2.0-only, which is incompatible with this port's
GPL-3.0-or-later.** For that reason no release of this title contains RPCS3 or
any of its files, and you must compile it yourself from source if you want it
(see [PlayStation 3 (RPCS3): build it yourself](#playstation-3-rpcs3-build-it-yourself));
the binaries you build are for your own console only.

The cores keep their own licences, and they differ:

| Licence | Cores |
| --- | --- |
| GPL-2.0-or-later | FCEUmm, PPSSPP, Dolphin, Beetle PSX HW, Beetle Saturn, Mupen64Plus-Next (with MIT and LGPL parts), VICE, DeSmuME, Azahar (Dynarmic is 0BSD), MAME (as a whole; many files BSD-3-Clause) |
| GPL-3.0-or-later | LRPS2 (PCSX2) |
| GPL-2.0-only | RPCS3: **not in any release; compile it yourself from source.** Its source is public and it builds with this repository, but it cannot be distributed together with this port's GPL-3.0 code |
| MPL-2.0 | mGBA |
| Non-commercial licences | Snes9x, FinalBurn Neo, Genesis Plus GX: they may not be sold or used commercially, and FBNeo's forbids asking for donations for a project that uses its code |

Assets and fonts keep their licences too: the XMB theme is CC-BY-4.0 with the M+
font licence, PPSSPP's fonts are OFL-1.1, and Dolphin's `Sys` files carry theirs.
The launcher backgrounds (`sce_sys/pic0.dds`, `pic1.dds`) are drawn by
`tools/make-title-art.py`, under this repository's licence.

**No games, BIOS files, firmware or keys are included, and piracy is not
condoned.** Use only legally obtained backups of games you own and system files
dumped from hardware you own.

This is an independent homebrew project, not affiliated with or endorsed by Sony
Interactive Entertainment, the Khronos Group or the libretro project. PlayStation
and PS5 are Sony trademarks. Vulkan is a registered trademark of the Khronos Group
Inc.; the RADV port this title uses is not a Khronos-conformant product (see
[PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan)). RetroArch is the libretro
project's name and logo, used here to name the frontend this port is built from.
