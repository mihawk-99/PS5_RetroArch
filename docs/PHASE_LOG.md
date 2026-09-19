# Phase log

Append-only. A dated entry per landed step, newest at the end. Never rewrite an
existing entry: a correction is a new entry that says what it corrects. This
file is read by search and by its tail, never end to end.

An entry holds, in this order: what changed, why it is right, the evidence (the
exact command or run and what it returned), and the commit. Keep the failures in
too — the runs that were wrong are what stop the next agent from repeating them.

---

## 2026-09-18: The documentation contract is instantiated for PS5 RetroArch

The repository stopped being a template. `AGENTS.md`, `docs/PLAN.md`,
`docs/ACTIVE.md`, `docs/REFERENCE.md`, `docs/TESTING.md`,
`docs/DEPLOYMENT.md`, `docs/TROUBLESHOOTING.md`, `docs/FINDINGS.md` and
`tools/verify.sh` now describe a PS5 RetroArch port: its gates, its milestone
map (M0 contract, M1 console shell, M2 video and input, M3 cores and storage,
M4 audio, mapping and release), its four invariants, its step ladder, the
pinned environment, and the evidence rules. It exists because the project has
to be defined before any code is written, and it unblocks every later step.

**The evidence.** `grep -rno '{{[A-Z0-9_]*}}' AGENTS.md docs tools .gitignore`
returns nothing, so no template placeholder is left in the read path;
`bash -n tools/verify.sh` and `tools/verify.sh --list` return the five gate
commands; the toolchain was proven to work by cross-compiling a hello-world C
file, which produced an ELF 64-bit LSB pie executable, x86-64, version 1
(FreeBSD), 110,712 bytes, from `prospero-clang` 22.1.8 targeting
`x86_64-sie-ps5`. The four findings of the survey are committed in
`docs/FINDINGS.md`; the environment facts are in `docs/REFERENCE.md`.

**What was tried first.** The first draft of the plan assumed the shipped
graphics path was a Vulkan context driver through `../PS5_Vulkan` and treated
the OpenGL project as unrelated. Reading the two local checkouts showed the
opposite ordering: the OpenGL project is the only one with an installed consumer
package, a pkg-config contract and a recorded hardware acceptance run, while the
Vulkan project has driver sources and headers but no consumer package yet.
`docs/REFERENCE.md` now records both stages with the M2.1 measurement that
decides between them, instead of the assumption. Recorded here so the next step
does not rediscover it.

**Commit.** `3dfc3dd` — Instantiate the agent documentation contract for PS5
RetroArch.

---

## 2026-09-18: The existing PS5 RetroArch payload recipe joins the repository as a baseline

`reference/ps5-retroarch/` now holds `ps5-payload-dev/websrv`'s
`homebrew/RetroArch/` at commit `1afd476`, copied by sparse checkout and kept
read-only, with `PROVENANCE.txt` recording the source, the revision, the fetch
date and a SHA-256 for every file. It exists because that recipe already turns
the upstream RetroArch tarball into a loadable PS5 payload, so this project
starts from a known baseline instead of from nothing, and it unblocks the
packaging work: the recipe's staging step already produces the title's
`icon0.png` and its `retroarch.cfg` seed.

**The evidence.** The clone is reproducible with
`git clone --filter=blob:none --no-checkout --depth 1
https://github.com/ps5-payload-dev/websrv.git` followed by
`git sparse-checkout set homebrew/RetroArch`; it returned twelve files totalling
48 KB, and `sha256sum` of each is committed in the recipe's `PROVENANCE.txt`.
Reading `build.sh` shows it pins upstream **1.21.0** and configures with
`OS=BSD`, `--enable-sdl2 --enable-mmap --enable-dylib` and every GPU switch
disabled, which is what `docs/FINDINGS.md` records.

**What was tried first.** The recipe was expected to be a drop-in build, and it
is not: `prospero-pkg-config --exists sdl2` exits 1 on this host and
`$PS5_SYSROOT/user/homebrew/` holds an empty `include/`, so the SDL2 the recipe
enables is not present. That measurement is in `docs/FINDINGS.md` and is the
first item in `docs/ACTIVE.md`'s Next list. The recipe was not modified to work
around it: it is committed as it was fetched, and any change to it is a step of
its own.

**Commit.** `0d8a225` — Vendor the existing PS5 RetroArch payload recipe as a
baseline.

**A correction to this entry's own landing.** It first landed as commit
`107dbae`, whose message was a stray shell fragment rather than the text above:
the command that wrote it chained a nested here-document, so the shell closed
the message early and fed it the script instead. Nothing was pushed, and the
commit was amended in place to `0d8a225` with no change to a single file. The
lesson is in the command, not in the tooling: one message per command, written
from a file, never a here-document inside a chain.

---

## 2026-09-18: The baseline payload builds from cache in 34 seconds and reaches the console

`tools/fetch-ports.sh` and `tools/build-baseline.sh` now build the Option 1
baseline end to end, and `tools/deploy.py` publishes it to the console under this
project's own folder name. It exists because a console run of something we did
not write is what makes the later runs of our own build interpretable, and it
unblocks the move to the Vulkan driver: the frontend, its configuration seed and
its launcher are now known-good.

**The evidence.** `tools/build-baseline.sh` returned PASS in 34 s and staged four
files in `dist/baseline/` with a digest manifest; the payload check found
`libkernel_web.sprx` in its imports and no `libkernel_sys.sprx`. A rebuild after
appending a line to `menu/menu_driver.c` returned PASS in 35 s, which is the
measurement that matters for the edit-test loop. `tools/fetch-ports.sh` verified
PacBrew v0.40.2 against its SHA-256 and resolved SDL2 2.30.12. The staged tree was
uploaded and then listed back from the console: `retroarch.elf` at 70,642,656
bytes beside `retroarch.cfg`, `homebrew.js` and the manifest.

**What was tried first.** Three approaches to the toolchain's prefix failed before
the fourth worked, and all four are recorded in `docs/FINDINGS.md`: rewriting
`PS5_HBROOT` before the recipe sources `prospero.sh` (it exports the value
unconditionally), a private mount namespace (`unshare -Urm` cannot create `/user`
without root), and bubblewrap (`--tmpfs /user` fails the same way). What works is
to give the build both spellings: a pkg-config of ours that answers with the host
path, and a rewrite of the generated `config.mk`. The recipe's own build script
was never edited — the changes are injected into the copy under `work/`, so
`reference/ps5-retroarch/` stays the baseline it was measured against.

**Still open.** The launch run has not happened: the console's FTP service began
answering `550 Read-only filesystem` to writes during the last deploy, while
reads and the control payload stayed healthy. `sce_sys/icon0.png` is therefore
still at the folder root. Both are the first item in `docs/ACTIVE.md`'s Next
list, and neither is being treated as done.

**Commit.** `1036f50` — Build and deploy the Option 1 baseline.

---

## 2026-09-18: Option 1 is proved — the baseline loads on the console and draws its menu

The vendored recipe's RetroArch 1.21.0 payload was started on the console through
its own homebrew launcher, and the menu came up. It exists because a console run
of something we did not write is the reference every later run of our own build
is read against, and it unblocks the move to the Vulkan driver: the frontend, its
configuration, its launcher manifest and the console's launcher path are all
known-good now.

**The evidence.** `tools/console-launch.sh --capture` started
`/data/homebrew/PS5_RetroArch/retroarch.elf` through websrv's `/hbldr` endpoint
with the arguments and environment the launcher manifest declares, and the
capture is committed as `evidence/m1-baseline-loads/` with the run it came from
and the expectation `tools/evidence.py compare` replays (exit 0, one capture,
zero failures). The raw capture is `klog/launch-20260918-120546.log`. The
console's control payload reported the run as the active application —
`app=24600 pid=151` under the homebrew title — and the owner confirmed the menu
was on screen. While it ran, RetroArch wrote `retroarch.cfg` (34 KB to 110 KB) and
a `.config/retroarch/` tree into its own folder, which is independent proof that
the frontend reached its main loop with a working storage path.

**What was tried first.** The first launch captured a single line —
`Fontconfig error: Cannot load default config file: No such file: (null)` — which
looked like a failure and is not one: it is fontconfig falling back to its
built-in defaults before any driver starts, and the menu renders anyway. It is
now in `docs/TROUBLESHOOTING.md` with its exact text, so the next reader does not
chase it. A second wrong turn is worth recording too: two deploys looked like
failures while the files had in fact arrived, because this console's FTP service
ignores the path argument of a listing command and answers deletes with 226.
Both behaviours are handled in `tools/deploy.py` and written up in
`docs/FINDINGS.md`.

**Commit.** `d869361` — Prove the Option 1 baseline on the console.

---

## 2026-09-18: The PPSA title folder, and what stands between it and eboot.bin

`tools/stage-ppsa.sh` now assembles the PPSA title folder the objective asks
for: `dist/PPSA99005/` with the title's identity (`title/sce_sys/param.json`,
checked for a valid id, content id, version and launch intent), the 512x512
launcher icon, `sce_module/libc.prx`, the configuration seed, the payload beside
them and a digest manifest. It exists because a payload folder and a title folder
are different things: the console lists the first only while a launcher is
running, and installs the second as an application in its own right.

**The evidence.** `tools/stage-ppsa.sh` stages five files into `dist/PPSA99005/`
and writes `manifest.sha256`; with `eboot.bin` absent it prints that the folder is
not complete and exits 3, which is the intended state rather than a silent
success. The three converter requirements found on the way are written up with
their exact messages in `docs/FINDINGS.md`.

**What was tried first.** Three rejections in a row from the image converter, each
one real and each one narrower than the last. The linker's default layout leaves
no room for the console's process parameters, so the link now goes through
`prospero-lld` with our `linker/ps5-pie.ld` and one extra page boundary; that was
verified on a minimal program, which converted cleanly at 121,536 bytes, before
being applied to the 68 MB payload. Weak `__dlopen` references — which clang
emits because FreeBSD's libc declares both spellings — and a `kernel_mprotect`
reference then had to be defined, which `platform/ps5_dl_stubs.c` does, including
one that forwards to `mprotect` instead of failing, because a failing stub is what
denies a dynamic recompiler executable memory. The third rejection is not ours to
fix in the build: the converter refuses to publish application exports
(`error: native converter does not yet publish application exports`), and the
payload has exports because `-rdynamic` asks for them.

**Commit.** `4cea907` — Stage the PPSA title folder and clear two of the three converter requirements.

---

## 2026-09-18: The PPSA title folder is complete, eboot.bin included

`dist/PPSA99005/` is now a complete PPSA title folder: the signed application
image, `sce_module/libc.prx`, the title's identity and icon, the configuration
seed, the payload and a digest manifest. It exists because the objective is a
PPSA homebrew, and a payload folder is not one — the console lists a payload only
while a launcher runs, and installs a title as an application in its own right.

**The evidence.** `tools/stage-ppsa.sh` exits 0 with six files staged. The
converter's own inspection of the image reports `container: signed, plaintext`,
`segments: 12`, `authority: 0x3100000000000002`, `program type 0x1` and
`integrity: valid`; the digests are in the folder's `manifest.sha256`, with
`eboot.bin` at 49,968,821 bytes and SHA-256
`88b2b02ded8a4bd00b2505aa01f70c8cfcf8150c927e1ccad5d9abfd0ce974f2`.

**What was tried first.** Four rejections from the converter, each recorded
because each one is a property of this image format rather than of RetroArch.
The layout had to leave room for the console's process parameters, which is what
`linker/ps5-pie.ld` and one page boundary fix. Linking straight to the linker
skipped the startup objects the compiler driver adds, so there was no `_start`
and the entry point stayed 0 while the converter complained about symbols that
`crt1.o` defines — a stub file written before that was understood turned out to
be redundant and was deleted rather than kept as ballast. The image's own
boundary symbols (`__bss_start`, `__bss_end`, `__image_start`, `__image_end`)
cannot come from any library, so they are defined in
`platform/ps5_image_symbols.S`. And `--exclude-libs=ALL` was needed because the
linker otherwise exports symbols pulled out of static libraries, which both
bloats the dynamic table and trips the converter's export rule.

**Still open.** No part of this has been on the console: the folder is built and
validated here. Installing and running it is the next step and needs a console
window, which is asked for rather than taken.

**Commit.** `c8f60e7` — Complete the PPSA title folder with a signed application image.

---

## 2026-09-18: The launcher icon is generated from the project's artwork

The title's icon now comes from `title/assets/retroarch.png` — the RetroArch
invader, 640x640 — resampled to the 512x512 a launcher tile must be. It exists
because the icon is the first thing the console shows for this title, and until
now it was whatever the vendored recipe happened to lift out of the upstream
tree: a detail of that recipe rather than a decision of ours.

**The evidence.** `tools/stage-ppsa.sh` regenerates `build/icon0.png` on every run
and copies it into the folder; the staged `sce_sys/icon0.png` is 512x512 and its
digest is in the folder's `manifest.sha256`. `tools/build-baseline.sh` generates
the same image for the payload folder, so the two outputs cannot drift apart. The
source image was inspected at 640x640 before resampling, and the result was read
back as an image to confirm it is the intended artwork rather than a blank or
distorted tile.

**Commit.** `162f974` — Generate the launcher icon from the project's artwork.

---

## 2026-09-18: The installed title was launched, it crashed, and the log says why

The user asked for the kernel log to be watched while `PPSA99005` was opened.
Both were done: the log was captured from the console's klog service and the
title was launched through the resident control payload. It does not start, and
the capture says exactly why.

**The evidence.** `evidence/ppsa-99005-startup-crash/` holds the distilled record
and the raw capture (`klog/ppsa99005-122733.log`, 214 lines), and
`tools/evidence.py compare` replays it. The launch returned
`EndAppMount(0x00000018)` with no running process; the log shows the title being
mounted, then `SIGSEGV` in a thread named `eboot.bin`, a page fault at address
`0x1`, `/app0/sce_module/libc.prx` named in the crash block, and
`[Syscore App] App Crash`. The installed image is 51,870,448 bytes — a link-stage
ELF, not the converted and signed 49,968,821-byte image this repository produces.

**What was tried first.** `tools/install-title.sh` was written to replace that
image and ran, reporting `226 File deleted` and `226 Path renamed` and verifying
nothing. Immediately afterwards the entire `/data/homebrew/PPSA99005/` directory
was gone from the console: absent from the parent listing, refused by `CWD`. The
neighbouring homebrew folders were untouched and the control payload never
faltered. That is recorded as its own finding rather than smoothed over, because
it changes the install procedure: this service may lose a directory during a
replace, so an install must write a whole folder and then verify it.

**Still open.** The repaired run has not happened. Everything needed is local
(`dist/PPSA99005/` with a digest per file, plus the same payload on the console
under `/data/homebrew/PS5_RetroArch/`), so nothing was lost that cannot be
rebuilt.

**Commit.** `e1c0dfa` — Watch the kernel log, launch the title, and record why it crashes.

---

## 2026-09-18: The deployment path is rebuilt from the project that already solved it

`tools/deploy-title.py` and `tools/ps5_ftp.py` now publish the PPSA folder, and
they are modelled on `../PS5_Vulkan/tools/deploy.sh` — the deployment path that
already works against this console. It exists because my own FTP client kept
reporting success while the console kept the previous bytes, and the reason was
three server quirks the sibling project had already written down: a successful
delete is answered with 226, which `ftplib.delete()` rejects; paths resolve from
the root; and a listing with a path argument behaves inconsistently.

**The evidence.** `tools/deploy-title.py --check` reports the console's actual
state, including the image's magic bytes — `eboot.bin magic: 7f454c46 (NOT a
converted image)`. A deploy verifies every stored file's size and exits non-zero
when one does not match; that is what caught `libc.prx` reverting. Separately,
freshly generated blobs uploaded to the same folder round-trip byte-for-byte at
2 MB, 8 MB and 60 MB, which rules the server out as the limit.

**What was tried first.** Several hand-rolled FTP paths, including one that
replaced `eboot.bin` and a `libc.prx` and appeared to succeed at every step. The
useful lesson is in the tooling, not the effort: a transfer that is not verified
by reading back or by checking the stored size is not evidence, and this server
is exactly the case where that distinction matters.

**Still open.** The console's `eboot.bin` and `sce_module/libc.prx` are still the
other session's build; the two files in `dist/PPSA99005/` are the converted ones.
Replacing them needs either the console's owner or a moment when no other session
is publishing that title.

**Commit.** `a80417a` — Publish the PPSA folder with the deployment path this console needs.

---

## 2026-09-18: The deployment is one step from working, and the blocker is measured

The user asked for the deployment, so it was run with the helpers taken from
`../PS5_Vulkan/tools/deploy.sh`. The result is specific: the folder is writable
and stays written, but the converted image will not take.

**The evidence.** `sce_sys/icon0.png` (512x512) published and verified. Test blobs
of 2 MB, 8 MB and 60 MB, a 49,968,821-byte zero blob, a pattern blob of exactly
the converted image's size, and a 40 KB marker all round-tripped byte-for-byte
under `/data/homebrew/PPSA99005`. The converted image did not: written under its
own name, under an unused name, in place, and through a temporary name with a
rename, the listing and the read-back both returned 51,870,448 bytes starting
`7f454c46`. A file created under a fresh name received `eboot.bin`'s bytes.

**What was tried first, and corrected.** The first explanation recorded in
`docs/FINDINGS.md` blamed the second session working on this console. The user
pointed out that session publishes `PPSA99988`, not this title, and the entry has
been superseded by a correction with the measurement that rules it out. The same
correction records that `/data/homebrew/PPSA99005` and
`/system_ex/app/PPSA99005` are separate trees, not one storage seen two ways — a
marker written through one is absent from the other.

**Still open.** Placing those two files needs a route other than this FTP
service. Everything around it is ready and verified; the check that names success
is `tools/deploy-title.py --check` reporting `eboot.bin magic: 4f153d1d`.

**Commit.** `4ad48bf` — Correct the deployment diagnosis and record what the console actually stores.

---

## 2026-09-18: The route changes — a native title instead of a converted payload

The project owner has ended the conversion approach: build RetroArch on the
pipeline that already produces working titles rather than converting a finished
payload into the title format. This entry records why that is the right call and
what the new shape is, because both are measured rather than assumed.

**Why the conversion route ends.** A title's `eboot.bin` must be a converted
image, and conversion strips the dynamic symbols the homebrew launcher needs — the
converter refuses to publish exports and `--exclude-libs=ALL` keeps the rest
internal. So the same build cannot serve both routes, and each step of converting
the finished payload produced a further failure on the console: a null-pointer
crash inside a raw image, then `PRX_SCE_MODULE_LOAD_ERROR` from an image the
loader would not take. The lesson is the one the user reached first: sources
should be *built for* the title pipeline, not adapted into it afterwards.

**The new foundation.** `../ps5-native-app-boilerplate-main`, and the project
built on it, `../ProsperoLight` — same author, both native PS5 applications whose
titles start on this console. Their `tools/build.sh` collects `src/**/*.{c,cc,cpp}`
only, compiles it with fixed flags (`-std=c11`, `-std=c++20`, `-O2 -Wall -Wextra
-ffunction-sections -fdata-sections`), links it with the project's own
`app_crt.o`, `app_cpp_runtime.o`, stub objects and version script, and signs the
result into `eboot.bin`. Include paths and static archives arrive through
`APP_INCLUDE_PATHS` and `APP_STATIC_ARCHIVES`, which is how a tree of RetroArch's
shape can be reached without moving it file by file.

**The blocker found by trying it.** Building that project here fails in its C++
headers: the SDK's `math.h` defines `isnan` unconditionally while Clang 22's
libc++ headers call `std::isnan` — `error: expected unqualified-id`. Both SDK
copies on this machine share that header, so the pairing is inherent, which is
why the project pins Clang 18 (`extra/clang18 18.1.8-2` is available). Installing
it needs administrator rights, so the build waits on that.

**Commit.** `80dc8d4` — Change route: build a native title on the pipeline that works.

---

## 2026-09-18: ProsperoLight builds here, on Clang 22, and its title is staged

The plan is now: prove the whole console path with a complete, known-good title,
then replace its sources with a fresh RetroArch. This entry records the first
half — ProsperoLight builds on this machine and produces a valid title.

**What it took, four things, each found by a failed build.** The project's own
vendored SDK rather than the one in `$HOME`; `PS5_CLANG=/usr/bin/clang`, because
its wrapper defaults to a `clang-18` that is not installed while the sibling
project that works uses plain `clang`; a minimal, documented `jsonschema`
stand-in at `tooling/pystub/`, because mbedTLS regenerates a source file with a
script that imports it; and a verified `runtime/libc.prx`, which the project ships
only as a manifest — the boilerplate's copy carries the same digest. All four are
in `tools/build-native-app.sh`, and none of them edits the project's sources.

**The evidence.** `make app` completed and the converter's own inspector reports
`container: signed, plaintext`, twelve segments, `integrity: valid` for
`dist/PPSA99002/eboot.bin`. The title folder is staged at `handoff/PPSA99002/`
with its identity `PPSA99002`, `UP9000-PPSA99002_00-PROSPEROLIGHT000`,
"ProsperoLight".

**What is not proven.** That it runs on the console. Placement is the console
owner's step, because writes from this machine do not reach the console's title
folders, and a title also has to be registered with the shell before its launch is
anything but `is not registered`.

**Commit.** `d1165a2` — Build ProsperoLight on this machine and stage its title.

---

## 2026-09-18: The native pipeline is proven on the console, and the swap is scoped

ProsperoLight, built on this machine through the native pipeline, was placed on
the console by its owner and **started flawlessly**. This is the milestone the
route change was made for: sources compiled for the title pipeline produce a title
the console runs, so the conversion fight is behind us.

**The evidence.** The owner observed the title running. The kernel capture for the
attempt contains zero fatal signals (`klog/PPSA99002-134510.log`), and the
converter's own inspector reports `container: signed, plaintext`, twelve segments,
`integrity: valid` for `dist/PPSA99002/eboot.bin`. A note on reading those logs
honestly: an earlier pass of this session read `is not registered` lines as being
about this title, when they were the console's AutoMounter talking about other app
ids (0x2019, 0x18) — and `tools/console-run.sh` printed "still running" from a
condition that is always true. Both are corrected here; the observation that
matters is the owner's.

**What it took, and it is now one command.** `tools/build-native-app.sh` carries
the four things that had to be right: the project's own vendored SDK,
`PS5_CLANG=/usr/bin/clang` (the wrapper default of `clang-18` is neither installed
nor needed, as the owner pointed out), the documented `jsonschema` stand-in under
`tooling/pystub/`, and a verified `runtime/libc.prx`.

**The swap, scoped by measurement.** RetroArch 1.22.2 is cloned fresh. The
pipeline's graphics layer is not a GPU stack: its renderer drives VideoOut
directly (`sceVideoOutOpen`, `RegisterBuffers`, `SetBufferAttribute`,
`SetFlipRate`, `SubmitFlip`) from its own direct memory — enough for a CPU
framebuffer, not enough for RetroArch's menu, which draws through a GPU display
context. The backend that can serve it exists in `../PS5_Vulkan`
(`libps5vk.ps5.a`, with `ps5vk_CreateInstance` and friends defined) and its headers
live in `../ps5-opengl-sdk-0.2.0/third_party/Vulkan-Headers`. Both are reached
through `APP_INCLUDE_PATHS` and `APP_STATIC_ARCHIVES`, which is exactly how the
sibling project already consumes them.

**Commit.** `1da48f0` — Prove the native pipeline on the console and scope the RetroArch swap.

---

## 2026-09-18: The scaffold title runs on the console and presents frames

This repository's own title — the native pipeline's scaffolding, this project's
display layer and its own entry point — deployed by FTP and launched, and stayed
up. First milestone of the RetroArch plan, and the first code of ours on the
console.

**The evidence.** The launch was captured with the console's log delimited at the
launch (`klog/PPSA99169-135230.log`): the capture holds **zero** fatal signals and
**zero** `exit_value` lines, shows our process twice in the shell's accounting
(`[SceShellCore] 23%  700 PPSA99169 eboot.bin`, then 22%), and the control payload
reports `app=49176 title=PPSA99169 count=1 pids=228` — still running. A
present-and-wait loop is what 22–23% CPU looks like.

**What was tried first, and cost a run each.** The first version of the display
layer guessed at constants the console does not forgive, and the title built,
launched and reported `eboot.bin calls exit() exit_value=1` because `open` had
failed. Comparing against the sibling application that had already proved them on
hardware showed five errors at once: the pixel format
(`0x8000000022000000`, 64-bit), the direct-memory size accessor (`size_t`, not
`int64_t`), the mapping protection (`0x33`), the `VideoBuffer` shape (four
pointers) and the tiled pixel addressing. The constants and the tiling are now
taken from that project with the reason written down in `src/display.cpp`, because
none of them is derivable by reasoning.

**Commit.** `b18e203` — Run this project's own title on the console.

---

## 2026-09-18: The PS5 video driver is written and compiles

`src/video_ps5.cpp` implements both interfaces RetroArch asks of a video driver:
the `video_driver_t` itself and the `video_poke_interface_t` that RGUI needs. It
presents through `src/display.cpp`, which has already run on the console.

**Why this is the right first target, measured rather than assumed.** RGUI
references the menu display context **zero** times; XMB references it 72 times, and
every backend in `gfx_display_ctx_drivers[]` is a GPU API — OpenGL, Vulkan, Metal,
Direct3D, or a console-specific one — with no software entry. So RGUI is the only
menu that can run over VideoOut alone, and XMB needs a GPU context over
`../PS5_Vulkan` later.

**What the interface actually requires.** Six members: `init`,
`suppress_screensaver`, `alive`, `frame`, `ident`, `poke_interface`. Verified on
the interface report and by compiling: `suppress_screensaver` returns **bool**, not
void, and the table is filled positionally with `overlay_interface` (inside
`#ifdef HAVE_OVERLAY`) before `poke_interface`, plus `wrap_type_to_enum` after it.
`ps5_frame` prefers the menu's framebuffer when RGUI has handed one over, else the
core's frame, and scales nearest-neighbour through the display layer's tiled
addressing.

**Evidence.** The driver compiles through the pipeline's own compiler against
RetroArch's headers:

```text
tooling/prospero-clang18 -std=c++20 -Isrc -Ivendor/retroarch \
    -Ivendor/retroarch/libretro-common/include -c src/video_ps5.cpp
compiled: 6,056 bytes, no warnings
```

**Commit.** `74e22bd` — Write the PS5 video driver for the RetroArch frontend.

---

## 2026-09-18: Websrv removed, and the frontend compiles from RetroArch's own build

Two things in one step: the websrv-derived material is gone, and the frontend now
compiles from a source list that comes from RetroArch itself.

**Removed.** `reference/ps5-retroarch/` (already empty), the Option 1 baseline
build tree under `work/baseline/`, its artefacts in `dist/baseline/` and
`dist/PPSA99005/`, the pacbrew ports cache, and the tools that existed only for
the conversion route (`build-baseline.sh`, `prospero-clang-link`, `stage-ppsa.sh`,
`fetch-ports.sh`). The intention was to start from ProsperoLight's foundation, and
none of that is part of it.

**The source list now comes from RetroArch's own build.** The previous build script
took a 264-object list from the deleted websrv tree. That dependency is gone:
`tools/retroarch-sources.sh` runs RetroArch's own `./configure` and `make info`,
and prints exactly the objects a link needs. Its `OBJ` list is grown from 248
conditional `OBJ +=` lines in `Makefile.common`, so only make can produce it for a
given configuration; configure runs in a copy under `build/ra-conf/`, so
`vendor/retroarch` is never touched. The configure flags are this project's — RGUI,
no graphics API, nothing that needs a library the SDK does not ship.

**The evidence.** `tools/build-retroarch.sh` compiled **225 of 244** sources with
the pipeline's own compiler. What remains is Linux-only and correctly out of scope
for this console: `udev_joypad.c`, `udev_input.c`, `linuxraw_input.c`,
`keyboard_event_xkb.c` and `libchdr_zstd.c`. Four fixes got there, each found by a
compile rather than guessed:

- the generated `config.h`, placed where RetroArch's relative includes
  (`../config.h`, `../../config.h`, `../../../config.h`) resolve;
- `-D_GNU_SOURCE`, which RetroArch's own build passes — `_POSIX_C_SOURCE` alone
  hid `strlcpy` and broke sixty more sources than it fixed;
- `-DCLOCK_REALTIME=0 -DCLOCK_MONOTONIC=4`, the header's own values, because this
  SDK's `time.h` hides the clock ids under the standard the pipeline compiles with;
- RetroArch's vendored zlib (`--enable-builtinzlib`) with its compatibility headers
  on the include path.

**Still to do for RGUI on screen.** Two pieces, both now unblocked:
register `&video_ps5` in `video_drivers[]`, and reconcile the entry point, since
`retroarch.c` defines its own `main` and the pipeline's builder supplies a `_start`.
Then link with the pipeline's CRT and runtime, stage, deploy and launch.

**Commit.** `b214c22` — Delete the websrv material and compile the frontend from RetroArch's own build.

## 2026-09-18: The title links, signs, and carries the PS5 driver in its table

**What was asked.** Register `&video_ps5` in RetroArch's `video_drivers[]`,
reconcile the entry point, and link the frontend with `src/` through the native
pipeline's CRT — the two items the goal table still had open.

**Both were done, and the link is the proof.** `bash tools/build-title.sh`
compiles 225 of 225 sources, archives them into `build/ra/libretroarch.a`, links
that with `src/` through `app_crt.o` and the SDK's `_start`, signs the result and
assembles `dist/PPSA99169/`. `eboot.bin` is 8,026,239 bytes, `container: signed,
plaintext`, 12 segments, `integrity: valid`.

**The registration is verified by relocation, not by reading the source.**
`readelf -r build/ra/obj/gfx_video_driver.c.o` shows `.rela.data.video_drivers`
holding exactly two entries, `video_ps5` then `video_null`, and
`nm build/llvm-pie.elf` shows `video_ps5`, `video_null`, `rarch_main` and a single
`main`. The entry point needed no adapter: RetroArch guards its own `main` with
`#ifndef HAVE_MAIN`, which its desktop build defines, so `-DHAVE_MAIN` removes it
and `src/main.cpp` supplies the only one.

**Five faults were found on the way, and each was a real one.**

1. *The build compiled the wrong tree.* `tools/build-retroarch.sh` read sources
   from `vendor/retroarch` while the port's patches were applied to `build/ra-conf`.
   It linked, signed and started with `video_drivers[]` holding no ps5 entry: the
   file that lists the drivers came from the tree that had never heard of the
   patch. Source and patch now come from the same place by construction.
2. *The feature flags were stated twice.* The script passed 36 `-DHAVE_*` flags of
   its own, six of which configure had turned off — the BSV movie recorder, the
   soft filters, the video filters, the translator, gfx widgets. Those compiled
   code whose sources are not in the object list, so the link failed on symbols
   belonging to files nobody built. `tools/retroarch-flags.sh` now asks `make -n`
   for the flags it would use; the hand-written list is gone.
3. *`config.h` alone is not enough.* A source can test a feature before any header
   that includes `config.h` reaches it: `libchdr_chd.c` guards its zlib code with
   `#ifdef HAVE_ZLIB` near the top, so a build with config.h only compiled the CHD
   reader without zlib. This is why `make` passes each enabled feature on the
   command line as well, and why this project does too.
4. *Four Linux-only subsystems were in the object list.* udev, v4l2, tinyalsa and
   libusb cannot compile against this SDK, and xkbcommon is enabled by this host's
   pkg-config rather than by a compiler flag. All five are now switched off at
   their own configure switches — `HAVE_XKBCOMMON` is declared `auto` in
   `qb/config.params.sh` by the port's second patch, because upstream checks for it
   without declaring it and configure therefore refuses the switch. Three further
   sources (`linuxraw_input.c`, `linuxraw_joypad.c`, `linux_common.c`) are in the
   object list because `Makefile.common` tests `findstring Linux,$(OS)` and
   configure is given `OS=BSD`; they are dropped by `tools/retroarch-sources.sh`,
   which is safe because the toolchain defines `__FreeBSD__` and not `__linux__`,
   so nothing references them.
5. *Two link-time stubs the pipeline refuses.* `assert` expands to `__assert`,
   which only `libc.a` defines and this pipeline deliberately does not link, so
   the build is now `-DNDEBUG` — what a release build of RetroArch uses anyway.
   zstd enables its tracing hooks on any x86-64 ELF and emits a weak undefined
   symbol; the pipeline's stub table refuses to write one for a symbol no public
   stub exports, so `-DZSTD_TRACE=0` stops it being emitted.

**The abandoned route is fully gone.** The tools that belonged to it —
`deploy.py`, `console-launch.sh`, `check-payload.sh`, `scaffold-native.sh`,
`build-native-app.sh`, `install-title.sh` and `deploy.sh` — are deleted, and the
tools that remain are the ones `build-title.sh` and `verify.sh` call. Every tool
named by another tool now exists: `tools/fetch-retroarch.sh` was named by three
tools and three documents and had never been committed, and it now pins
RetroArch at v1.22.2 (`69a4f0e`) and checks the fetched tree against that commit.
`vendor/retroarch` carries no git directory, so upstream cannot be committed into.

**The gates are green for the first time.** `tools/verify.sh` passes format, unit,
build, integration and evidence. Three gate scripts it named had never been
written: `tools/lint-shell.sh` and `tools/lint-format.sh` now exist, and
`tools/check-manifest.sh` verifies the built folder against its manifest. The
template's `make test-unit` built a test for `src/demo_renderer.cpp`, a file that
does not exist here; `tests/test_frontend.py` replaces it with eight checks over
what actually risks being wrong — the driver table's relocations, the frame
layout's arithmetic compiled from `src/display.cpp`, and the artifact's container.

**A generated file stopped dirtying the tree.** `tools/build-retroarch.sh` had
been copying the configured `config.h` to the repository root; no source read it,
and it made every build show a modified tracked file. Untracked and ignored, and
the build is byte-identical without it.

**The evidence.** `bash tools/build-title.sh` → `eboot.bin` 8,026,239 bytes,
`integrity: valid`. Two consecutive builds produce the same digest,
`71c88975040535a02b462618dd394a7a378034272c9bffa43ce0ca4b717ab9b7`. `bash
tools/check-manifest.sh` → 7 files match, none extra, `eboot.bin` magic
`4f153d1d`, and it fails as it should when one byte of the image is changed.
`tools/verify.sh` → `PASS (format unit build integration evidence)`. Staged tree
for the console: `handoff/PPSA99169/`, 8 files.

**What is not done.** The title has not run. It is built, signed and manifested,
and the folder has to reach `/data/homebrew/PPSA99169` on the console. Writes from
this machine do not take: `tools/deploy-title.py` stored a 1,284,674-byte
`libc.prx` and read back the previous 1,335,962-byte file under the new name,
twice, and a probe uploaded under a name never used before read back those same
old bytes. The console's FTP accepts the transfer and serves something else, so
the console's owner uploads by hand.

**Commit.** `eebba81` — Link the title and register the PS5 video driver.

## 2026-09-18: The loop is automated, the crash is fixed, and the menu's silence is explained

**The console loop is one command now.** `tools/run-title.sh` builds, publishes
over FTP, verifies what the console stored, starts the kernel-log listener before
launching, launches, watches, closes the title itself, and prints the title's own
trace. It exists because the hand-driven loop had produced two bad readings: runs
overlapped, so a trace from one launch was read as another's result, and a probe
build was left in place and mistaken for a finding. No round after this one
needs a person to upload anything.

**Verification had to change to match this console.** It converts a signed fake
self into a raw ELF as it stores it, so the file it serves is a different
container from the file sent - the stored image is 8,117,072 bytes against a
signed 8,029,263 and only 12.8% of the bytes agree. A digest comparison therefore
answers the wrong question and was rejecting successful uploads. eboot.bin is now
verified by the strings that only this build carries, which is the same evidence
the debugging used. `sce_module/libc.prx` is the one path this console will not
replace - four uploads, a fresh filename and a full listing all returned the
console's own 1,335,962-byte file - so it is reported and kept, not fatal: the
title runs against the console's copy, and failing on it blocked eboot.bin from
being published at all.

**The crash is fixed, and it was a null joypad driver.** Every joypad driver
upstream ships needs a library or header this SDK does not carry, so
`primary_joypad` is NULL, and `input_joypad_analog_axis` reads `drv->axis` without
checking `drv`. It only runs when the menu is alive, which is why the first pass
through the runloop survived and the second died: SIGSEGV, fault address 0x18,
which is `joypad_info.joy_idx` plus the `auto_binds` member. `patches/series` 0004
returns 0 when there is no driver - the answer the function already gives when
there is no axis to read. The title now runs indefinitely: 1500+ frames in 25
seconds, closed by the script.

**The menu's silence is explained, and it is not the driver.** RGUI initialises,
loads its fonts from the assets now bundled under `assets/rgui/font/`, holds a
320x240 framebuffer, and `rgui_set_texture` is called every frame - but
`GFX_DISP_FLAG_FB_DIRTY` is 0 on every one of those calls, so it returns before
handing anything over and the driver reports `no-menu-source` for every frame.
That flag is set at the end of `rgui_render`, which is only reached through
`menu->driver_ctx->render` under `if (BIT64_GET(menu->state, MENU_STATE_BLIT))`.
The menu's renderer is never running; finding which condition above it is false is
the next step.

**The evidence.** `bash tools/run-title.sh` → built, published with eboot.bin
verified by its own markers, ran 25 s, closed by the script; `/app0/trace.txt`
shows `ps5_frame 1500: no-menu-source 4x4 present=1`, i.e. a healthy loop
presenting frames and no menu pixels. `tools/verify.sh` → PASS (format unit build
integration evidence). Port changes against upstream: `retroarch.c` 10 lines,
`gfx/video_driver.{c,h}` 5, `input/input_driver.c` 6, `runloop.c` and
`menu/drivers/rgui.c` 0 - every probe removed, verified by diff.

**One thing to check later.** `sce_sys/icon0.png` shows as modified and I cannot
account for it: the worktree file and `title/assets/retroarch.png` are both
512x512 but differently encoded, and nothing in this round touches it. It is
committed as it stands rather than reverted, and flagged here so it is not a
silent change.

**Commit.** `4e91e35` — Automate the console loop, fix the null joypad crash, bundle the RGUI fonts.

## 2026-09-18: The RGUI menu is on the console's screen, and the fault was a struct size

**The milestone.** `bash tools/run-title.sh --watch 20` — one unattended command that
builds, publishes, verifies, listens, launches, watches and closes the title — ran
to completion with **no fatal signal**, and the console's owner watched RetroArch's
RGUI menu on the television during it. The title's own trace from that run is
committed as `evidence/ppsa-99169-rgui-menu-on-screen/`: the display opens at
1920x1080, the frontend is told the size, RGUI's 320x240 RGB565 framebuffer arrives
through `poke->set_texture_frame`, and the driver presents it every frame by
alternating the two registered buffers (`display: flip 1200 of buffer 1, status=0
marker=1`).

**Two faults had to go, and they were independent.**

The first was that **the title and the frontend disagreed about the size of
RetroArch's driver interface struct**. `src/` was compiled with no `-DHAVE_*` flags
while the archive was compiled with fifty, so `HAVE_OVERLAY` and `HAVE_GFX_WIDGETS`
were off on one side only: `video_ps5` was 136 bytes where the frontend read 144, and
every member after `overlay_interface` was read one slot late. `poke_interface` came
back NULL, the frontend therefore never called it, and RGUI's hand-over was dropped
in silence on every frame while the driver happily presented 1500 frames of its own
probe pattern. `tools/build-title.sh` now asks `tools/retroarch-flags.sh` for the
frontend's own defines and passes them to the title's sources, and writes the
configured tree's `config.h` where RetroArch's headers look for it relative to the
repository root. `tests/test_frontend.py` gained a class that compares
`sizeof(video_driver_t)`, compiled with the frontend's defines, against the size of
`video_ps5` in the object the title links, and checks the member at the frontend's
`poke_interface` offset against the table's relocations; built without the defines it
fails with the two numbers, which was verified by doing it.

The second was that the port initialised the **GPU command processor**
(`sceAgcInit(8)`) before opening the display, left over from the round that submitted
flips through an AGC command buffer. `../PS5_Vulkan/src/demo_renderer.cpp`, whose
output has been seen on this console, contains no `sceAgc` call at all. Removing it
produced the first pixels this port ever put on the screen.

**A correction to the lead this round started from.** The hypothesis was that
returning into RetroArch's runloop after a flip was what undid the frame, because the
working reference blocks forever after its one flip. That is now measured false: with
the probe's frame held for 8 seconds inside `present()`, the bands appeared
immediately - before the hold could be the reason - and were still on screen twelve
seconds after `present()` had returned into the runloop, with the display's flip
status reading `marker=1` for every one of the sixteen samples taken during the hold.
The runloop does not take a presented frame back. The two candidate causes went in
together in one build, so what the run is evidence for is the pair's effect and the
elimination of the runloop; the AGC call is what remains with no other candidate.

**What also changed in the driver, and why.** `present()` is a real driver now: it
flushes the back buffer, submits a flip to that buffer's registered index, waits a
vblank, and alternates buffers so the next frame is drawn into the one the display is
not reading. The probe paint, the readback and the hold are gone. The core's frame is
4x4 because upstream hardcodes a dummy frame when no game is loaded
(`video_driver.c` sets the cache to 4x4 for exactly that case), so the menu's
framebuffer takes precedence when it exists and the bands remain underneath as the
instrument that says "the display is alive but the menu is not drawing".

**Verification.** `bash tools/verify.sh` → **PASS (format unit build integration
evidence)**, including the 11 host tests and the three replayed evidence records.
The probes are gone from the configured tree: `build/ra-conf` was deleted,
regenerated from `vendor/retroarch`, and diffed - only the four files
`patches/series` names differ from upstream.

## 2026-09-18: The pad driver is written and registered, and two upstream faults stand in front of it

**What was built.** `src/input_ps5.cpp` is a complete input driver for this console:
the pad is read with `scePadInit`/`scePadOpen`/`scePadRead` and the 120-byte sample
layout that `../ProsperoLight` verified on hardware, the pad's button words are
mapped to RetroArch's own numbering (CIRCLE is its A, so CIRCLE confirms), sticks
and triggers are reported as axes, and the table is registered in
`input_drivers[]` before `input_null`. `tests/test_frontend.py` pins the
registration with a relocation test and the button pairing by reading the map out of
the object the title links, so neither can drift quietly.

**Two faults were found in front of it, both upstream, both fixed.**

The first is that RetroArch's built-in test input driver is on by default
(`HAVE_TEST_DRIVERS=yes` in `qb/config.params.sh`), and with it on
`video_driver_init_input` returns immediately whenever the configured driver is not
`"test"` - so no input driver is ever initialised and every button reads 0. That is
now disabled at configure time.

The second is that the title's `-c /app0/retroarch.cfg` never reached RetroArch:
content loading rebuilds argv from the frontend's environment and keeps only
`["retroarch", "--menu"]`. The trace shows it plainly -
`probe config_parse_file: path="(null)"` and `probe config_load: after parse
input="null" video="ext"` - which means this title has been running on compiled
defaults since the day it first built, with its config file unread and `--verbose`
dropped. A one-line guarded fix restores the title's own config path.

**And that fix is parked, because it exposes a crash I could not finish.** With the
config actually read, the title dies on launch with `SIGSEGV`, `rip=0` - a call
through a null function pointer - before `ps5_input_init` is entered and also with
`input_driver="null"` configured. Markers through `drivers_init` and
`video_driver_init_internal` show the entire video path completing, so the crash is
after driver initialisation, in the runloop or the content task. The change is in
`parked/config-path.patch.py` with its reasoning; `config/retroarch.cfg` keeps
`input_driver = "null"` so the title continues to run and show its menu.

**Honest position.** The pad does not work yet, the picture is still one menu frame
that only redraws when something changes (which is what input is for), and the
"frozen frame" question cannot be settled until input lands. What is solid: the
driver exists, is registered, is unit-tested, and the two faults that were silently
blocking every input path are named with their measurements.

**Verification.** `bash tools/verify.sh` → PASS (format unit build integration
evidence), 13 host tests. `bash tools/run-title.sh --watch 12` → title runs, menu
visible, no fatal signal, `/app0/trace.txt` shows the same hand-over as before.

## 2026-09-18: The config-path crash is reading the config, not any setting in it

**Four runs, one change each, and the crash follows the `-c`.** With `-c` in the
argument list the title dies with `SIGSEGV`, `rip=0`; with `-c` removed it runs and
shows the menu. Dropping `--verbose` does not help, and dropping `-f` does not help,
so neither of the two settings that would newly *take effect* is the cause - it is
the config file being read at all. That is a narrower claim than the last entry
could make.

**The config parses.** Marking `config_load` before and after shows both probes, so
defaults, file parse and `config_load_file` all return. The crash is after the read
and before the first frame: no `ps5_frame 0`, and neither `runloop_iterate` site is
reached. It is inside `retroarch_main_init`, between the config load and the first
frame.

**What remains ruled out.** `drivers_init` completes (overlay unload/init, context
reset, display server, mouse cursor, audio init, core info all mark), and
`ps5_input_init` is never entered, so the input driver is not involved. What is left
in that stretch is the driver lookups, and that is where the next marker goes:
`audio_driver_find_driver`, `video_driver_find_driver`, `input_driver_find_driver`,
`camera_driver_find_driver`, `menu_driver_find_driver`, each indexing a table this
build has stripped to almost nothing.

**A process note that cost this round's last attempt.** Reading a marker line into
the middle of a multi-line call split the call and produced
`undefined symbol: rarch_main` at link time. Marker placement is an edit, not a
substitution: the anchors have to be statements, and the brace and call structure
has to be checked after every insertion. The input driver stays built, registered
and unit-tested, and the title stays in its working state while this is chased.

## 2026-09-18: The config crash is in command_event, and the log route is closed for it

**Narrowed.** With the config-path fix applied, markers bracket the crash to
`command_event()` before its switch reaches `CMD_EVENT_CONTROLLER_INIT`: the marker
directly before that call prints, and three markers after it - the first instruction
of `command_event_init_controllers`, the case label, and the statement following the
call - never do. The register dump is identical across every config-loading run
(`rip: 0`), so it is deterministic.

**A real guard, kept without pretending it fixed anything.** `patches/series` 0007
null-guards the core's `retro_set_controller_port_device` callback, which upstream
calls unguarded and which only `dynamic_dummy.c`'s empty stub makes survivable. It
is verified present in the object and the crash is unchanged, so the commit message
and `docs/FINDINGS.md` both say so.

**File logging: configured, inert, and the reason is structural.** The logger is
initialised after the config is parsed, and this crash happens before that, so the
log cannot catch it. `log_to_file`, `log_to_file_timestamp` and `log_dir` are now in
`config/retroarch.cfg` for the day the config is read - they need no build flag,
since `rarch_log_file_init` is compiled unconditionally.

**State.** The title runs (menu up, no fatal signal, all five gates pass). The
config-path fix (0006) stays parked in `parked/config-path.patch.py`, the guard
(0007) is in `patches/series`, and `input_driver` is back to `"null"` because it is
only reachable through the config file. Two self-inflicted process faults are
recorded in `docs/FINDINGS.md`: a text-sliced patch park that emptied the parked file
and desynced the script from the tree (recovered with `git checkout`), and probe
lines inserted into a multi-line `#if` block that broke the link.

## 2026-09-18: The pad works, the menu is live, and the pad's absence was one comparison

**The milestone.** `bash tools/run-title.sh --watch 20` with the console owner working
the pad: the title runs, the pad is read, and the menu responds. From the title's own
trace on the shipping build:

    input: pad opened, user=515310723 handle=51119872
    input: press, pad=0x00000040 retropad=0x00000020     DOWN
    input: press, pad=0x00004000 retropad=0x00000001     CROSS  -> RetroPad B
    input: press, pad=0x00002000 retropad=0x00000100     CIRCLE -> RetroPad A
    menu: framebuffer commit 2 is a new picture (2 of 2 changed so far)
    ps5_frame 600: menu commits=76 changes=14 presented=yes

Both halves of the round's goal are answered. Input works, with the mapping a
PlayStation player expects - CIRCLE confirms, CROSS cancels. And the picture is not
one frozen frame: 76 framebuffer commits, 14 of them a different picture, against 1
and 1 in every earlier run. The image was never frozen; the menu had nothing to
redraw for, which is exactly what input supplies.

**The fault was one comparison.** `ps5_input_init` had never run, and the reason was
not the driver, not the registration, and not the test-driver flag alone.
`video_driver_init_input` opens with `if (*input) return true;`, which upstream means
for a video driver that pre-initialised an input driver of its own - and the tell is
`tmp`. `video_driver_init_internal` assigns `tmp = current_driver` *before* calling
`video_driver_find_driver`, so after the pre-initialisation pass selects a driver,
`tmp` is that same selection. Measured: `probe INV: entered tmp=ba41e0 *input=ba41e0
configured="ps5"`. The early return therefore fired for a case upstream never
designed for, and the wrap below was dead code. My first attempt - clearing the
selection when `tmp == NULL` - never fired, because `tmp` is not NULL; the fix that
works is `patches/series` 0009's `if (*input != NULL && *input == tmp)`, three lines
that say what they mean.

**The probes are kept.** They live in `tools/apply-runtime-probes.py` now, applied
with one command and reverted with `--revert`, because a rebuild wipes any probe
written into the configured tree - which is how the set that found this was lost
mid-round. They are not part of the shipping build: `tools/build-title.sh` does not
call that script.

**Still open, and unchanged by this.** The config file is still not read: 0006 is
parked in `parked/config-path.patch.py` because reading the config crashes the launch
in `command_event`. That is the next task, and the probe set now covers the landmarks
around it.

**Verification.** `bash tools/verify.sh` → PASS (format unit build integration
evidence). `bash tools/run-title.sh --watch 20` → pad working, menu redrawing, no
fatal signal.

## 2026-09-18: A duplicate patch block cannot reach a build again

**The fault this closes.** The working copy of `tools/apply-port-patches.py` had grown
to fourteen blocks against the committed ten: the controller-port guard was pasted in
twice, so `runloop.c` was patched twice, and every build from that tree died with
`rip: 0`. The block read correctly and the script reported "applied" for both, so
nothing about reading it revealed the duplicate - and the crash it produced was chased
as an unrelated bug for most of a round.

**The check is mechanical now.** `tests/test_frontend.py` gains a `PortPatches` class
that parses `EDITS` and asserts: no two blocks share a `(file, anchor)` pair - which
is precisely what makes an edit apply twice; the patch count is the pinned ten, so an
addition or removal is a deliberate diff; and `input/input_driver.c` carries its three
distinct edits. A duplicate was planted to prove the test fails on it (11 != 10, two
failures) and removed again to prove it passes.

**On purging the patch set: no, and the enumeration is the argument.** Of the ten
blocks, eight are load-bearing - video driver registration (2), the `main` rename,
input driver registration (2), the input-init fix, the compiled input default, and the
null-joypad guard. Purging them would remove the display and the pad. Only the
`HAVE_XKBCOMMON` declaration and the controller-port guard are dead weight, and both
are cheap. The set was instead verified from a pristine extraction: ten blocks, no
duplicate pairs, three legitimate edits to `input_driver.c` at different anchors.

**State.** `bash tools/build-title.sh` from a wiped tree, then
`bash tools/run-title.sh --watch 12` -> title runs, no fatal signal;
`bash tools/verify.sh` -> PASS; 16 host tests.

## 2026-09-18: The driver is linked into the title, and the link completes

**The step.** `../PS5_Vulkan`'s driver stops being a library RetroArch loads at run
time and becomes ordinary symbols in `eboot.bin`. The alternative was measured and
closed in `0cc661d`: a PS5 title cannot dlopen a driver. This is the commit that makes
the replacement actually link.

**What the first link said.** Twenty-four undefined symbols, and the count is only
knowable because the link line now carries `--error-limit=0`; before that lld stopped
at twenty and the tail of the list was invisible. The 24 are three unrelated problems:

- **Four `__eh_frame_*` boundaries** (`__eh_frame_start/end`,
  `__eh_frame_hdr_start/end`). Their shader compiler is C++ and links the SDK's
  libunwind, which finds the unwind tables through these boundary symbols rather than
  through `dl_iterate_phdr`. `../PS5_Vulkan/tooling/psbc/ps5-pie-unwind.ld` defines
  them; this project's `tooling/native/ps5-pie.ld` is byte-identical to the
  `ps5-pie.ld` that script includes, so the same four `PROVIDE`s were added to it
  rather than adopting their `-T` path.
- **Eight Mesa utility symbols** from `u_queue.ps5.o`, `os_memory_fd.ps5.o` and
  `ac_spm_config.ps5.o`: `u_thread_create`, `u_thread_setname`,
  `util_barrier_init/destroy/wait`, `util_thread_get_time_nano`,
  `os_create_anonymous_file`, `os_read_file`. Their
  `toolchain/Makefile.opengnm-psbc-ps5` filters `src/util/{u_thread,anon_file,os_file}.c`
  out of `UTIL_SRCS`, and `tools/build-psbc-ps5.sh` compiles only a five-file support
  list, so nothing in that tree defines them. Their own `libvulkan.so.1` links because
  a shared object may leave symbols undefined; a title's link may not. Mesa's own
  definitions are compiled here (`tools/build-mesa-util.sh`) with their PS5
  configuration rather than reimplemented, because `u_thread.c`'s `util_barrier` and
  its `mtx_t`/`cnd_t` come from that tree's `c11/threads.h`, the same header the
  `u_queue.ps5.o` inside the archive was compiled against. Two of the three needed a
  flag that configuration does not set: `HAVE_PTHREAD_NP_H` for
  `pthread_set_name_np`, and for `anon_file.c` the `-D_XOPEN_SOURCE=700` drop that
  `tooling/psbc/support.mk` already documents for `usleep` and `getprogname`, because
  `SHM_ANON` is declared under `__BSD_VISIBLE`. `os_file.c` needed `-D__ORBIS__`, the
  same switch their mak uses for `futex.ps5.o`: its FreeBSD branch walks the kernel's
  file table through `sysctl(KERN_FILE)` and a `kvaddr_t` the SDK does not have.
- **Twelve `sceAgc*` entry points.** The console provides libSceAgc and
  libSceAgcDriver and the SDK stubs neither. The declarations come from
  `../PS5_Vulkan/vendor/ps5/sdk/stubs/agc_canary_link_stub.c`, signatures unchanged,
  because those are the imports that project's own titles already record.

**The stubs moved.** They had been edited in `vendor/ps5/sdk/stubs/`, which
`.gitignore` excludes, so the fix would have built here and vanished at the next
checkout. They are `tooling/ps5-stubs/agc_link_stub.c` and
`tooling/ps5-stubs/agc_driver_link_stub.c` now, and `tools/build.sh` reads them there.

**Evidence.** `bash tools/build-title.sh` -> exit 0, no undefined symbols,
`dist/PPSA99169/eboot.bin` 29,859,722 bytes against 8,225,706 before: 21.6 MB of
driver and shader compiler. `bash tools/verify.sh` -> PASS (format unit build
integration evidence). `python3 -m unittest discover -s tests` -> 18 tests, two new:
every `sceAgc*` the three driver archives reference is declared by the stubs (20
referenced, 21 declared, none missing), and the linked `build/llvm-pie.elf` defines
the four `__eh_frame_*` symbols - the check is on the image rather than on the script,
so regenerating the script from the boilerplate fails the test instead of the link.

**Not proven.** The driver has not been on the console since it was linked. The next
run is `bash tools/run-title.sh --watch 20` and the answers are in
`/app0/retroarch.log`.

## 2026-09-18: First console run of the linked build: it returns 1 before video init

**What was run.** `bash tools/run-title.sh --watch 20` against the build of `17c44d8`
(the driver linked, `eboot.bin` 29,859,722 bytes). The console reports the title
running, and it is gone before the twenty-second watch ends.

**What the console says.** Eight attempts, each exactly this and nothing else:

    main() entered; static constructors have already run
    argv built: retroarch -f -c /app0/retroarch.cfg --verbose --log-file
    rarch_main returned = 1

No `ps5_init entered`, no frame, no menu, and no fatal signal - `main` returned by
itself, which is the one failure the title's own trace can show but not explain.

**What the frontend's own log says.** Fetched over FTP; `/app0/...` is not an FTP
path, the file is `/data/homebrew/PPSA99169/retroarch.log`, which is worth writing
down because two `RETR /app0/retroarch.log` attempts answered `550` first. It stops
after the audio fallback:

    [INFO] [Input] Found input driver: "ps5".
    [INFO] [Video] Set video size to: fullscreen.
    [INFO] [Video] Graphics driver did not initialize an input driver. Attempting to pick a suitable driver.
    [INFO] [Video] Found display server: "null".
    [ERROR] Failed to initialize audio driver. Will continue without audio.

Two facts follow, and both narrow it. The log reaches `drivers_init` and gets past the
video block, so the video driver was found and its own init did not fail loudly; and
`rarch_main`'s setjmp handler, which logs `Fatal error received in: "<error_string>"`,
never ran - so no `retroarch_fail` fired and the return is one of the silent `return 1`
paths later in `rarch_main`. The first of those is
`task_push_load_content_from_cli` (`retroarch.c:6036`), reached after `drivers_init`
returns, which is also where the parked 0006 lives: content loading is the code that
discards the title's `-c`.

**Why `ps5_init` is absent, and it is not a regression.** The compiled video default is
`vulkan` since `6f4bd10`, and the config that names `ps5` is the config content loading
discards, so the ps5 driver is never selected. The last run that reached the menu
(`ps5_frame 600: menu commits=22 changes=2 presented=yes` in the trace at block 1714)
is the build before that default changed, which is why it looks like a step backwards
and is not one.

**Recorded captures.** `klog/console-trace.txt` (the whole append-only trace, 1762
lines, the eight attempts at 1741-1762) and `klog/console-retroarch.log` (24 lines,
stderr and stdout of the frontend's logger). Both stay in the ignored `klog/` tree;
what they said is transcribed here.

**Next.** Statement-level probes, through `tools/apply-runtime-probes.py` so they stay
revertible: inside `drivers_init` after the audio block, and around `rarch_main`'s
content-load push. Which one fires decides whether this is a Vulkan-init problem or the
content-load path 0006 already describes.

## 2026-09-19: The Vulkan driver initialises on the console, and where it stops

**The result, in one line.** A `tools/run-title.sh` run now shows RetroArch
selecting `video_vulkan`, creating a device, a swapchain and its textures, compiling
the stock shader's SPIR-V, and then having its pipeline refused by the driver's own
console AGC call. Nothing renders yet. Everything before that refusal is new.

**What the trace shows**, in order, from `/app0/trace.txt`:

    probe VDRV: configured="vulkan" selected="vulkan"
    probe VK: init entered 960x720 rgb32=0
    probe VK: context driver=2003a58 ident="khr_display"
    probe IMG: 4x4  type=1 fmt=37 tiling=0 usage=0x7 | optimal=0xdd81
    probe IMG: 512x512 type=1 fmt=37 tiling=0 usage=0x7 | optimal=0xdd81
    probe IMG: 512x512 type=1 fmt=37 tiling=0 usage=0x7 | optimal=0xdd81
    probe IMG: 1x1  type=1 fmt=37 tiling=0 usage=0x7 | optimal=0xdd81
    probe BUF: create size=128 usage=0x80
    probe BUF: bound memory=880083980
    probe REFL: entered, vertex=273 words fragment=196 words
    probe CHAIN: creating vertex module, 1092 bytes of SPIR-V
    probe CHAIN: both modules created; creating pipeline
    probe PIPE: vkCreateGraphicsPipelines -> -13 (pipeline=0)

**Five frontend faults stood between the link and this**, each one a request the
driver's own checks refuse, and each fixed in `patches/series` rather than in that
project (0013, 0014, 0015):

- the swapchain asked for `TRANSFER_SRC|TRANSFER_DST|SAMPLED` beside
  `COLOR_ATTACHMENT`, and the surface advertises the one bit - now masked with
  `supportedUsageFlags`, which is the specification's rule anyway;
- the 4x4 blank and 1x1 default textures are created as `B8G8R8A8_UNORM`, whose
  entry in that driver's table is colour-attachment-only - the create-info is now
  masked to the format's advertised features, and a format that cannot be sampled
  is replaced by `R8G8B8A8_UNORM`, the same image for a uniform colour;
- `vulkan_format_to_bpp()` did not know `R8G8B8A8_UNORM`, so the staging buffer
  sized from it was zero bytes long.

**One frontend file had to go.** `src/video_filters_stub.cpp` defined the whole
Vulkan filter chain as NULL-returning stubs, from when this port had video filters
off; `src/` is linked before the archive, so the stubs won and the driver read "no
chain" - no pipeline was ever created, and nothing in the log or the trace said so.
Removing it pulled in glslang and SPIRV-Cross, and with them FreeBSD's xlocale
interface: `src/locale_shims.c` provides the thirty-four `_l` functions the console
SDK does not export, each the C-locale answer its unsuffixed counterpart gives.

**The remaining refusal is console-only.** Both stock shaders compile cleanly
through the host build of that project's compiler, with the driver's own options -
`build/host/opengnm-psbc-probe` on the extracted SPIR-V, UBO stride 16 and the
sampler at binding 2 stride 48, vertex in both `--ngg` and `--raw` modes. What the
host cannot reproduce is the next step in `ps5vk_graphics_pipeline_create`
(driver/ps5vk_pipeline.c): `sceAgcCreateShader` and `sceAgcLinkShaders` on the
console, whose failure is the `vk_errorf(..., "AGC shader creation or linking
failed: 0x%08x", result)` that returns the VK_ERROR_UNKNOWN RetroArch sees. That
driver's own log is compiled out - `src/vulkan/runtime/vk_log.c` guards it with
`#if !MESA_DEBUG` - so the `result` code has to be read from their side.

**Instruments added, and kept.** `tools/build.sh` now links with `--error-limit=0`
and `--Map`; `tools/symbolize-crash.py` turns a console backtrace into names using
that map (this is what identified every refusal above, since `--exclude-libs`
leaves the driver, the compiler and the SDK runtime with no symbol table);
`src/main.cpp` points stderr at the trace file and installs a terminate handler
that names an uncaught exception's type. `tools/apply-runtime-probes.py` carries
the probes that measured all of it - twenty-nine, applied with one command and
reverted before the shipping build.

## 2026-09-19: The driver initialises, the pipeline is created, and the first frame stops on an open render pass

**Where this round started.** A `tools/run-title.sh` run showed RetroArch selecting
`video_vulkan`, creating a device, a swapchain and four textures, compiling both stock
shaders into modules - and then `vkCreateGraphicsPipelines` answering
`VK_ERROR_UNKNOWN`, with `vulkan_init` returning NULL. Nothing rendered.

**What it took, in order.**

- **The topology.** `../PS5_Vulkan`'s pipeline check accepts
  `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST` alone (and Mesa's meta rectangle list), and its
  draw path refuses a non-indexed draw whose first vertex is not zero. RetroArch's
  Vulkan filter chain drew its two quads as a four-vertex triangle *strip*. Patches
  0016 converts them to six-vertex lists, says triangle list, moves the final pass's
  quad to the new offset, and draws each quad's second triangle through the binding's
  own offset. `vkCreateGraphicsPipelines` then returned 0 and `vulkan_init` handed back
  a real pointer.
- **Samplers.** This driver refuses any sampler whose address mode is not clamp-to-edge
  and leaves the output handle untouched; `CommonResources` destroys every handle that
  is not `VK_NULL_HANDLE`, so the sixteen refused ones were destroyed as if they were
  real samplers and the driver asserted on the first. Patch 0017 clears the array first.
- **The runloop quit before its first frame.** `rarch_main` returned 0 with no frame
  drawn. The display context's `check_window` sets `quit` when the frontend's signal
  handler state is non-zero, and RetroArch's khr_display context read that as "the user
  asked to quit". Patch 0018 stops this context treating it as one: a console title has
  no terminal and no SIGTERM sender, and the platform's own lifecycle ends the process.
- **The value behind that state was garbage, and that is the round's real find.** It
  read `-285230512`, stable across runs, before any signal had been delivered - because
  **this port's CRT never zeroed the BSS**. The image's writable segment is
  `0x104b4` bytes in the file and `0xc7040` in memory, and `_start`
  (tooling/native/app_crt.cpp) went straight from `_init_env` to the static
  constructors. Every zero-initialised object in the frontend, in the driver and in the
  shader compiler therefore started with whatever the memory held: a counter, a
  flag, a pointer. `tooling/native/ps5-pie.ld` now marks `__bss_start`/`__bss_end` and
  `_start` clears that range before anything else. A run prints
  `bss check=0 (must be 0), data check=7 (must be 7)`.
- **Assertions now say what they are.** `../PS5_Vulkan`'s `__assert` prints the
  expression, file and line to stderr and then calls `abort`, which does not flush -
  and the console's libc buffers stderr, so every assertion arrived as a bare
  `abort is called(system)`. `src/main.cpp` points stderr at `/app0/trace.txt` **and
  makes it unbuffered**. The trace now reads, for the current blocker:

      probe SPAN: offscreen passes
      probe SPAN: menu upload
      probe SPAN: about to begin the backbuffer pass
      probe REC: begin render pass
      assertion failed: cmd_buffer->render_pass == NULL
        (.../vulkan-runtime/src/vulkan/runtime/vk_render_pass.c:2648,
         vk_common_CmdBeginRenderPass2)

**Where it stands.** The driver initialises, both shaders compile, the pipeline is
created, the runloop runs, and the first frame records commands until
`vkCmdBeginRenderPass` - which asserts because the frame's command buffer already has
a render pass open. The marks bracket it to the span between
`vulkan_filter_chain_build_offscreen_passes` and the backbuffer pass, which is the
first-frame history/feedback clear and the menu texture upload. Nothing reaches the
screen yet.

**Probes and their guard.** `tools/apply-runtime-probes.py` carries thirty-nine probes
and `tests/test_frontend.py` now checks the set itself: an insert that ends with its
own anchor duplicates the line it is inserted before, which broke the build three times
in this round (a duplicated `vkCreateImage`, a duplicated `switch (`, a duplicated
`vulkan_filter_chain_build_offscreen_passes(`); every probe must carry a marker, a
note and a trailing newline. All of it is checked before a probe can reach a build.

## 2026-09-19: Two rounds of "a render pass is already open" were this port's own probe

**The correction.** The blocker chased through round 1 and the start of round 2 - the
driver asserting `cmd_buffer->render_pass == NULL` in `vkCmdBeginRenderPass` - was not
the driver's and not RetroArch's. A probe insert had **duplicated the
`vkCmdBeginRenderPass` call**: the tool writes `insert + anchor`, the insert ended with
the anchor line, and the tree ended up with the statement twice, so the *second* call
found the pass the first had just begun. A duplicate-detector run against
`vendor/retroarch` found five such pairs - `vkCmdBeginRenderPass`, `vkCmdEndRenderPass`,
`vkEndCommandBuffer`, `vulkan_filter_chain_end_frame`, and the `if (quit)` pair from
the ALIVE probe. Restoring the file from `vendor/retroarch` and re-applying the port
patches removed all of them.

**The guard is stronger now.** `tests/test_frontend.py`'s `ProbeSet` checked that an
insert does not *end* with its anchor; it now checks that the insert does not *contain*
the anchor at all, which is the property that matters. It caught one more of the same
mistake in the SIGNAL probe while this was written up.

**What the real blocker is.** With the duplication gone, a run reaches the frame's
whole recording - the backbuffer pass begins, the chain binds and draws, the quad's
second triangle is drawn through the binding offset - and then `vkQueueSubmit` asserts
with `cmd_buffer->state` neither INITIAL, EXECUTABLE nor PENDING. That state is what a
**recording refusal** leaves (Mesa's `vk_command_buffer_set_error`), so the draw itself
is being refused. `../PS5_Vulkan`'s draw path refuses in three places, and two of them
are silent in a build without its log:

- `pipeline->draw_refusal` - a string computed when the pipeline is created;
- `ps5vk_pipeline_prepare_shaders` - the AGC `sceAgcCreateShader`/`sceAgcLinkShaders`
  step, which that driver runs at the **first draw**, not at pipeline creation;
- the viewport/scissor count check, which this pipeline passes (one of each is set).

**Why the message cannot be read from here.** `vk_log.c` drops every message unless
`MESA_DEBUG` is set at compile time or the instance has debug logging on or a debug
callback installed:

    if (unlikely(!instance) ||
        (likely(!instance->enable_debug_logging) &&
         likely(list_is_empty(&instance->debug_utils.callbacks)) &&
         likely(list_is_empty(&instance->debug_report.callbacks))))
       return;

and `enable_debug_logging` is never assigned anywhere in that tree. So the two ways
forward are a debug-utils messenger created from this side, or that project's own
result code.

**Verified.** `bash tools/verify.sh` PASS (format unit build integration evidence);
20 unit tests; `strings build/llvm-pie.elf | grep -c 'probe DRAW|probe REC|...'` = 0
after deleting `build/ra/obj`, which a probe revert needs to take effect.

## 2026-09-19 - the Vulkan driver draws: 644 frames in one unattended run

Run: `bash tools/run-title.sh --no-build --watch 25` ->
`VERDICT: it ran for 25s and this script closed it` (`klog/run-PPSA99169-022103.log`).
The trace (`klog/trace-r3o.txt`, fetched over FTP from `/app0/trace.txt`) counts
644 x `probe REC: frame entered`, 643 x `probe QUAD: menu quad`, 180 x `probe TRI`, and
40 driver refusals - all of them at init (21 triangle strips, 16 sampler address modes,
3 storage-image compute uploads), none inside a frame. `vkQueueSubmit`'s assertion,
which had ended every earlier run, does not appear.

Then the probes were reverted, `build/ra/obj` removed, the frontend rebuilt (276 of 276
sources), and the shipping image run the same way: `klog/run-shipping-r3.log` ->
`VERDICT: it ran for 20s and this script closed it`, `klog/run-PPSA99169-023553.log`
with `grep -c 'fatal signal'` = 0. `bash tools/verify.sh` -> `PASS (format unit build
integration evidence)`.

The four patches this step needed, each one named by a refusal the driver logged once
patch 0019's `VK_EXT_debug_utils` messenger was in place:

- 0024: `vulkan_init_samplers` asked for `VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE`, and
  `../PS5_Vulkan`'s `ps5vk_CreateSampler` creates no sampler for any border colour but
  transparent black, so all four display samplers stayed `VK_NULL_HANDLE`. A NULL
  sampler is the descriptor write the driver refuses ("names no sampler"), and one
  refused draw ends the frame.
- 0025: any sampler the driver still refuses now falls back to `vk->samplers.nearest`
  instead of reaching a draw as NULL.
- 0026: the display layout declares binding 2 as a second combined image sampler (its
  HDR shaders read their source there) and the driver requires a write for **every**
  binding a stage's metadata names, so the quad descriptor writer writes the same image
  at binding 2. `PS5VK_PUSH_CONSTANT_BINDING` is 32, not 2 - the reserved
  push-constant binding was ruled out by reading `psbc_compile.h`.
- 0027/0028: the menu texture took the B4G4R4A4 path with a B/R swizzle in the view, and
  the driver samples only identity component mappings. It now takes the 32-bit path the
  device-without-B4G4R4A4 branch already implemented, in `R8G8B8A8_UNORM` (the format
  this driver reports as sampled, so the staging texture and its destination agree and
  the upload stays a copy), with the CPU conversion's channels in that order.

One diagnostic was tried and dropped: a probe in
`gfx_ctx_khr_display_swap_buffers` (the swap-buffers/acquire path) made the run die in
`vulkan_acquire_next_image` with `abort is called(system)` - the frames had run without
it, so it was removed rather than chased (`klog/run-PPSA99169-022734.log`).

Still refused, and named here so the next step does not rediscover them: the frontend's
first-frame clears are triangle strips, the blank texture's upload takes the compute
path (storage-image descriptor, no table entry in the driver), and the filter chain asks
for sampler address modes the driver has no word for (patch 0023 makes the chain use its
clamp-to-edge entry instead).

## 2026-09-19 - an unattended argument file, and screenshots compiled in

The goal's last gap is a picture of what the console was handed, and the port had no
capture path at all. Two things were missing, both now in place and both verified on the
console:

- `src/main.cpp` reads `/app0/args.txt` when that file exists - one extra argument per
  line, blank lines and `#` comments skipped, stored statically because RetroArch's
  option parsing keeps pointers into argv for the whole run. Evidence:
  `probe ... "argv extras from /app0/args.txt = 3"` in the trace
  (`klog/trace-r4.txt`) after staging three lines over FTP; without the file nothing
  changes.
- `tools/build-retroarch.sh` now defines `HAVE_SCREENSHOTS`. RetroArch's
  `--max-frames=N --max-frames-ss --max-frames-ss-path=FILE` is entirely inside
  `#ifdef HAVE_SCREENSHOTS`, so on this build the options did not exist and a run had
  nothing to write. With the flag the frontend compiles and links 276 of 276 sources and
  the title runs (`klog/run-shot-r4b.log`: "it ran for 20s and this script closed it").

The capture still does not fire, and the reason is precise: `runloop.h`'s
`RUNLOOP_TIME_TO_EXIT` compares `runloop_state.max_frames` against `frame_count`, which
is the **core's** frame count. This title loads no content - the menu is the whole
program - so that counter never advances and frame 200 never arrives (three runs with
`--max-frames=200` all ran the full watch window and wrote no `shot.png`). Next step: a
port block in `runloop.c` that counts the frontend's own frames for that comparison when
`--max-frames-ss` is asked for, then read the file back over FTP and look at it.

## 2026-09-19 - the capture needs a hook the menu-only path reaches

The screenshot still does not happen, and this round narrowed it to a single fact. The
runloop's `--max-frames` block compares `frame_count`, which is refreshed from
`video_st->frame_count` - and that counter is only incremented by `video_driver_frame`.
With no content loaded this title draws the menu through `video_driver_cached_frame`, so
the counter never advances and the block cannot be reached by a frame budget.

A port patch was written to test exactly that (0029: a static counter of the runloop's own
iterations, folded into `frame_count` just before `RUNLOOP_TIME_TO_EXIT`), the frontend
rebuilt (276 of 276 sources) and deployed, and three console runs were made with
`--max-frames=120`/`200 --max-frames-ss --max-frames-ss-path=/app0/shot.png` staged in
`/app0/args.txt`:

- `klog/run-shot-r5.log`, `klog/run-PPSA99169-030553.log`: "it ran for 20s and this script
  closed it" - nothing exited at the budget;
- no `shot.png` in the title folder (listed over FTP);
- `/app0/retroarch.log` still the same 1200 bytes.

So the block is not reached at all on a content-less run: the menu-only path returns
before it. The patch was **removed** rather than committed, because it had no verified
effect - the next attempt has to hook a path that runs without content.
`gfx/video_driver.c`'s `video_driver_cached_frame` is that path (it is what draws each
menu frame), and `take_screenshot` is `tasks/task_screenshot.c`'s, so the hook is a
counter there plus a call to it, with the quit the same way the max-frames path quits.

Everything else this round is unchanged from the last: 644 frames and 643 menu draws in an
unattended run, the probe-free image alive for its whole watch window, and
`bash tools/verify.sh` PASS (format unit build integration evidence).

## 2026-09-19 - the capture fires; the driver's readback is what fails

Two hooks were tried for the frame budget the screenshot needs, and the difference
between them is the finding:

- `video_driver_cached_frame` (gfx/video_driver.c) - the natural place, and **not called**
  in a content-less run: the hook never ran on the console, so that patch was removed
  rather than committed.
- `vulkan_frame` (gfx/drivers/vulkan.c, patch 0031) - the one path every frame takes, and
  the trace now says so on every capture run:

      capture: frame 90 of 90, take_screenshot -> failed (/app0/shot.png)

So the port's own options work end to end: `/app0/args.txt` carrying
`--ps5-capture=90` and `--ps5-capture-path=/app0/shot.png` is read by the patched
`vulkan_frame`, the count reaches the budget, `take_screenshot` is called with the path,
and the result is written to the trace either way. `src/main.cpp` keeps `--ps5-*` lines
away from RetroArch's option parser. Run: `klog/run-shot-r6b.log`,
`klog/run-PPSA99169-031642.log`.

What fails is the readback behind `take_screenshot` - the frontend's screenshot writer
asks the video driver for the frame, and `vulkan_readback`'s synchronous path (blit the
backbuffer into a staging texture, `vkQueueWaitIdle`, map it) returns false. That is the
next step: its refusal is logged to `/app0/retroarch.log`, which is the file that never
flushes, so the first move is to get that message out - the driver's debug messenger is
already installed, so a readback refused by `../PS5_Vulkan` would name its reason in the
trace once the readback runs inside a trace-visible path.

The options this needs are the frontend's own and stay that way: a run without
/app0/args.txt takes no picture and behaves exactly as before (verified by the same
round's runs), and `bash tools/verify.sh` is PASS (format unit build integration
evidence) with the capture patch in place.

## 2026-09-19 - the capture fails inside the frontend's writer, not in the driver

Three more console runs, all with the trigger working (patch 0031's line appears every
time) and all ending in `take_screenshot -> failed`:

- `/app0/shot.png` (klog/run-shot-r7.log) - failed, no file;
- with `video_gpu_screenshot = "true"` added to `config/retroarch.cfg` and published, so
  that `take_screenshot_choice` takes the viewport path (`take_screenshot_viewport` ->
  the driver's `read_viewport`) - still failed, no file
  (klog/run-shot-r7.log, klog/run-PPSA99169-032236.log);
- `/app0/shot.bmp` instead of `.png`, to rule out the PNG encoder - still failed, no file
  (klog/run-shot-r7b.log, klog/run-PPSA99169-032522.log).

The decisive detail is what is *absent*: the trace around the `capture:` line has no new
`vulkan:` message, and the driver's messenger writes every refusal there. So the failure
is in the frontend, before the frame is ever asked for - `take_screenshot_viewport` returns
false without a message on two of its three early paths: `video_driver_get_viewport_info`
reporting a zero width or height, and `malloc` failing. (Its third path, the driver's
`read_viewport` returning false, would have produced a driver message; the dump path
would have produced a file.)

The config change was **reverted**: `video_gpu_screenshot = "true"` was a hypothesis about
which path is taken, and it did not change the outcome, so it does not belong in the
shipping config.

Next step, in order: print the viewport and the dump's result from the capture patch (one
more line in the trace, which separates "no viewport" from "writer refused"), and if the
viewport is the zero one, call the driver's `read_viewport` directly and write a PPM from
the port's own code - a PPM is a header and the bytes, so no frontend writer is involved.

## 2026-09-19 - reading the frame back in the port's own code: first attempt does not compile

The next step from the last entry was tried: patch 0031 rewritten to call the driver's
`vulkan_read_viewport` directly from `vulkan_frame` and write a PPM (header plus bytes)
from the port's own code, with a forward declaration of the driver's static
`vulkan_read_viewport`. The intention was to take the frontend's screenshot writer out of
the path entirely, since three runs showed it bails before the driver is asked.

It does not compile; the frontend build reports one source missing and the link fails on
`vulkan_raster_font` (the missing object's symbols):

    /home/mihawk/Desktop/PS5_Homebrews/PS5_RetroArch/build/ra-conf/gfx/drivers/vulkan.c:4695:7: error: function declared in block scope cannot have 'static' storage class

The rewrite was reverted rather than pushed further: `tools/apply-port-patches.py` is back
to the committed 0031 (the verified `take_screenshot` trigger), the configured tree is
rebuilt from that, and `make test-unit` is green. The lesson for the next attempt is in
the error itself - the local declaration and the driver's own definition have to agree
exactly, and a `static` forward declaration inside a function body is where that attempt
went wrong.
