# PS5 RetroArch: reference

Stable reference. Edit when the specification changes. This file holds the step
ladder, the workflow that turns a step into a commit, and the environment facts
every session needs. It does not hold progress, run results or next actions:
those are `docs/ACTIVE.md` and `docs/PHASE_LOG.md`.

## M0: the contract

| Step | What | Acceptance |
| --- | --- | --- |
| M0.1 | Fill the documentation contract: this file's ladder, the gates in `docs/PLAN.md`, the environment table below, and the procedures in `docs/TESTING.md`, `docs/DEPLOYMENT.md` and `docs/TROUBLESHOOTING.md` | `grep -rno '{{[A-Z0-9_]*}}' AGENTS.md docs tools` returns nothing, and every command named in a gate exists as a file in `tools/` |
| M0.2 | Pin the toolchain and the upstream source: `tools/doctor.sh` proves the SDK and every host tool, `tools/fetch-upstream.sh` fetches the pinned tarball, verifies its digest and applies `patches/series` into `vendor/retroarch` | `tools/doctor.sh` exits 0 and names each resolved path; `tools/fetch-upstream.sh` prints `main.c`-bearing tree at the pinned version and a second run is a no-op |
| M0.3 | Cross-compile `libretro-common` alone to prove the toolchain end to end | `tools/verify.sh build` produces `build/ps5/libretro_common.a`, and `tools/check-ps5-object.sh` finds `libkernel_web.sprx` and no `libkernel_sys.sprx` in its import table |
| M0.4 | Stage the title skeleton: `sce_sys/param.json`, `sce_sys/icon0.png`, `retroarch.cfg` seed and the directory layout the loader reads | `tools/stage.sh` writes `dist/<TITLE_ID>/` with all four; the manifest is committed and `tools/verify.sh integration` passes on it |
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
| Build | `make` → `tools/verify.sh build` → `build/ps5/retroarch.elf`, then `tools/stage.sh` → `dist/<TITLE_ID>/` |
| Test | `tools/verify.sh unit` (host-native, GoogleTest) and `tools/verify.sh integration` (shell and Python against the staged tree) |
| Format and lint | `tools/verify.sh format` (`clang-format --dry-run --Werror`, `bash -n`, `python3 -m py_compile`, attribution and JSON checks) |
| Run locally | host-native frontend against the headless driver: `build/host/retroarch --features` and the unit suite; no console is touched |
| Target | PS5 payload ELF (`retroarch.elf`, x86-64, `libkernel_web` imports) plus `retroarch.cfg`, `sce_sys/param.json` and `sce_sys/icon0.png` as a homebrew title folder |
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
carrying a `PPSA#####` id. An existing PS5 RetroArch payload already produces
most of that folder, so the starting point is not a blank page:

| Source | What it already gives us | What it does not |
| --- | --- | --- |
| `reference/ps5-retroarch/` — the payload recipe from `ps5-payload-dev/websrv` at `1afd476`, kept read-only with its digests in `PROVENANCE.txt` | a working way to fetch and patch the upstream tarball, the configure switch set, the title's `icon0.png` taken from the upstream tree, a config seed, and seven per-core recipes | it launches through that project's loader with its own launcher manifest, it renders through a software SDL2 driver with every GPU path switched off, and it needs ports this host does not have (`docs/FINDINGS.md`) |
| A small signer of our own, in `tools/`, driven by our `Makefile` | the `eboot.bin` and title folder the console reads as a PPSA title | it is the piece we still write |
| `../ps5-native-app-boilerplate-main` | its `tools/build.sh` compiles and wraps an app end to end | it is not a drop-in host for a program of RetroArch's shape; the differences are below |

So the default route is: build RetroArch with **our** patched tree and **our**
graphics flags, stage the folder with the recipe's two solved pieces, and wrap it
with our own signer. The boilerplate's differences are recorded so a step does
not discover them halfway:

- it compiles **only** `src/*.c`, `src/*.cc` and `src/*.cpp`, with fixed flags
  (`-std=c11` / `-std=c++20`, `-O2 -Wall -Wextra -ffunction-sections`,
  `-fno-exceptions -fno-rtti`). RetroArch's tree is dozens of directories with
  per-file flags and its own configure step, so it cannot be dropped into `src/`;
- it links a fixed set — its own CRT and C++ runtime, two AGC link stubs, the
  static archives passed through `APP_STATIC_ARCHIVES`, the PacBrew archives, and
  `$PS5_PAYLOAD_SDK/target/lib/*.so` under `--as-needed` — and exposes no hook
  for extra linker flags. A graphics backend needs its own link inputs, and the
  OpenGL one also needs `-Wl,-u,ps5_agc_gate2_run` and a heap wrap set;
- it links against payload SDK **v0.42** through its own `prospero-clang18`
  wrapper, which requires a `clang-18` binary on the host. This host's toolchain
  is clang 22.1.8 from the SDK's own `prospero-clang`;
- it produces a directory-style homebrew application that a homebrew loader
  picks up from `/data/homebrew/<TITLE_ID>/`. It does not register a title with
  the shell, and it says so in its own deployment document.

If a later step adopts it anyway, the adoption is one step with one acceptance
line: the same RetroArch revision builds and loads through its pipeline, and the
differences above are answered in that step's evidence rather than worked around
in `vendor/`.

## Reference material

Two directories hold material this project did not write, and they are different
kinds of thing:

| Path | Committed | Rule |
| --- | --- | --- |
| `reference/` | yes, with a `PROVENANCE.txt` naming the source, the revision and every file's digest | read-only. It is what a claim was measured against; a change to it is its own step, and the digests are re-recorded in that step |
| `vendor/` | no, ignored | build-time only. `tools/fetch-upstream.sh` recreates it from the pin on every clean build, so nothing in it can be relied on to persist |

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
