# PS5 RetroArch

A native RetroArch homebrew application for jailbroken PlayStation 5 consoles.
RetroArch is the reference frontend for the libretro API; this project brings it
to a console that was never meant to run it, so the emulators and games that
already exist as libretro cores can run there too.

**Status: project definition.** The gates, the milestone map, the invariants and
the step ladder are written and committed; no code has been built or run on a
console yet. What is true right now is in [`docs/ACTIVE.md`](docs/ACTIVE.md), and
the evidence behind each claim is in [`docs/PHASE_LOG.md`](docs/PHASE_LOG.md).

## How it fits together

Everything about this console's platform layer already has an owner. This
repository supplies the RetroArch side of it and consumes the rest:

| Project | What it provides | How this project uses it |
| --- | --- | --- |
| [`ps5-payload-sdk`](https://github.com/ps5-payload-dev/sdk) | the PS5 compiler, sysroot, and the console's public APIs (`ScePad`, `SceAudioOut`, `SceVideoOut`, `SceUserService`, networking) | `$PS5_PAYLOAD_SDK/toolchain/prospero.sh` supplies the toolchain for every PS5 compile |
| `../ps5-opengl-sdk-0.2.0` | a relocatable OpenGL 3.3 Core stack with a hardware acceptance run | the OpenGL backend, for a menu or a core that needs one |
| `../PS5_Vulkan` | the Vulkan driver being built for this console, with vendored Vulkan headers | the backend the XMB menu needs once the driver reaches rung 1.0; RGUI needs no GPU at all |

RetroArch itself is upstream, unmodified on disk and patched by a committed
series: the pinned release tarball is fetched into `vendor/`, which is never
committed and never edited by hand. Every change to it is a file in `patches/`.

What this repository owns: the RetroArch platform layer — a context driver that
takes a display and a graphics context, a platform driver that owns the title's
lifetime, a pad driver and an audio driver — plus the build, the staging, the
console deployment and the evidence pipeline that prove a step.

## Build

Requirements: Linux with `clang`, `clang-format`, `clang-tidy`, `make`,
`python3`, `curl`, `tar` and `pkg-config`, plus the PS5 payload SDK unpacked
somewhere and pointed at with `PS5_PAYLOAD_SDK` (default
`~/ps5-payload-sdk`).

```bash
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk
tools/verify.sh              # format, unit, build, integration, evidence
```

`tools/verify.sh` is the only entry point a step needs: it runs the five gates
in order and stops at the first failure. `tools/verify.sh --list` prints what
each gate runs, and `docs/TESTING.md` says what each one means.

## Install on a console

The application ships as a homebrew title folder, not a retail package:

```text
dist/<TITLE_ID>/
├── retroarch.elf          the frontend payload
├── retroarch.cfg          the configuration seed
└── sce_sys/               param.json, icon0.png
```

Copy that folder to `homebrew/` on the console's internal storage or a USB
drive, so it ends up beside the other homebrew titles, and start it from the
console's own homebrew launcher. This project does not configure a console, does
not install a loader and does not register titles — see
[`docs/DEPLOYMENT.md`](docs/DEPLOYMENT.md).

## Documentation

The documentation is the project's memory, and it is layered so that an agent
session pays only for what it reads:

| File | Role |
| --- | --- |
| [`AGENTS.md`](AGENTS.md) | frozen rules: read order, volatility, work loop, gates, evidence |
| [`docs/PLAN.md`](docs/PLAN.md) | the gates, the milestone map, the invariants |
| [`docs/ACTIVE.md`](docs/ACTIVE.md) | what is happening now, what is next, what was last verified |
| [`docs/REFERENCE.md`](docs/REFERENCE.md) | the step ladder, the environment, the pins, the graphics contract |
| [`docs/TESTING.md`](docs/TESTING.md) | what the gates mean and what counts as evidence |
| [`docs/DEPLOYMENT.md`](docs/DEPLOYMENT.md) | staging, upload, observing a run, rollback |
| [`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md) | symptom → cause → fix, and known-benign messages |
| [`docs/FINDINGS.md`](docs/FINDINGS.md) | append-only measurements and what they force |
| [`docs/PHASE_LOG.md`](docs/PHASE_LOG.md) | append-only, dated entry per landed step |

## Licence

RetroArch is GPL-3.0-or-later, and so is this port; the upstream licence travels
with the sources that get fetched into `vendor/`. Files in this repository carry
their own copyright and SPDX headers.
