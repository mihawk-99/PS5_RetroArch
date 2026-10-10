<div align="center">

# RetroArch for PS5

**Your games on the TV. Your library in your hands.**

33 native cores · RetroArch & EmulationStation · A local WebUI

[Download](https://github.com/mihawk-99/PS5_RetroArch/releases) · [Get started](#get-started) · [Supported systems](#supported-systems) · [What’s new](releases/v1.0.0-beta.1.md)

</div>

![Important warning](assets/readme/important-warning.png)

## PS5 RetroArch is a passion project, not a piracy project.

It exists so you can play the games you own on the console you own.

- **Piracy is not condoned.**
- **No games, BIOS files, console firmware or decryption keys are included, and they never will be.**
- **Use only legally obtained backups of games you own**, made yourself from your own discs, cartridges or digital purchases.
- **Use only BIOS and firmware files dumped from hardware you own.**
- **Requests for, or links to, games, BIOS files, firmware or keys are not welcome** in this project's issues or discussions.

---

<a id="galaxy-banner"></a>

<img src="assets/readme/galaxy-banner.svg" width="960" alt="RetroArch for PlayStation 5 — a violet starfield and magenta horizon grid">

<a href="assets/releases/v1.0.0-beta.1/webui-games-neogeo-b.jpg"><img src="assets/readme/galaxy-games.svg" width="960" alt="WebUI Games library with SNK Neo Geo selected and the B filter active, in a galaxy frame"></a>

<p align="center"><strong>Your collection, beautifully organized.</strong><br>Browse, add games, and manage artwork from your phone or computer.</p>

A native RetroArch homebrew title for jailbroken PlayStation 5 consoles, made by [Mihawk](https://github.com/mihawk-99). Play through RetroArch or EmulationStation, with Vulkan graphics and up to four controllers.

**Latest release: [v1.0.0-beta.1](https://github.com/mihawk-99/PS5_RetroArch/releases/tag/v1.0.0-beta.1).** Read [what’s new](releases/v1.0.0-beta.1.md) or [download the package](https://github.com/mihawk-99/PS5_RetroArch/releases/latest).

> [!WARNING]
> **Designed around ShadowMountPlus 1.7beta4 or later.** Use [ShadowMountPlus 1.7beta4](https://github.com/drakmor/ShadowMountPlus/releases/tag/1.7beta4) or newer for supported USB and extended-storage access.

<a id="choose-your-frontend"></a>

## <img src="assets/readme/starfield-section.svg" width="960" alt=""><br><img src="webui/assets/fluent/game-chat.svg" width="26" height="26" alt=""> Choose your frontend

**RetroArch or EmulationStation.** Pick your preferred way to browse and play.

<a href="assets/readme/pre-screen.png"><img src="assets/readme/galaxy-frontends.svg" width="960" alt="Frontend picker offering RetroArch and EmulationStation, in a galaxy frame"></a>

Press **Square** to remember your choice. Hold **L1** during startup to return to the picker, or change it in **WebUI Settings → Start with**.

<a id="highlights"></a>

## <img src="webui/assets/fluent/board.svg" width="26" height="26" alt=""> At a glance

| | Made for your collection |
| --- | --- |
| <img src="webui/assets/fluent/game-chat.svg" width="36" height="36" alt=""> | **33 cores.** From NES and Mega Drive to Dreamcast, GameCube, PSP, and more. |
| <img src="webui/assets/fluent/library.svg" width="36" height="36" alt=""> | **One shared library.** Artwork and game details across RetroArch, EmulationStation, and the WebUI. |
| <img src="webui/assets/fluent/arrow-square-down.svg" width="36" height="36" alt=""> | **Easy game setup.** Add a game, find its details, and download missing artwork. |
| <img src="webui/assets/fluent/edit.svg" width="36" height="36" alt=""> | **DualSense stylus.** Use the touchpad for Nintendo DS and 3DS touchscreen controls. |

**Explore:** [Get started](#get-started) · [WebUI](#webui) · [Systems](#supported-systems) · [Controls](#controls) · [Graphics](#graphics) · [Your files](#your-files) · [Known limits](#what-has-been-verified) · [Build](#build-from-source)

<a id="get-started"></a>

## <img src="webui/assets/fluent/arrow-square-down.svg" width="26" height="26" alt=""> Get started

1. **Install the complete title folder.** Extract a release and copy `PPSA99169` to your homebrew launcher’s location, usually `/data/homebrew/PPSA99169/`. A compatible jailbreak and launcher must already be set up.
2. **Choose your frontend.** Start RetroArch or EmulationStation from the picker. Press **Square** to remember your choice; hold **L1** during startup to return to the picker. XMB and RGUI are available in RetroArch.
3. **Open the WebUI.** Scan its QR code or visit `http://<PS5-IP>:6769` from a device on the same network.
4. **Add your games.** Use **Games → + Add Game** for one game backup, or **Content** for folders and multi-file games. Install any required BIOS files in the [system folders](#your-files), then load your game on the console.

No games, BIOS files, firmware or decryption keys are included. Use your own
legally obtained backups and system files dumped from hardware you own. Piracy
is not condoned; requests for these files are not welcome in issues or discussions.

### Scan to connect

| Menu | Open the WebUI QR code |
| --- | --- |
| **Picker** | Press **Triangle**; Triangle again or Circle closes it |
| **RetroArch XMB** | **WebUI** tab to the left of Settings |
| **RetroArch RGUI** | **WebUI** at the top of the main menu |
| **EmulationStation** | **WebUI** in the main menu |

<a id="webui"></a>

## <img src="assets/readme/starfield-section.svg" width="960" alt=""><br><img src="webui/assets/fluent/laptop.svg" width="26" height="26" alt=""> Your browser companion

Stay connected while switching between the picker, RetroArch, and EmulationStation. A supported ELF loader or homebrew launcher keeps the WebUI running across menus; without one, it is available while RetroArch runs.

| Page | What you can do |
| --- | --- |
| <img src="webui/assets/fluent/home.svg" width="24" height="24" alt=""> **Overview** | Check updates, alerts, recent transfers, and quick settings. |
| <img src="webui/assets/fluent/library.svg" width="24" height="24" alt=""> **Games** | Browse cover art, search, filter by system or letter, and sort your collection. |
| <img src="webui/assets/fluent/arrow-square-down.svg" width="24" height="24" alt=""> **Download Media** | Download artwork and game details for a few games or your whole library. |
| <img src="webui/assets/fluent/document-folder.svg" width="24" height="24" alt=""> **Content** | Browse folders, create subfolders, and upload or download files. |
| <img src="webui/assets/fluent/arrow-sync.svg" width="24" height="24" alt=""> **Transfers** | Follow uploads and their results. Downloads appear in your browser’s download manager. |
| <img src="webui/assets/fluent/settings.svg" width="24" height="24" alt=""> **Settings** | Adjust global preferences, core options, controller mappings, and the startup frontend. |

> The WebUI is a local HTTP service without a login. Use a trusted network and
> do not forward port **6769** to the internet.

<a id="supported-systems"></a>

## <img src="assets/readme/starfield-section.svg" width="960" alt=""><br><img src="webui/assets/fluent/game-chat.svg" width="26" height="26" alt=""> Supported systems

**33 native cores**, grouped below by family. Compatibility and performance vary by game.

**Vulkan (GPU)** cores render on the GPU. **Software** cores render on the CPU, with Vulkan displaying the result.

| Family | Systems | Core | Renderer |
| --- | --- | --- | --- |
| **Nintendo** | <img src="webui/assets/systems/nes.webp" width="40" height="40" alt=""> **NES / Famicom** | FCEUmm | Software |
|  | <img src="webui/assets/systems/gb.webp" width="40" height="40" alt=""> **Game Boy**<br><img src="webui/assets/systems/gbc.webp" width="40" height="40" alt=""> **Game Boy Color**<br><img src="webui/assets/systems/gba.webp" width="40" height="40" alt=""> **Game Boy Advance** | mGBA | Software |
|  | <img src="webui/assets/systems/snes.webp" width="40" height="40" alt=""> **SNES / Super Famicom** | Snes9x | Software |
|  | <img src="webui/assets/systems/n64.webp" width="40" height="40" alt=""> **Nintendo 64** | Mupen64Plus-Next | Vulkan (GPU) |
|  | <img src="webui/assets/systems/gc.webp" width="40" height="40" alt=""> **GameCube**<br><img src="webui/assets/systems/wii.webp" width="40" height="40" alt=""> **Wii** | Dolphin | Vulkan (GPU) |
|  | <img src="webui/assets/systems/nds.webp" width="40" height="40" alt=""> **Nintendo DS** | DeSmuME | Software |
|  | <img src="webui/assets/systems/n3ds.webp" width="40" height="40" alt=""> **Nintendo 3DS** | Azahar | Vulkan (GPU) |
|  | <img src="webui/assets/systems/virtualboy.webp" width="40" height="40" alt=""> **Virtual Boy** | Beetle VB | Software |
|  | <img src="webui/assets/systems/pokemini.webp" width="40" height="40" alt=""> **Pokémon Mini** | PokeMini | Software |
| **Sony** | <img src="webui/assets/systems/psx.webp" width="40" height="40" alt=""> **PlayStation** | Beetle PSX HW | Vulkan (GPU) |
|  | <img src="webui/assets/systems/ps2.webp" width="40" height="40" alt=""> **PlayStation 2** | LRPS2 | Vulkan (GPU) |
|  | <img src="webui/assets/systems/psp.webp" width="40" height="40" alt=""> **PlayStation Portable** | PPSSPP | Vulkan (GPU) |
| **Sega** | <img src="webui/assets/systems/genesis.webp" width="40" height="40" alt=""> **Mega Drive / Genesis**<br><img src="webui/assets/systems/mastersystem.webp" width="40" height="40" alt=""> **Master System**<br><img src="webui/assets/systems/gamegear.webp" width="40" height="40" alt=""> **Game Gear**<br><img src="webui/assets/systems/sg-1000.webp" width="40" height="40" alt=""> **SG-1000**<br><img src="webui/assets/systems/segacd.webp" width="40" height="40" alt=""> **Sega CD** | Genesis Plus GX | Software |
|  | <img src="webui/assets/systems/sega32x.webp" width="40" height="40" alt=""> **Sega 32X** | PicoDrive | Software |
|  | <img src="webui/assets/systems/saturn.webp" width="40" height="40" alt=""> **Sega Saturn** | Beetle Saturn | Software |
|  | <img src="webui/assets/systems/dreamcast.webp" width="40" height="40" alt=""> **Dreamcast**<br><img src="webui/assets/systems/naomi.webp" width="40" height="40" alt=""> **NAOMI / NAOMI 2**<br><img src="webui/assets/systems/atomiswave.webp" width="40" height="40" alt=""> **Atomiswave** | Flycast | Vulkan (GPU) |
| **Arcade & SNK** | <img src="webui/assets/systems/fbneo.webp" width="40" height="40" alt=""> **Arcade**<br><img src="webui/assets/systems/neogeo.webp" width="40" height="40" alt=""> **Neo Geo** | FinalBurn Neo | Software |
|  | <img src="webui/assets/systems/mame.webp" width="40" height="40" alt=""> **Arcade** | MAME | Software |
|  | <img src="webui/assets/systems/ngpc.webp" width="40" height="40" alt=""> **Neo Geo Pocket / Color** | Beetle NeoPop | Software |
|  | <img src="webui/assets/systems/neogeocd.webp" width="40" height="40" alt=""> **Neo Geo CD** | NeoCD | Software |
| **NEC** | <img src="webui/assets/systems/pcengine.webp" width="40" height="40" alt=""> **PC Engine / TurboGrafx-16 / CD**<br><img src="webui/assets/systems/supergrafx.webp" width="40" height="40" alt=""> **SuperGrafx** | Beetle PCE | Software |
|  | <img src="webui/assets/systems/pcfx.webp" width="40" height="40" alt=""> **PC-FX** | Beetle PC-FX | Software |
| **Atari** | <img src="webui/assets/systems/atari2600.webp" width="40" height="40" alt=""> **Atari 2600** | Stella | Software |
|  | <img src="webui/assets/systems/atari5200.webp" width="40" height="40" alt=""> **Atari 5200** | a5200 | Software |
|  | <img src="webui/assets/systems/atari7800.webp" width="40" height="40" alt=""> **Atari 7800** | ProSystem | Software |
|  | <img src="webui/assets/systems/atarilynx.webp" width="40" height="40" alt=""> **Atari Lynx** | Handy | Software |
|  | <img src="webui/assets/systems/atarijaguar.webp" width="40" height="40" alt=""> **Atari Jaguar** | Virtual Jaguar | Software |
| **Computers & adventures** | <img src="webui/assets/systems/c64.webp" width="40" height="40" alt=""> **Commodore 64** | VICE x64sc | Software |
|  | <img src="webui/assets/systems/amiga.webp" width="40" height="40" alt=""> **Commodore Amiga** | PUAE | Software |
|  | <img src="webui/assets/systems/dos.webp" width="40" height="40" alt=""> **MS-DOS** | DOSBox Pure | Software |
|  | <img src="webui/assets/systems/scummvm.webp" width="40" height="40" alt=""> **Point-and-click adventures** | ScummVM | Software |
| **More consoles** | <img src="webui/assets/systems/3do.webp" width="40" height="40" alt=""> **3DO** | Opera | Software |
|  | <img src="webui/assets/systems/wonderswancolor.webp" width="40" height="40" alt=""> **WonderSwan / Color** | Beetle Cygne | Software |

PicoDrive also supports Mega Drive, Mega-CD, Master System, and Pico. FinalBurn Neo includes Sega System 16/32; ScummVM supports many LucasArts, Sierra, and other adventures.

Use matching arcade sets: MAME targets **0.289**, and FBNeo needs sets compatible with its version. Azahar requires decrypted content. Only native cores built for this port are compatible; desktop cores and other PS5 distributions’ cores are not interchangeable. No games or BIOS files are bundled.

None of the games I tested with is provided.
**Use only legally obtained backups of games you own**, and BIOS files dumped
from your own hardware: piracy is not condoned.

<a id="controls"></a>

## <img src="webui/assets/fluent/edit.svg" width="26" height="26" alt=""> A stylus in your DualSense

In **DeSmuME** and **Azahar**, slide a finger on the touchpad to move the stylus. **Press** to touch the screen; hold the press while sliding to drag. Release to lift the stylus. A light tap does not click.

Up to **four controllers** are supported. Save controller mappings for each game or core.

<a id="graphics"></a>

## <img src="webui/assets/fluent/image.svg" width="26" height="26" alt=""> Graphics for your display

RetroArch presents at the display resolution. Internal rendering is chosen to
leave room for emulation and memory use; it need not reach native 4K. Saved
core, folder and game options take priority over these compiled defaults.

| Core | Default internal resolution | Anti-aliasing |
| --- | --- | --- |
| PPSSPP | 6× · 2880 × 1632 | 8× MSAA |
| Dolphin | 4× | No MSAA |
| LRPS2 | 4× · approximately 1440p | Core default |
| Beetle PSX HW | 8× | MSAA off |
| Mupen64Plus-Next | 4× | Core default |
| Azahar | 6× · 2400 × 1440 top screen | Core default |
| DeSmuME | 4× · 1024 × 768 per screen | Core default |
| Flycast | 4.5× · 2880 × 2160 | Per-pixel transparency |

Other cores retain native rendering, scaled for the display. MAME uses a 4K
target for vector output; that path still needs console acceptance.

Higher resolutions and anti-aliasing use more memory. If a game struggles, lower its internal resolution or disable MSAA.

To adopt these defaults on an existing installation, change the listed options
in the WebUI or **Quick Menu → Core Options**. Reset Core Options resets every
option for that core, so use individual controls if you want to keep other choices.

<a id="shaders-filters-and-bezels"></a>

## <img src="webui/assets/fluent/image.svg" width="26" height="26" alt=""> Shaders, filters and bezels

Release builds bundle offline Slang shaders, Mega Bezel, koko-aio and
standard overlays. Load GPU presets through **Quick Menu → Shaders**, choose a
CPU filter in **Settings → Video**, or choose artwork in **Settings → On-Screen
Display → On-Screen Overlay**. CPU filters apply to software-rendered cores;
hardware-rendered cores use GPU shaders.

Presets and overlays are included for offline use. Compatibility varies by core and preset; original notices are retained.

<a id="your-files"></a>

## <img src="assets/readme/starfield-section.svg" width="960" alt=""><br><img src="webui/assets/fluent/document-folder.svg" width="26" height="26" alt=""> Your files

`/app0` is the running title’s mount. Over FTP, use your installed title folder,
usually `/data/homebrew/PPSA99169/`.

| Folder or file | Purpose |
| --- | --- |
| `content/` | Your content; available to WebUI uploads and downloads |
| `content/Saturn/` | Saturn disc images; created automatically |
| `system/` | General BIOS and system data |
| `system/Saturn/` | Saturn BIOS files; created automatically |
| `system/pcsx2/bios/` | PlayStation 2 BIOS (your own dump) |
| `system/fbneo/` | FinalBurn Neo system files |
| `cores/` and `info/` | Native cores and their metadata |
| `config/retroarch.cfg` | Live RetroArch configuration |
| `config/webui.cfg` | Saved global WebUI preferences |
| `config/<core>/` | Core options and RetroArch overrides, per core and per game |
| `config/remaps/<core>/` | Controller remaps, per core and per game |
| `library/<system>/` | Scraped box art, screenshots and details, shared by every frontend |
| `savefiles/` and `savestates/` | Save RAM and save states |
| `shaders/shaders_slang/` | GPU presets, including Mega Bezel and koko-aio |
| `filters/` | Built-in CPU filter configurations |
| `overlays/` | Standard overlay artwork and configurations |
| `radv-shader-cache/` | Reusable compiled graphics pipelines |

**Saturn setup:** place your own BIOS dump, `mpr-17933.bin` (US/Europe) or
`sega_101.bin` (Japan), in `system/Saturn/`. Keep a disc’s `.cue` and all referenced tracks together,
then load the `.cue`. The default system path is routed to the Saturn folder;
an explicitly selected custom system directory remains unchanged.

Genesis Plus GX’s Sega CD BIOS files and Beetle PSX HW’s optional BIOS files
belong in the general system folder, with the filenames listed by their metadata.
BIOS uploads use FTP; the WebUI only exposes `content/`.

Everything you put in `content/` and `system/` must be **your own legally
obtained backups**: games you own, and BIOS or firmware dumped from hardware you
own. Piracy is not condoned.

Ordinary scripted updates preserve content and saved settings. Back up your
files before replacing or removing a title. The file browser opens on **INTERNAL**,
the title folder. With [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus)
1.7beta4 or later running, it also lists each USB drive (**USB 0** to **USB 7**,
`/mnt/usb0` onward) and extended storage (**EXTENDED 0** and **1**, `/mnt/ext0` and
`/mnt/ext1`), only when a drive is mounted there. Without it, the sandbox hides them
and **EXTERNAL** (`/mnt`) may be empty.

<a id="what-has-been-verified"></a>

## <img src="webui/assets/fluent/checkmark-circle.svg" width="26" height="26" alt=""> Compatibility and known limits

Startup, controller input, audio, saved settings, and representative games have been tested on PS5. Compatibility and long-session testing are ongoing.

- **Azahar:** a crash when closing content, switching to Azahar, and starting a game remains under investigation.
- **RetroAchievements and NetPlay:** RetroArch starts its bundled Lapy service automatically through the console's local ELF loader, then reuses it on later launches. No Payload Manager autoload entry is needed. Files stay in their installation folder; existing `/app0` settings continue to work. Without the local loader or verified elevation, outgoing online features remain unavailable. See [service setup](platform/lapy/README.txt) and [online verification](evidence/online-features/README.txt).
- Shader presets, disc swapping, and rumble still need broader testing.

[Recorded evidence](evidence/) accompanies verified changes. For a bug report,
include the core, file format, settings and reproduction steps. Preserve
`retroarch.log` and `trace.txt` **before reopening RetroArch**: the frontend log
is replaced on each launch. Remove private paths, addresses and credentials
before sharing captures.

---

<a id="build-from-source"></a>

## <img src="webui/assets/fluent/settings.svg" width="26" height="26" alt=""> Build from source

<details>
<summary><strong>Build requirements and commands</strong></summary>

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
[PS5_Dynarmic](https://github.com/mihawk-99/PS5_Dynarmic),
[PS5_Flycast](https://github.com/mihawk-99/PS5_Flycast),
[PS5_PicoDrive](https://github.com/mihawk-99/PS5_PicoDrive),
[PS5_BeetlePCE](https://github.com/mihawk-99/PS5_BeetlePCE),
[PS5_BeetlePCFX](https://github.com/mihawk-99/PS5_BeetlePCFX),
[PS5_BeetleVB](https://github.com/mihawk-99/PS5_BeetleVB),
[PS5_BeetleNGP](https://github.com/mihawk-99/PS5_BeetleNGP),
[PS5_BeetleWSwan](https://github.com/mihawk-99/PS5_BeetleWSwan),
[PS5_PokeMini](https://github.com/mihawk-99/PS5_PokeMini),
[PS5_Handy](https://github.com/mihawk-99/PS5_Handy),
[PS5_VirtualJaguar](https://github.com/mihawk-99/PS5_VirtualJaguar),
[PS5_Stella](https://github.com/mihawk-99/PS5_Stella),
[PS5_A5200](https://github.com/mihawk-99/PS5_A5200),
[PS5_ProSystem](https://github.com/mihawk-99/PS5_ProSystem),
[PS5_DOSBoxPure](https://github.com/mihawk-99/PS5_DOSBoxPure),
[PS5_Opera](https://github.com/mihawk-99/PS5_Opera),
[PS5_PUAE](https://github.com/mihawk-99/PS5_PUAE),
[PS5_NeoCD](https://github.com/mihawk-99/PS5_NeoCD),
[PS5_ScummVM](https://github.com/mihawk-99/PS5_ScummVM)), each pinned by revision in
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
frontend and thirty-three cores, and stages the native title in `dist/PPSA99169/`.
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
                             # build-flycast.sh, build-picodrive.sh,
                             # build-beetle-pce.sh, build-beetle-pcfx.sh,
                             # build-beetle-vb.sh, build-beetle-ngp.sh,
                             # build-beetle-wswan.sh, build-pokemini.sh,
                             # build-handy.sh, build-virtualjaguar.sh,
                             # build-stella.sh, build-a5200.sh, build-prosystem.sh,
                             # build-dosbox-pure.sh, build-opera.sh, build-puae.sh,
                             # build-neocd.sh, build-scummvm.sh
```

When adding or updating a core, rebuild the title too: the frontend's native
import table and build identity depend on the shipped core binaries. Source
patches live in `patches/`; fetched and generated trees stay in ignored
`vendor/`, `.deps/`, `build/` and `dist/` directories.

</details>

<details>
<summary><strong>Authors and acknowledgements</strong></summary>


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

### Frontends and interface artwork

EmulationStation uses [ES-DE](https://es-de.org/) and the Alekfull NX theme. Touchpad handling builds on PS5_Proton; the picker uses PS5_VulkanTemplate’s UI foundation.

Color icons: **Microsoft Fluent Color**. System artwork: **Linear ES-DE**. Region flags: **flag-icons**. Utility icons: **Lucide**. Full attribution and licences are in [artwork credits](webui/assets/ICON-CREDITS.txt).

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
| [Flycast](https://github.com/flyinghead/flycast) | flyinghead and contributors, building on reicast and nullDC; PS5 port begun by rpf16rj |
| [PicoDrive](https://github.com/libretro/picodrive) | notaz, fDave and the PicoDrive contributors |
| [Beetle PCE](https://github.com/libretro/beetle-pce-libretro), [Beetle PC-FX](https://github.com/libretro/beetle-pcfx-libretro), [Beetle VB](https://github.com/libretro/beetle-vb-libretro), [Beetle NeoPop](https://github.com/libretro/beetle-ngp-libretro), [Beetle Cygne](https://github.com/libretro/beetle-wswan-libretro) | The Mednafen authors and contributors; libretro Beetle maintainers |
| [PokeMini](https://github.com/libretro/PokeMini) | JustBurn and contributors |
| [Handy](https://github.com/libretro/libretro-handy) | K. Wilkins and contributors |
| [Virtual Jaguar](https://github.com/libretro/virtualjaguar-libretro) | James Hammons and the Virtual Jaguar contributors |
| [Stella](https://github.com/stella-emu/stella) | Bradford W. Mott, Stephen Anthony and the Stella Team |
| [a5200](https://github.com/libretro/a5200) | The Atari800 team and the a5200 contributors |
| [ProSystem](https://github.com/libretro/prosystem-libretro) | Greg Stanton and contributors |
| [DOSBox Pure](https://codeberg.org/schelling/dosbox-pure) | Bernhard Schelling, building on DOSBox by the DOSBox Team |
| [Opera](https://github.com/libretro/opera-libretro) | The FreeDO authors, the 4DO and Opera contributors |
| [PUAE](https://github.com/libretro/libretro-uae) | The UAE, WinUAE and PUAE authors and contributors |
| [NeoCD](https://github.com/libretro/neocd_libretro) | Laurent Cayrol and contributors |
| [ScummVM](https://github.com/scummvm/scummvm) | The ScummVM Team and contributors |
| [libretro core-info](https://github.com/libretro/libretro-core-info) | Metadata maintainers and contributors |


</details>

<a id="license-and-third-party-terms"></a>

## <img src="webui/assets/fluent/book-open.svg" width="26" height="26" alt=""> License and third-party terms

This repository's own code is **GPL-3.0-or-later** ([LICENSE](LICENSE)). Most source
files carry a copyright and SPDX notice; the ones that do not (for example
`src/memory_ps5.cpp` and the build scripts in `tools/`) are under the same licence.
Code inherited from BlackBearReloaded's ps5-native-app-boilerplate and ProsperoLight
is Copyright (C) 2026 BlackBearReloaded, GPL-3.0-or-later. The title's
`sce_module/libc.prx` is generated by this repository (`runtime/`).

Every built title folder carries `LEGAL.txt` (the legal notice above) and
`licenses/`: the licence texts each part requires, and `components.json`, which
ties every executable file to the source revision it was built from
([tooling/notices/components.json](tooling/notices/components.json)). The package records public source links and exact revisions for every component.
The release tag contains the build scripts and patches; `tools/source-bundle.py`
can assemble the corresponding source archives from a prepared build checkout.
Releases up to v0.5.0-alpha.5 were published without `licenses/`.

The cores keep their own licences, and they differ:

| Licence | Cores |
| --- | --- |
| GPL-2.0-or-later | FCEUmm, PPSSPP, Dolphin, Beetle PSX HW, Beetle Saturn, Mupen64Plus-Next (with MIT and LGPL parts), VICE, DeSmuME, Azahar (Dynarmic is 0BSD), Flycast, Beetle PCE, Beetle PC-FX, Beetle VB, Beetle NeoPop, Beetle Cygne, Stella, a5200, ProSystem, DOSBox Pure, PUAE, MAME (as a whole; many files BSD-3-Clause) |
| GPL-3.0-or-later | LRPS2 (PCSX2), PokeMini, Virtual Jaguar, ScummVM |
| LGPL-3.0-or-later | NeoCD |
| Zlib | Handy |
| MPL-2.0 | mGBA |
| Non-commercial licences | Snes9x, FinalBurn Neo, Genesis Plus GX, PicoDrive, Opera (FreeDO's modified LGPL): they may not be sold or used commercially, and FBNeo's forbids asking for donations for a project that uses its code |

The Alekfull NX theme also carries non-commercial restrictions. Artwork credits and licence texts for the WebUI’s icons, flags, and system images are in [ICON-CREDITS.txt](webui/assets/ICON-CREDITS.txt).

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

<p align="center">made by <a href="https://github.com/mihawk-99">Mihawk</a> · Built on the work of the RetroArch, libretro and PS5 homebrew communities.</p>
