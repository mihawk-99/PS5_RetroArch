# PS5 RetroArch 🎮

**Native RetroArch for jailbroken PlayStation 5 consoles, with GPU presentation
through [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan).**

Maintained by [Mihawk](https://github.com/mihawk-99). Based on
[RetroArch / libretro](https://github.com/libretro/RetroArch), with a native PS5
application foundation derived from
[ProsperoLight](https://github.com/blackbearreloaded/ProsperoLight).

The current build launches as a homebrew title, presents XMB through RetroArch's
Vulkan video driver, and runs five native libretro cores. Input, stereo audio,
configuration persistence and content browsing have been verified on a console.
This is an active development project; the tested paths below do not imply
complete core compatibility or Vulkan conformance.

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
| Vulkan video output | ✅ Menu and software-core frames presented through statically linked `libps5vk` |
| XMB | ✅ Default menu, with icons, fonts and background rendering |
| RGUI | ✅ Alternative menu |
| Native controller input | ✅ Buttons, left-stick menu navigation and button/axis binding capture |
| Native audio | ✅ `audio_ps5` stereo PCM output, audible channel test and buffering diagnostics |
| Filesystem and configuration | ✅ Directory browsing, configuration loading/saving and FTP-writable application folders |
| Core loading | ✅ Native shared-core loader, official `.info` discovery and recovery from rejected loads |
| Content loading | ✅ Tested games and archives with the cores below |
| Colour and menu transitions | ✅ Corrected pixel uploads; Quick Menu/Close Content/next-game transitions I verified on the console |
| Hardware-rendered cores | ✅ PPSSPP renders through PS5_Vulkan (Vulkan backend, JIT), tested at 10× internal resolution |
| Save states and fast-forward | ✅ Save/load states (including `--entryslot`) and fast-forward, tested with PPSSPP and mGBA |
| 120 Hz output | ✅ 120 Hz by default where the display offers it, 60 Hz fallback |
| CPU video fallback | ✅ `video_ps5` remains registered and selectable |
| Development diagnostics | ✅ `retroarch.log`, startup/GPU trace, kernel captures and optional buffered frame timing |

The latest verified gameplay build recorded zero Vulkan driver refusals, zero
GPU API failure records, zero kernel fatal signals and zero audio backend errors.
These results apply to the captured tests, not every possible workload. See
[active state](docs/ACTIVE.md) and [committed evidence](evidence/) for exact builds,
test coverage and known exceptions.

## Available cores

The title build includes these cores and their official metadata. Five of them
**render emulated games in software**; RetroArch uploads their frames and presents
them through Vulkan. PPSSPP **renders on the GPU** through PS5_Vulkan, with its
Vulkan backend and its JIT.

| Core | Systems covered by the core | Console verification in this port |
| --- | --- | --- |
| [FCEUmm](https://github.com/libretro/libretro-fceumm) | NES / Famicom | ✅ Gameplay, audio and input; subsequent shared menu-transition fixes verified. [Evidence](evidence/native-core-loading/) |
| [mGBA](https://github.com/libretro/mgba) | Game Boy, Game Boy Color, Game Boy Advance | ✅ GB/GBC/GBA loading, corrected colours and clean menu/next-game transitions. [Evidence](evidence/mgba-native/) |
| [Snes9x](https://github.com/libretro/snes9x) | SNES / Super Famicom | ✅ Tested gameplay, colours, audio/input and menu transitions; not every special chip or video mode. [Evidence](evidence/snes9x-native/) |
| [FinalBurn Neo](https://github.com/libretro/FBNeo) | Supported arcade boards, including Neo Geo and Sega System 16/32 | ✅ Tested arcade games using both native 32-bit and converted 16-bit output; not every board or ROM set. [Evidence](evidence/fbneo-native/) |
| [Genesis Plus GX](https://github.com/libretro/Genesis-Plus-GX) | Mega Drive / Genesis, Master System, Game Gear, SG-1000, Sega CD | ✅ Genesis gameplay and clean transitions, which I confirmed on the console. Other Sega systems and disc/BIOS paths still need separate acceptance. [Evidence](evidence/genesis-plus-gx-native/) |
| [PPSSPP](https://github.com/hrydgard/ppsspp) v1.20.4 | PlayStation Portable | ✅ God of War: Ghost of Sparta and Yu-Gi-Oh! GX Tag Force at 10× internal resolution (4800×2720), 16× anisotropy: correct picture, full speed at 120 Hz, save states, fast-forward, and closing and reopening games. MSAA is not yet available (see below). |

Use **FBNeo for Sega System 16/32 arcade sets**, rather than Genesis Plus GX.
FBNeo needs compatible arcade sets and receives its ZIP/7z archives intact.
Archive support and BIOS requirements vary by core.

Core binaries must be built for **this native pipeline and SDK**. A desktop `.so`
or a core from a different PS5 RetroArch distribution is not automatically
compatible. No games or BIOS files are bundled.

## Graphics and native runtime

```text
Software core → video callback → RetroArch Vulkan video driver
                                → statically linked libps5vk → PS5 display
XMB / RGUI ──────────────────────┘
PPSSPP (hardware core) → Vulkan through RetroArch's HW context → libps5vk
```

[PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan), which I also maintain,
is the separate GPU-driver project used here. RetroArch is already rendering
through it; completing the driver's Vulkan 1.0 coverage is not a prerequisite
for the working paths demonstrated by this application. Driver conformance and
feature coverage remain that project's own milestones.

The driver is **linked into the title**. Updating a driver checkout or replacing
a loose `libvulkan.so.1` does not update the linked code: rebuild and redeploy the
RetroArch title against the intended driver artifacts.

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
threads to finish before unmapping the core. See the
[runtime contract](docs/REFERENCE.md#native-in-process-core-loader).

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
- ✅ 120 Hz output where the display offers it, with a 60 Hz fallback.
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
| ❌ | MAME — expand arcade coverage beyond FBNeo |
| ❌ | Beetle PSX HW — PlayStation, targeting the Vulkan renderer |
| ❌ | Nintendo 64 — evaluate Mupen64Plus-Next / ParaLLEl-N64 with ParaLLEl-RDP |
| ✅ | PPSSPP — PSP, Vulkan rendering and JIT; tested games only |
| ❌ | PPSSPP MSAA — needs render pass 2 and depth/stencil resolve in PS5_Vulkan |
| 🚧 | Dolphin — GameCube: Wind Waker boots and plays with correct 3D and HUD; speed, long play and the enhancement profiles are not yet measured |

Future entries are development targets, not a promised release order. Hardware
rendering introduces new Vulkan requirements beyond presenting software frames;
PPSSPP is the first core that exercises them.

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
└── PS5_Vulkan/       # Driver archives, dependencies and matching source tree
```

The title build consumes that project's driver, Vulkan runtime and shader-compiler
archives; it does not build the driver for you. `PS5_VULKAN_DIR` can select an
alternative checkout for the build; some host tests currently require the sibling
layout above. Driver dependencies and setup requirements are documented in the
driver repository.

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
frontend and all five cores, and stages the native title in `dist/PPSA99169/`.
The initial dependency/source fetch requires network access.

For an already configured checkout:

```bash
bash tools/build-title.sh     # Build/stage the frontend and all shipped cores
make genesis-plus-gx         # Build and ABI-check one core only
# Other core targets: fceumm, mgba, snes9x, fbneo
```

When adding or updating a core, rebuild the title too: the frontend's native
import table and build identity depend on the shipped core binaries. Source
patches live in `patches/`; fetched and generated trees stay in ignored
`vendor/`, `.deps/`, `build/` and `dist/` directories.

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
| Live configuration | `config/retroarch.cfg` | `/app0/config/retroarch.cfg` |
| Save RAM | `savefiles/` | `/app0/savefiles/` |
| Save states | `savestates/` | `/app0/savestates/` |

For FBNeo, use `system/fbneo/` for its system files. Genesis Plus GX's Sega CD BIOS
filenames belong in the configured `system/` root, as listed by its metadata.

The application creates its managed writable folders and seeds live settings
only when no live configuration exists. Ordinary scripted updates preserve user
content and saved settings. Existing settings can therefore keep RGUI selected
even though XMB is the packaged default. Back up user files before any clean
removal; `--clean` removes the entire title folder.

See [deployment](docs/DEPLOYMENT.md) for FTP setup, verified readback and test runs.
Console details belong in the ignored `.env`, based on `.env.example`.

## Testing and troubleshooting

A successful build proves neither gameplay nor correct rendering. Core acceptance
includes native loading, gameplay, colour checks, audio/input, Quick Menu →
Close Content, and loading another game. I record my own visual confirmation on
the console alongside the logs; a camera can miss refresh-synchronous flicker.

The current gameplay milestone passed all five host gates with 66 Python tests;
23 recorded captures replay successfully. Exact results and limitations are in
[ACTIVE](docs/ACTIVE.md), rather than implied by a core's upstream feature list.

For reports, include the core, game-file format, relevant settings, reproduction
steps and whether the application was closed manually. Preserve `retroarch.log`
and `trace.txt` before reopening: the frontend log is replaced on a new launch.
The test tools retain kernel captures in ignored `klog/`. Redact private paths,
console addresses and credentials before sharing logs.

Old `gpu-buffers-*.bin`, `gpu-stages-*.bin` and `gpu-tables-*.bin` files are temporary
rendering diagnostics from earlier investigations. They are not required runtime
assets and can be removed. Keep the normal development logs when reporting bugs.

## Documentation

| Document | Purpose |
| --- | --- |
| [Active state](docs/ACTIVE.md) | Current verified build, known errors and acceptance limits |
| [Reference](docs/REFERENCE.md) | Native loader, core contracts, source pins and platform details |
| [Testing](docs/TESTING.md) | Host gates and console acceptance procedures |
| [Deployment](docs/DEPLOYMENT.md) | Build staging, FTP locations, updates and capture workflow |
| [Troubleshooting](docs/TROUBLESHOOTING.md) | Recorded symptoms, causes and fixes |
| [Findings](docs/FINDINGS.md) | Technical observations and evidence behind decisions |
| [Phase log](docs/PHASE_LOG.md) | Dated development and console-test history |
| [Evidence](evidence/) | Machine-readable captures and expected results |
| [Contributor/agent instructions](AGENTS.md) | Scope, verification, attribution and commit rules |

## Authors and acknowledgements

This port builds on substantial upstream and PS5 homebrew work. Credits below
identify project authors and teams; their repositories retain the full contributor
lists and original notices.

### Frontend, platform and graphics

| Project / author | Contribution |
| --- | --- |
| [Mihawk](https://github.com/mihawk-99) — [PS5_RetroArch](https://github.com/mihawk-99/PS5_RetroArch), [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) | This native RetroArch port, core integration and the separate PS5 Vulkan implementation |
| [RetroArch / libretro contributors](https://github.com/libretro/RetroArch) | Frontend, libretro API, menus, video pipeline and shared libraries |
| [BlackBearReloaded — ProsperoLight](https://github.com/blackbearreloaded/ProsperoLight) | Project starting point and reference for native PS5 input and audio integration |
| [BlackBearReloaded — PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) | Underlying native title tooling, ELF/FSELF conversion and runtime-shim foundation |
| [John Törnblom and ps5-payload-dev contributors](https://github.com/ps5-payload-dev/sdk) | Public PS5 Payload SDK, toolchain and API stubs; [PacBrew](https://github.com/ps5-payload-dev/pacbrew-repo) ports infrastructure |
| [John Törnblom / ps5-payload-dev — websrv](https://github.com/ps5-payload-dev/websrv) | Reference for per-core fetch/build/stage scripts; this port uses a separate native title pipeline |
| [BlackBearReloaded — ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl) | Shader-compiler and graphics foundations consumed by PS5_Vulkan; not the active RetroArch video backend |
| [Mesa contributors](https://gitlab.freedesktop.org/mesa/mesa) | Vulkan runtime, NIR/ACO and utility foundations used through the graphics stack |
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
| [libretro core-info](https://github.com/libretro/libretro-core-info) | Metadata maintainers and contributors |

## License and third-party terms

Port-authored code uses **GPL-3.0-or-later**, with copyright and SPDX notices in
the source files. RetroArch and each dependency retain their own license and
copyright notices. The locally generated runtime shim is described in
[runtime/README.md](runtime/README.md).

Emulator cores are not all GPL-3.0: FCEUmm uses GPLv2, mGBA uses MPL-2.0, and
Snes9x, FBNeo and Genesis Plus GX include non-commercial terms. Consult each
linked upstream repository for the complete applicable terms. Asset and font
licenses are retained with their files.

This is an independent homebrew project, not affiliated with or endorsed by Sony
Interactive Entertainment. PlayStation and PS5 are Sony trademarks.
