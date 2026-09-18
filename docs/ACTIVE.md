# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-18_

## Now

**The decision has changed: stop converting the payload, build a native title.**
The vendored-recipe route has been abandoned by the project owner. The reason is
measured, not a preference: a title's `eboot.bin` must be a *converted* image and
the conversion strips the dynamic symbols the homebrew launcher needs, so one set
of sources cannot serve both routes, and every step of converting the finished
payload produced a new failure on the console
(`docs/FINDINGS.md`, the loader-route entries).

**The new foundation is the pipeline that already produces working titles.**
`../ps5-native-app-boilerplate-main` (and the project built on it,
`../ProsperoLight`) compiles application sources with a fixed toolchain, links
them through its own CRT, runtime and `ps5-pie.ld`, and signs the result with its
own tool — the exact path that produces the `PPSA99988` image which starts on this
console. Building RetroArch *inside* that pipeline is a different job from
converting a finished payload into it: the sources are compiled for it rather than
adapted afterwards.

**What the new pipeline requires, measured from its build script.** Sources are
collected from `src/` only, matching `src/**/*.{c,cc,cpp}`, and compiled with
fixed flags — `-std=c11` for C, `-std=c++20` for C++, `-O2 -Wall -Wextra
-ffunction-sections -fdata-sections`. Include paths and static archives are passed
through `APP_INCLUDE_PATHS` and `APP_STATIC_ARCHIVES`, and the result is linked
with the project's own `app_crt.o`, `app_cpp_runtime.o`, stub objects and version
script. A tree of RetroArch's shape has to be reached by include path rather than
dropped into `src/` one file at a time.

**Its one hard prerequisite is not installed here.** The build needs **Clang 18**:
the SDK's `math.h` defines `isnan` unconditionally, and Clang 22's libc++ headers
call `std::isnan`, so the two cannot be mixed (`error: expected unqualified-id`).
The package is available — `extra/clang18 18.1.8-2` — and installing it needs
administrator rights, which this session does not have and does not ask for.

## Next

1. Install Clang 18 (`sudo pacman -S clang18`), then build
   `../ps5-native-app-boilerplate-main` with
   `PS5_CLANG=/usr/bin/clang18 PS5_PAYLOAD_SDK=$PS5_PAYLOAD_SDK make app` — the
   known-good image that proves the pipeline end to end on this machine.
2. Place that image on the console under its **own** title id (PPSA99999) and
   launch it. That answers whether a native-pipeline image boots here, without
   touching PPSA99005 or any other title.
3. If it boots, bring RetroArch's sources into the pipeline behind a symlinked
   `src/` tree: a first milestone is the frontend initialising with a null video
   driver, the second a frame through the Vulkan driver in `../PS5_Vulkan`.
4. Retire the conversion path once (3) holds, and say so in `docs/REFERENCE.md`.

## Working notes

- `tools/fetch-ports.sh` provisions the ports image once (346 MB, digest-checked,
  cached in `.deps/pacbrew/`) and `tools/build-baseline.sh` runs `--check`
  without building anything. Both are cheap to re-run.
- The recipe's own build script is never edited: our changes are injected into a
  copy at `work/baseline/build.sh`, so `reference/ps5-retroarch/` stays the
  baseline it was measured against.
- This console's FTP service ignores the path in a listing command and answers
  deletes with 226. `tools/deploy.py` handles both; do not "simplify" that away.
- `source $PS5_PAYLOAD_SDK/toolchain/prospero.sh` before any manual PS5 compile;
  the clang is 22.1.8 targeting `x86_64-sie-ps5`.
- The console's address and credentials come from the ignored `.env`.

## Last verified

| Check | Result |
| --- | --- |
| `tools/build-baseline.sh` from cache | PASS in 34 s; 4 files staged; payload imports `libkernel_web.sprx`, not `libkernel_sys.sprx` |
| `tools/build-baseline.sh` after a source edit | PASS in 35 s |
| `tools/fetch-ports.sh` | PASS: v0.40.2 verified by SHA-256, SDL2 2.30.12 resolved |
| Console listing of `/data/homebrew/PS5_RetroArch/` | payload, config, manifest and launcher present; icon still at the folder root |
| Console FTP writes | FAILED: `550 Read-only filesystem` on the last attempt; reads fine |
| Console run of the baseline, `evidence/m1-baseline-loads/` | PASS: payload started under the homebrew launcher (pid 151), menu on screen, config written to the title folder |
| `tools/stage-ppsa.sh` | PASS: `dist/PPSA99005/` holds 6 files with a digest manifest |
| Image conversion | PASS: `eboot.bin` 49,968,821 bytes, `signed, plaintext`, 12 segments, `integrity: valid` |
| Launcher icon | PASS: `title/assets/retroarch.png` (640x640) resampled to 512x512, sha256 `65dcb224d62da0427f17589f16922918` |
| `tools/verify.sh` (all gates) | not yet green: the gate scripts it names still have to be written |

## Open findings

- The console's write path needs re-checking before the deploy can finish. If it
  stays read-only, the loader cannot see a new folder either.
- The recipe pins upstream 1.21.0 while our own tree is 1.22.2. The baseline is
  deliberately the recipe's version so it matches what the user already runs.
- Nothing in this repository has run on a console yet, so every target-layer
  claim in the docs is a specification, not a result.
