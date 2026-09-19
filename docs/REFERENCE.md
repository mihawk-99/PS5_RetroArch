# PS5 RetroArch: reference

Stable reference. Edit when the specification changes. This file holds the step
ladder, the workflow that turns a step into a commit, and the environment facts
every session needs. It does not hold progress, run results or next actions:
those are `docs/ACTIVE.md` and `docs/PHASE_LOG.md`.

## M0: the contract

| Step | What | Acceptance |
| --- | --- | --- |
| M0.1 | Fill the documentation contract: this file's ladder, the gates in `docs/PLAN.md`, the environment table below, and the procedures in `docs/TESTING.md`, `docs/DEPLOYMENT.md` and `docs/TROUBLESHOOTING.md` | `grep -rno '{{[A-Z0-9_]*}}' AGENTS.md docs tools` returns nothing, and every command named in a gate exists as a file in `tools/` |
| M0.2 | Pin the toolchain and the upstream source: `tools/doctor.sh` proves the SDK and every host tool, `tools/fetch-retroarch.sh` fetches the pinned revision and checks the tree it got against that commit | `tools/doctor.sh` exits 0 and names each resolved path; `tools/fetch-retroarch.sh --check` prints the pin and what is present, and a second run is a no-op |
| M0.3 | Compile the frontend's own sources, to prove the toolchain end to end: `tools/retroarch-sources.sh` asks RetroArch's build which objects a link needs, `tools/build-retroarch.sh` compiles them | `tools/build-retroarch.sh` reports how many sources it compiled and archives them into `build/ra/libretroarch.a`; every source it could not compile is named in its report |
| M0.4 | Stage the title: `sce_sys/param.json`, the presentation assets, the runtime and the signed image in the layout the loader reads | `bash tools/build-title.sh` writes `dist/<TITLE_ID>/` with `eboot.bin`, `sce_sys/` and `sce_module/libc.prx`, and records `manifest.sha256`; `bash tools/check-manifest.sh` verifies it and `tools/verify.sh integration` passes on it |
| M0.5 | The gate runner: `tools/verify.sh` runs format, unit, build, integration, evidence in order and fails fast | `tools/verify.sh` exits 0 on a clean tree and exits 1 at the first red gate; `tools/verify.sh --list` prints the five commands |

## M1: the console shell

| Step | What | Acceptance |
| --- | --- | --- |
| M1.1 | A minimal PS5 payload that prints its identity through the SDK's stdio and exits | The ELF loads on the console and the captured `klog` holds the identity line; `evidence/m1.1/` carries the capture and the expected line |
| M1.2 | The platform driver that owns a title's lifetime: user service, splash screen, and an exit path that does not return from `main` | Console run shows the splash hidden by our call and a clean close from the home screen; the capture is committed |
| M1.3 | VideoOut bring-up behind a project-owned seam: acquire buffers, present a solid frame, release on shutdown | A frame is presented on the console and the run's capture names the resolution it received (`docs/DEPLOYMENT.md`, "Observing a target run") |
| M1.4 | RetroArch's frontend enters the platform driver and reaches its own main loop in a headless configuration | The console run logs the frontend's own version and driver identity, and the host build of the same step passes `tools/verify.sh` |

## M2: video and input

| Step | What | Acceptance |
| --- | --- | --- |
| M2.1 | Choose the backend with evidence: build against the installed OpenGL 3.3 Core package and list the entry points RetroArch's GL driver resolves against what the package exports | `tools/check-gl-exports.sh` prints the resolved and the missing lists; either the missing list is empty, or it is quoted in full in `docs/FINDINGS.md` and the step closes as a decision to wait for PS5_Vulkan's rung |
| M2.2 | A platform video driver and its matching display context: the console's display is opened, the swapchain or context is created from the backend's public entry points, and nothing else in this repository touches the GPU | The console run shows RetroArch's own driver identity line and the resolution it received; the capture is committed under `evidence/m2.2/` |
| M2.3 | The menu is drawn: the first visible interface, on whichever backend the driver provides | A console run presents a menu frame; the capture holds the driver name, the swapchain extent, the present count and a frame digest. A framebuffer-only driver cannot satisfy this — `docs/FINDINGS.md` records why the display context is what draws a menu |
| M2.4 | Controller input through the payload SDK's pad API into RetroArch's input driver | The capture records the buttons pressed and the menu's reaction, from the mapping table written in this file |
| M2.5 | A libretro core loads and runs one frame of content, headless first and then presented | The core's own identification line, the frame digest and the present count sit in one capture under `evidence/m2.5/` |

## M3: cores and storage

| Step | What | Acceptance |
| --- | --- | --- |
| M3.1 | Core discovery and installation: the scan path, the `.info` metadata and the load path | A console run lists the staged cores and loads one by name; the capture names the core and its version |
| M3.2 | Save and save-state paths on the console's own storage, with the write-through discipline `docs/DEPLOYMENT.md` describes | A state written on the console is read back after a relaunch, and both runs are captured |
| M3.3 | Configuration persistence: `retroarch.cfg` round-trips through the console's storage without losing keys | The diff between the seeded config and the config read back after a settings change names only the changed key |
| M3.4 | A second core of a different class (a non-dynarec, non-GL core) loads, runs and exits | Two core names appear in two captures, each with its own content digest |

## M4: audio, mapping and release

| Step | What | Acceptance |
| --- | --- | --- |
| M4.1 | Audio output through the payload SDK's audio API, with the buffer discipline documented in `docs/FINDINGS.md` | The console run reports the audio format it opened and the frames written; a captured run holds no underrun line |
| M4.2 | Input mapping: buttons, sticks, the menu combo and the PlayStation confirm/cancel convention | A capture records each mapped button's effect once, from a written mapping table in this file |
| M4.3 | The menus at 1080p: readable text, bounded frame time, and no layout that depends on a mouse | A console run at the target resolution with the frame-time record committed |
| M4.4 | Release: the ZIP, its `SHA256SUMS`, and one acceptance run end to end | The release artifact's digest, the run's capture and the expected lines are committed under `evidence/m4.4/` |

Acceptance is the contract. A step is done when its acceptance line is satisfied
word for word, and the evidence it names is committed. A step whose acceptance
was wrong is corrected in this file, in its own commit, before the code that
depends on it lands.

## The workflow

| Phase | What it means |
| --- | --- |
| Spike | Time-boxed, throwaway, never committed as product code. What it learned goes to `docs/FINDINGS.md`. |
| Step | One commit, one acceptance line, verified before it lands. |
| Milestone | Its steps are all done and its gate in `docs/PLAN.md` holds. |

### Turning a step into a commit

1. Restate the acceptance line, and name the artifact that will prove it.
2. Build the smallest thing that can satisfy it.
3. Run the gates in `AGENTS.md`; fix what fails.
4. Commit with the evidence: what was run, what it returned, what it proves.
5. Write the step up once in `docs/ACTIVE.md`; append a dated entry to
   `docs/PHASE_LOG.md`.

### Splitting and parking

- If a step needs two independent proofs, it is two steps. Split it in
  `docs/PLAN.md`/this file first, then build the first one.
- If a step cannot be verified in this environment (it needs the console, a
  loader the console does not have yet, or the Vulkan driver before its rung is
  reached), implement it and park it in `parked/` as a patch plus the
  verification plan. It is not merged and it is not "done".

## The environment

*Verified on this host with `tools/doctor.sh`. Every session otherwise
re-derives these, at full price.*

| Thing | Value |
| --- | --- |
| Host | CachyOS (Arch-based) Linux, x86-64, bash 5.3, Python 3.14 |
| Host compiler and formatter | clang / clang-format / clang-tidy 22.1.8, `llvm-ar`, `ccache` |
| PS5 SDK | `$PS5_PAYLOAD_SDK`, default `/home/mihawk/ps5-payload-sdk`, unpacked 2026-09-17; environment from `$PS5_PAYLOAD_SDK/toolchain/prospero.sh` |
| PS5 compiler | `prospero-clang` 22.1.8, target `x86_64-sie-ps5`; sysroot `$PS5_PAYLOAD_SDK/target` |
| Upstream source | RetroArch 1.22.2 tarball, fetched into the ignored `vendor/`, patched per `patches/series` |
| Build | `bash tools/build-title.sh` → `dist/<TITLE_ID>/eboot.bin`, signed and manifested; `bash tools/verify.sh build` runs the same command |
| Test | `tools/verify.sh unit` (host-native, GoogleTest) and `tools/verify.sh integration` (shell and Python against the staged tree) |
| Format and lint | `tools/verify.sh format` (`clang-format --dry-run --Werror`, `bash -n`, `python3 -m py_compile`, attribution and JSON checks) |
| Run locally | host-native frontend against the headless driver: `build/host/retroarch --features` and the unit suite; no console is touched |
| Target | A PS5 title folder: a signed fake-self `eboot.bin` (x86-64) with `sce_module/libc.prx` and `sce_sys/`, which is what the console's loader runs |
| Graphics on the target | the relocatable OpenGL 3.3 Core package from `../ps5-opengl-sdk-0.2.0` today, and the Vulkan driver from `../PS5_Vulkan` once it reaches rung 1.0 — see "The graphics backend" below; neither is rebuilt here |
| Vulkan headers for our build | `../ps5-opengl-sdk-0.2.0/third_party/Vulkan-Headers/include`, the vendored copy the local driver builds against; the payload SDK's sysroot ships none |
| Console tools | the resident control payload and `ps5_console.py` from `../PS5_Vulkan` (`launch`, `kill`, `klog` on port 3232); address and credentials come from the ignored `.env` |
| Source of truth for deps | `pin/sources.txt` (versions and SHA-256) and `patches/series` (the patch order) |

## Versions and pins

Anything pinned is pinned here, with the reason. Nothing is pinned in two
places, and nothing is upgraded without a line in `docs/PHASE_LOG.md`.

| Pin | Version | Why |
| --- | --- | --- |
| RetroArch source | 1.22.2, `sha256 245ef18c8fa8fbd9fbb5eb25cf43e17c6aace2f95c1ed99873cbd794012bb232` | The newest upstream tag that carries the Vulkan context driver and the frontend the port needs; the digest is the tarball's own, so a moved tag is caught |
| PS5 payload SDK | `$PS5_PAYLOAD_SDK`, unpacked 2026-09-17 | The only toolchain that produces PS5 payload ELFs; the version is recorded in `pin/sources.txt` so a build is reproducible from a checkout |
| Vulkan driver | from `../PS5_Vulkan` at the revision recorded in `pin/sources.txt` | The driver is this project's graphics backend; a revision that changes a driver entry point must be a deliberate, recorded bump |
| Graphics backend (current) | `../ps5-opengl-sdk-0.2.0`, release 0.2.0, consumed through its installed package | It is the only graphics stack with a recorded hardware acceptance run on this machine; the driver package is never rebuilt here |
| GoogleTest | host-only, fetched and digest-verified into `.deps/test/` | Unit tests run on the host; it is never linked into a PS5 artifact |

## The graphics backend

Two local projects provide graphics; this repository builds neither. Which one a
step uses is recorded in the step's acceptance line, and the choice is evidence
driven, not stylistic.

| Stage | Backend | How this repository consumes it | What it decides |
| --- | --- | --- | --- |
| Now | OpenGL 3.3 Core from `../ps5-opengl-sdk-0.2.0` | its relocatable package, through `ps5-opengl-core33.mk` or its `ps5-opengl-core33.pc`; headers are EGL, GL and KHR | RetroArch's OpenGL path, with the frontend rendering through our own platform context driver |
| After PS5_Vulkan rung 1.0 | the Vulkan driver from `../PS5_Vulkan` | its released static archive; Vulkan headers come from the SDK's vendored copy | RetroArch's `vulkan` context driver, which is the target backend for the shipped application |

Rules that hold either way:

- The application never opens a GPU device directly. It issues EGL/GL or Vulkan
  calls through RetroArch's own context driver, so the backend can be swapped
  without touching the frontend.
- Whichever backend is in use is named in the run's capture, and a step that
  changes backend is its own step with its own acceptance run.
- A backend that fails the step is a finding in `docs/FINDINGS.md` with the exact
  call that failed, not a reason to add private GPU access here.

The first M2 step settles the OpenGL question with evidence rather than
assumption: RetroArch's GL driver resolves its entry points through `glsym`, and
a backend that exports fewer functions than the driver asks for fails at load
time. `tools/check-gl-exports.sh` compares the backend's exported symbol list
with the driver's request list and reports the difference; if the difference is
not empty, that difference is the M2 finding and PS5_Vulkan's rung becomes the
blocking dependency for a presented frame.

## Shipping as a PPSA title

The console needs an `eboot.bin` in a title folder with `sce_sys/param.json`
carrying a `PPSA#####` id. This project produces one through
`../ps5-native-app-boilerplate-main`'s pipeline, which is the path already proven
on this console: ProsperoLight, built the same way, starts when placed.

| Piece | Where it comes from |
| --- | --- |
| the tooling that links and signs `eboot.bin` | `tooling/`, taken from the native pipeline and committed as part of this project |
| the loader-visible runtime module | `runtime/libc.prx`, checked against its own digest manifest |
| the title's identity and launcher assets | `sce_sys/`, written by the scaffold from `title/` |
| the application | RetroArch's sources, compiled by `tools/build-retroarch.sh`, linked with `src/` |

The pipeline's builder compiles `src/**/*.{c,cc,cpp}` with fixed flags
(`-std=c11`, `-std=c++20`, `-O2 -Wall -Wextra -ffunction-sections
-fdata-sections`) and links the result with its own CRT, C++ runtime, AGC link
stubs and version script. Two consequences shape this project:

- **RetroArch is reached by include path, not copied in.** Its tree is dozens of
  directories with its own configure step, so `tools/build-retroarch.sh` compiles
  it separately with the same compiler and the generated `config.h`, and `src/`
  holds only this project's code — the entry point and the video driver.
- **Extra link inputs arrive through `APP_STATIC_ARCHIVES`.** That is how a
  graphics backend is added when a step needs one; the Vulkan driver's archive is
  the intended first use.

## Reference material

Two directories hold material this project did not write, and they are different
kinds of thing:

| Path | Committed | Rule |
| --- | --- | --- |
| `reference/` | yes, with a `PROVENANCE.txt` naming the source, the revision and every file's digest | read-only. It is what a claim was measured against; a change to it is its own step, and the digests are re-recorded in that step |
| `vendor/` | no, ignored | build-time only. `tools/fetch-retroarch.sh` recreates it from the pin on every clean build, so nothing in it can be relied on to persist |

Neither is edited to get a step unblocked. A change we want in either one is a
patch in `patches/` or a documented decision, never a quiet edit.

## Style

- C and C++ follow the upstream RetroArch style inside `platform/` and the
  project's own `clang-format` configuration: 4-space indent, 80 columns, braces
  on the same line for control flow. Run `tools/verify.sh format` before staging.
- Shell scripts start with `#!/usr/bin/env bash` and `set -euo pipefail`, quote
  every expansion, and exit 2 on a usage error and 1 on a check failure — the
  convention the console tools in `../PS5_Vulkan` already use.
- Generated code, vendored dependencies and build output are never edited by
  hand and never reformatted: `vendor/`, `build/`, `dist/`, `.deps/`, `*.elf`,
  `*.so`, and everything under a git-ignored capture directory. `reference/` is
  committed but equally read-only: it is the baseline a measurement was taken
  against.

## The port

RetroArch on this console is a video driver of this repository's own, presented
through the display layer in `src/display.cpp`. Two menus are available and the
choice is a backend choice, not a preference:

| Menu | Display context | What it needs |
| --- | --- | --- |
| RGUI | none — `menu/drivers/rgui.c` references `dispctx` **zero** times | `poke->set_texture_frame`, `viewport_info`, `video_driver_supports_rgba` |
| XMB, Ozone, MaterialUI | required — XMB dereferences `dispctx` 72 times | a GPU backend: every entry in `gfx_display_ctx_drivers[]` is a GPU API and there is no software one |

So RGUI is the first target: it rasterises the menu into its own framebuffer and
hands that one texture to the video driver, which means it runs over VideoOut with
no GPU stack at all. XMB comes after, over `../PS5_Vulkan`
(284 `ps5vk_` symbols in `libps5vk.ps5.a`) once the frontend is already proven.

The mandatory surface is small, and the rest is optional:

| Interface | Mandatory | Notes |
| --- | --- | --- |
| `video_driver_t` | `init`, `suppress_screensaver`, `alive`, `frame`, `ident`, `poke_interface` | `suppress_screensaver` is called with no NULL check; the rest are NULL-guarded |
| `video_poke_interface_t` | `set_texture_frame` | RGUI pushes its menu framebuffer through it |
| `gfx_ctx_driver_t` | not needed at all | `video_driver_init_internal` never touches one |

Field order matters: the struct is initialised positionally, and `overlay_interface`
sits inside `#ifdef HAVE_OVERLAY` before `poke_interface`, so an initialiser has to
account for it.

## XMB menu assets and build selection

XMB uses RetroArch's existing Vulkan display driver; no new graphics backend or
runtime dependency is added. Configure enables XMB and retains RGUI; upstream's
default selection prefers XMB when Ozone/MaterialUI are disabled. The seed config
matches that selection. Configure arguments are fingerprinted before feature
flags are derived so an existing configured tree cannot silently retain RGUI-only
objects. `ASSETS_DIR` is `/app0`, since platform_unix appends `/assets` itself.
The title currently selects the null platform frontend, so that Unix initializer
does not execute: patch 0063 seeds `/app0/assets` directly in configuration
defaults and records the resolved XMB paths and load result at context reset.

`assets/xmb/source.json` pins the official libretro/retroarch-assets revision
73106363e14e34c08a5854b4cfbc29f184e3b783 and hashes the shipped subset: all 120
fixed menu icons named by RetroArch 1.22.2's xmb_texture_path, the monochrome M+ 1p
font, attribution and licenses. This pin makes the required menu artwork
reproducible without downloading assets at build/run time. Per-system playlist
icons are not shipped yet; standard default/content icons are included.

The XMB ribbon retains its two-float shader layout but uploads a zero-padded
16-byte uniform block (named patch 0060), matching libps5vk's current whole-record
restriction. This is frontend compatibility padding, not general support for
arbitrary UBO ranges in the driver. Named patch 0061 expands each triangle strip
into list vertices with alternating winding and submits the expanded count for
both effect and icon draws. `tests/test_xmb_assets.py` executes the C index mapping
for strips through the full 8,064-vertex ribbon and checks bounds and winding.

Named patch 0062 also binds the existing white texture and nearest sampler for
untextured menu effects. The ribbon shader does not sample them, but libps5vk
currently validates all stage-visible descriptor layout entries rather than just
statically used shader bindings. This uses the same descriptor-fill compatibility
path as textured draws (including patch 0026's second sampler slot).

Named patch 0065 keeps texture coordinates consistent with the physical image
width required by the driver's row alignment. RGBA8 rows need 64-texel alignment;
R8 font rows need 256-texel alignment. Display UVs are scaled to the logical image
region and font atlas offsets are normalized by the physical width. Padding only
the image width changes what a normalized coordinate samples.

Named patch 0066 keeps static/menu textures at one mip level. XMB's full mipmapped
icons rendered as repeated/cropped fragments in the tested libps5vk path; the
single-level images render correctly. This is a compatibility limit, not proof
that the driver's general mip-chain storage/sampling is correct. RGUI and the
CPU video driver remain compiled and selectable. The retired screenshot recipe
is `parked/xmb-readback/README.md`; screenshots are not taken by normal builds.

## Native PS5 audio

`src/audio_ps5.cpp` adapts ProsperoLight's `sceAudioOutInit/Open/Output/Close`
sequence to RetroArch's `audio_driver_t`. Patch 0067 registers `audio_ps5` and
selects `ps5` by default; the staged config agrees. The backend opens the system
user's main output at 48,000 Hz, signed 16-bit interleaved stereo, 256 frames per
native output call. `new_rate` tells RetroArch to resample other source rates.
Only the default device is supported. No SDL or Opus dependency is introduced.

A dedicated worker feeds AudioOut, whose synchronous output call paces playback.
The producer and consumer share a bounded ring guarded by a mutex/condition;
no lock is held while calling AudioOut. Requested latency sizes the ring in
256-frame multiples, with a 512-frame minimum and 8,192-frame maximum. A zero
latency request uses 64 ms (3,072 frames). An additional native block can be in
flight. `write`, `write_avail` and `buffer_size` use bytes, matching RetroArch's
call sites. Blocking writes wait for space; nonblocking writes return the bytes
accepted, including zero when full. Empty or partial blocks are zero-filled.

Pause drops queued samples, waits for the in-flight block, then drains the native
port. Resume uses the same worker. Free wakes and joins the worker before draining
and closing the port. Output errors make the driver inactive and wake blocked
writers. Startup, failures and close summaries are logged; successful blocks are
not individually logged. Silence includes normal idle/menu output and padding,
so it is not by itself evidence of a streaming underrun.

The opt-in backend test is documented in `docs/TESTING.md`. Core integration,
long-duration A/V synchronization and streaming underrun behavior need their own
content-based acceptance runs; native test tones do not prove those properties.
