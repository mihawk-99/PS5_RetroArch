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

## 2026-09-19 - the readback patch compiles; the console left before the run

The rewritten capture compiles and is in the image: the frontend builds 276 of 276
sources and `strings build/llvm-pie.elf | grep -c "read_viewport ->"` is 1
(`build/r8b-build.log`). The two errors from the first attempt - a `static` declaration
inside a function body, and `vk` referenced before its declaration - are fixed by
declaring the readback prototype at file scope (patch 0032, next to the driver's own
`vulkan_viewport_info` prototype) and by moving the capture block below
`vk_t *vk = (vk_t*)data;`.

The run that would have produced `/app0/shot.ppm` did not happen: the console stopped
answering partway through the round - `ping` and the FTP both report "No route to host" -
and the run that was attempted first failed at its deploy step for the same reason
(`klog/run-shot-r8c.log`: "the upload did not take"). Nothing in the port is implicated:
the same image had deployed and run repeatedly earlier in the day.

So the round ends with the patch built but the pixels still unverified, and that is how
it is committed (`949fdb9`). The next round starts by running it:

    # arm the capture, then run and read the picture back
    (upload /app0/args.txt: --ps5-capture=90 --ps5-capture-path=/app0/shot.ppm)
    bash tools/run-title.sh --no-build --watch 18
    (fetch /app0/shot.ppm, convert to PNG, look at it)

The trace line to expect is `capture: frame 90 of 90, viewport WxH, read_viewport -> ok
(/app0/shot.ppm)`; if it says `failed`, the trace line now carries the viewport size,
which is the value the frontend's own writer never showed.

## 2026-09-19 - a capture run now ends by itself (the console is still away)

The console answered nothing this round either - `ping -c 2 -W 3` exits 1, and the FTP
attempt before it failed the same way - so the capture run could not be made. What was
built instead is the half of the objective's evidence that a killed title can never
produce: a capture run now asks the frontend to quit once it has written its trace line.

- patch 0033 includes `command.h`, next to the `retroarch.h` the file already includes;
- patch 0031's capture calls `command_event(CMD_EVENT_QUIT, NULL)` after the trace line.
  That is how the frontend itself quits, and quitting is what flushes
  `/app0/retroarch.log` - the same 1200 bytes since the first round for exactly one
  reason: this title has always been killed from outside, never exited by itself.

The frontend builds 276 of 276 sources with both patches (`build/r9-build.log`), the
capture string is in `build/llvm-pie.elf`, `make test-unit` is green (37 patches pinned),
and `bash tools/verify.sh` is PASS (format unit build integration evidence).

The run that proves it is the one the previous entry left waiting, unchanged: arm
`/app0/args.txt` with `--ps5-capture=90` and `--ps5-capture-path=/app0/shot.ppm`, run
`tools/run-title.sh --no-build --watch 20`, then read `/app0/shot.ppm` and
`/app0/retroarch.log` back. With the quit in place the verdict should read "it exited on
its own" instead of "this script closed it", and the log should finally have content.

## 2026-09-19 - the GPU path: seven silent faults, and the black screen that is left

Ran the acceptance criteria in `docs/GPU_PATH_CRITERIA.md` against the console with
`bash tools/run-title.sh`, reading `/app0/trace.txt` after each run. Fifteen
diagnostic marks were added to the patch set (0046-0053) because inference failed
five times in a row: every fault below produced a clean trace, a presenting driver
and a black screen.

A4 and B1 are met. `grep -c 'vulkan: '` over a full run's trace is 0, and the trace
carries `video driver: video_vulkan init entered` with no `ps5_init entered`.

The seven faults, each with the patch that fixed it:

| # | Fault | Patch |
| --- | --- | --- |
| 1 | The last refusal was `vkCreateComputePipelines`, not an upload: libps5vk refuses any set-0 binding with stride 0, and this frontend's shared set declares a compute-only storage image at binding 3 | 0036 |
| 2 | `vulkan_gfx_widgets_enabled` answered true, so the frontend skipped `gfx_display_init_first_driver` and `p_disp->dispctx` was never set - `gfx_display_draw` returns immediately without it | 0042 |
| 3 | Nothing enabled `VK_FLAG_MENU_ENABLE`; the frontend's only `true` is in `display_menu_libretro`, which is a libretro concept this content-less title never reaches | 0046 |
| 4 | The optimal menu image was never copied into after the first handover, so the draw sampled an untouched allocation | 0051 |
| 5 | A stale `/app0/args.txt` made every run capture a frame and then quit itself 90 frames in, which the console books as an application crash with a coredump | 0043 |
| 6 | `gfx_display_vk_draw` was instrumented as "the menu's draw"; it is the display-list path, and the driver composites the menu itself in `vulkan_draw_quad` | 0053 |
| 7 | A capture probe that ended its own run: `command_event(CMD_EVENT_QUIT, NULL)` in the driver's capture block | 0039 |

Measured at the moment of the draw, once per frame:

    vulkan set_texture_frame: rgb32=0 320x240 frame=yes
    vulkan copy_staging_to_dynamic: dynamic 320x240 fmt=37 type=2, staging fmt=37 type=1, compute=0
    vulkan menu state: flag=1 idx=0 staging(img=0 buf=1) optimal(img=1 buf=0)
    vulkan draw_quad 0: texture=yes image=yes layout=5 320x240 pipe=yes

Nothing refuses, the copy runs, and the quad is drawn with a valid pipeline and
image in `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`. The screen is black, and the
owner has confirmed it three times.

Not met: B2, B3, B4. Not claimed: C1-C4. The next step is the driver's own draw
path in `../PS5_Vulkan` (`ps5vk_draw.c`), because everything on this side is now
instrumented and reads correct.

Two process notes worth keeping. The first is that the A2 mechanism recorded in the
criteria was wrong for two rounds: both "request RGB8888" and "match the menu's
format" were answers about a path that was not being taken, and the refusal named a
binding rather than an upload. The second is that a leftover file on the console
(`/app0/args.txt`, which deploy never deletes) silently changed what the title did;
`tools/run-title.sh` now clears it before every run.

## 2026-09-19 — Vulkan API diagnostics expose a covered title

Added three named 0054 edits and `src/vulkan_trace.cpp`: wrappers observe all
frontend command-buffer endings, submissions and presentations without changing
arguments or results. Host tests cover failed returns, per-image results, missing
symbols and repeated loading. `bash tools/verify.sh` passed all five gates with
21 tests. The initial test harness used an enum absent from the older frontend
headers; using its numeric VkResult value fixed that harness compilation failure.

`bash tools/run-title.sh --no-build --watch 90` first failed at socket creation
under the sandbox; the approved network retry uploaded and launched. The owner
saw the app background and manually closed it. The script mislabels that as an
exit on its own. The fresh startup segment contains 537 lines and zero refusal
lines, with the first four end/submit/present/per-image results all VK_SUCCESS.
The menu alpha is 1 and its matrix/viewport are plausible. This is a diagnostic
result, not full-window GPU acceptance. Capture and expectation are committed in
`evidence/vulkan-api-results-splash/`; raw console data stays ignored.

Reading the CPU display startup and the sibling's diagnostic title found splash
dismissal on both, but none on RetroArch's Vulkan route. The kernel log retained
the splash scene until manual closure. The next step is a title-side splash fix.
No sibling file was changed. A fresh port-patch replay also exposed repeated 0023
sampler blocks in the configured shader source; record separately from the
display diagnosis.

## 2026-09-19 — Dismiss the shell splash for Vulkan

`src/main.cpp` now performs the public splash-dismissal call before rarch_main.
`bash tools/verify.sh` passed all five gates (21 tests). The staged image digest
and target results are in `evidence/vulkan-splash-dismissal/`.
`bash tools/run-title.sh --no-build --watch 90` recorded splash return 0 and
successful initial Vulkan API results, with zero refusals in the 471-line new
startup. The owner confirmed RGUI is visible, but with buggy colours and flicker.
The final process query found no title; klog shows shell closure without a fatal
signal, so the full watch window is not claimed. Visual defects remain open.

Correction to the previous reproducibility description: 0023's marker is never
present in its replacement at all; no later edit is needed to invalidate it. A
new regression test failed on that exact condition. Its separate marker-only fix
passes all 22 host tests and makes a second patch replay byte-identical.

The user explicitly authorized future uploads and runs without their intervention;
this supersedes the older per-run confirmation rule, while the shared-console
busy check still applies.

The owner subsequently confirmed the second closure was manual too, and clarified
the visual defect as blue colours and black triangles appearing/disappearing.

## 2026-09-19 — Make the sampler fallback patch idempotent

The 0023 marker was prose that existed only in the Python comment, never in its
replacement C++. The new `test_every_patch_inserts_its_own_marker` failed on that
block before its marker was corrected to `patches/series, 0023`. All 22 host
tests then passed. The configured shader source was regenerated solely from
upstream plus named edits to remove the repeated blocks. `bash tools/verify.sh`
passed all five gates. Reapplying the full patch list leaves all 12 patched files'
SHA256 values unchanged: `evidence/frontend-patch-idempotence/` records them.
This is a host build-tool fix and makes no new claim about console rendering.

## 2026-09-19 — Bind deployment and trace to one build identity

Generic startup markers could accept an older transformed executable. Two host
regressions failed before the correction. `tools/build-title.sh` now embeds a
SHA256 identity of its source/archive inputs in a generated ignored header;
main logs it and deployment requires it in the executable readback.
`bash tools/verify.sh` passes all five gates with 25 tests, including the pending
colour converter's exhaustive test. The subsequent 30-second scripted launch
reported exactly the staged identity in its trace; see
`evidence/vulkan-build-identity/`. This rules out an older input set for that run
but does not prove correct pixels. The identity run closed before its final query.
No driver project files were modified and no new dependency or compiler flag was
introduced. The generated header is not committed.

## 2026-09-19 — Correct the RGUI upload channel order and range

Patch 0055 expands the producer's RGBA4444 nibbles to RGBA8888 bytes, preserving
A2's matching 32-bit textures and plain-copy route. Its regression test failed
before the fix and checks all 65,536 inputs after it. The five gates passed with
25 tests. The full 30-second colour run ended with a live title which the script
closed; its fresh trace has zero refusals and successful initial API results.
`evidence/vulkan-menu-rgba/` records the capture and expectations. The subsequent
identity run was verified as the intended input set. The owner's clarified report
is a green menu with blue flicker and black triangles; upload correction does not
resolve GPU corruption, and the overall goal remains open.

## 2026-09-19 — Resolve GPU flicker in the driver compiler

The user's detailed observation distinguished a green menu from blue flicker and
fixed-position dark triangles. An equivalent ONE/ZERO opaque-pass diagnostic
passed host gates and completed a 30-second run but left both defects unchanged;
it is retired in `parked/explicit-stock-blend/`. The user then authorized driver
changes provided they were documented and committed in PS5_Vulkan.

The bounded readback diagnostic (`parked/gpu-readback/`) captured correct menu
texels and white vertex colours, but corrupt completed GPU frames. A host compile
of RetroArch's fragment shader showed both texture coordinates and colour at
NIR base 0; ACO read both from attr0. PS5_Vulkan now assigns fragment input
locations as RADV does before shader-info gathering. Its fix and documentation
are committed in **6f0ce0d**, with dense/sparse compiler regressions, full driver
checks, `make test`, `make lint` and `make` passing.

`bash tools/run-title.sh --no-build --watch 30` against that compiler completed
and the script closed the title. The fresh trace has zero refusals and no observed
end/submit/present error. The owner confirms correct colours, no flicker and no
triangles. All 909 opaque-white sampled texels are correct, and frames 8 and 9
are byte-identical. `evidence/vulkan-fragment-inputs/` records the build identity,
trace and pixel hashes; raw captures remain ignored. A1-A4 and B1-B4 are verified.

Temporary capture hooks were then removed from the configured source and final
build. All five RetroArch gates and 25 tests pass; ELF symbols retain both video
drivers and omit the capture function. Two final deployment attempts stopped at
the pre-upload busy check (one running title). The owner-confirmed fixed diagnostic
build remains installed; no running title was interrupted. No performance or
core-execution claim is made. Criteria documentation now reflects the actual
settled upload mechanism and the user's driver-change authorization.

## 2026-09-19 — Quiet Vulkan timing locates the severe slowdown

Request: measure frame preparation, submission and presentation independently
before assigning the poor responsiveness to the unfinished Vulkan driver.
This is new performance work after the completed GPU menu acceptance.

Patch 0056 adds callback boundaries and limits five formerly unbounded texture
messages to four calls each. `src/vulkan_trace.cpp` measures monotonic elapsed
wall time in memory, emits one summary per >=5-second window after four warmup
callbacks, and preserves API arguments/results and error reporting. The refined
version also measures menu texture updates outside `vulkan_frame`. No driver
change or graphics-route switch was made. Patch count grows from 68 to 77.

Commands: `bash tools/verify.sh`; shared-console idle check;
`bash tools/run-title.sh --no-build --watch 45` for each build. Both runs completed
and their scripts closed the title. The first build
`1c14ef0e4f69b4fc559ba20d5b8bea6e6e5fff42060b0b029f029cc71da18b79`
still logged texture creation/staging asks repeatedly: seven windows fell from
35.907 to 1.630–1.985 FPS, with up to 958.226 ms outside the video callback.
The owner reported better responsiveness initially but a falling estimate.

The final build
`38ada4be3b74b8230ec13aae97b7d3618c4635760c30e90b29a54f0008e14450`
limits those remaining logs. Eight windows hold 53.760–57.942 FPS: 2,239 frames
in 40.039 seconds, aggregate 55.920 FPS. Per-frame weighted means: preparation
5.344 ms, end 0.020, submit 3.525, present 8.075, fence/acquire 0.041, menu texture
0.153, remaining outside time 0.724; total 17.882 ms. Phase averages sum within
printed rounding. Each of the five quieted diagnostics appears exactly four times.
Both traces have zero refusals and no recorded API errors; final successful
command-end, submit and present counters reach 1,800. CPU fallback is absent.

The owner reports "Noticeable improvement" with the estimated refresh rate
starting at 40 Hz and increasing. Source inspection confirms the estimate uses
average frame intervals, not the driver's output-mode rate. This and the timing
support a major diagnostic-I/O bottleneck in the port, not long submit/present
stalls. They do not establish a steady 60 FPS or eliminate all latency: final
frame maxima include 282 ms early and 167–184 ms in later windows. Sparse logs
and summary I/O remain possible contributors to investigate separately.

All five gates PASS with 26 tests. The added fake-clock regression validates
warmup/window boundaries, phase accounting, unchanged dispatch and failed API
results. Applying all 77 patches to a fresh vendor copy and applying them again
produces identical hashes. No toolchain flag/dependency changed. PS5_Vulkan is
clean at 6f0ce0d. The final profiling build is installed.

Evidence: `evidence/vulkan-quiet-timing/{capture,expectation}.json`, replay with
`python3 tools/evidence.py compare evidence/`. Private raw captures:
`klog/gpu-quiet-timing-run.log`, `klog/gpu-quiet-texture-timing-run.log` and their
`*-latest-trace.txt` extracts. Timing definitions: `docs/GPU_TIMING.md`.

## 2026-09-19 — Keep development logs, buffer profiling, then move to XMB

User requested important logs remain available while removing frame-loop stalls.
Patches 0057–0059 and the local profiler remove periodic successful-frame writes,
stop button-transition chatter, and make the two menu-handover probes once-only.
Startup diagnostics, driver errors/refusals and assertions remain. The frontend
logger is explicitly re-enabled after argument/config processing and writes the
build identity: the old 1,200-byte file had been stale. The run tool now captures
and verifies this log, and checks idle state before uploading and launching.

Profiling is opt-in (one-shot gpu-profile.txt), with 120 warmup frames and a bounded
8,192-frame buffer. No summary/file writes occur during the measurement. It writes
windows and per-frame TSV after capture; tools/analyze-gpu-profile.py validates
phase sums/completeness and reports percentiles and presentation-return intervals.
These intervals are not hardware scanout timestamps; missed-interval values are
explicitly estimates. No driver or SDK code changed.

Commands: bash tools/verify.sh; bash tools/run-title.sh --no-build --gpu-profile 60
--watch 80; python3 tools/analyze-gpu-profile.py klog/gpu-profile-<stamp>.tsv.
First buffered run (111457): 3,597 frames, 60.009771315 s, 59.940238 FPS, maximum
interval 16.905813 ms, no presentation-completion gap above 20 ms. Owner: "Very
smooth", estimate stabilizes at 59.941 Hz. With fresh frontend logging (112036):
3,582 frames, 59.690273 FPS, three ~100 ms gaps localized to texture updates.
The remaining first-four-call texture probes could occur late and were limited
to one initial call. The final run (112603), identity
a6063c61145fb42ac409a1d78026f29865673e4575e9a6afc3500e4abccc4f66, launched with
zero refusals and a current frontend log, then rarch_main returned 0 before the
profile completed. Required profile retrieval failed correctly. No final timing
claim is made, and no further performance run was started: the user requested
committing and proceeding to XMB. Existing audio/config-save errors remain visible
and outside this step's acceptance.

All five host gates PASS, 28 tests; fresh patch replay/idempotence PASS, 86 edits.
Evidence: evidence/vulkan-buffered-logging/{capture,expectation}.json. Replay:
python3 tools/evidence.py compare evidence/. Raw logs and TSV remain in klog/.

## 2026-09-19 — XMB default on the Vulkan path

The user ended performance work; logging was committed as d46495d, then this step
enabled upstream XMB while retaining RGUI/video_ps5. Official monochrome assets
are pinned at 73106363e14e34c08a5854b4cfbc29f184e3b783 (120 fixed icons, M+ font,
licenses). Configure arguments now invalidate the cached configured tree before
feature flags are derived. The asset pin adds no build-time network dependency.
ASSETS_DIR uses the Unix prefix convention; patch 0063 sets the actual title asset
default because the null platform frontend never executes Unix initialization.

Console commands were `tools/run-title.sh --no-build --watch N`; each upload was
read back and the run trace selected by its last startup marker/build identity.
Raw files below are ignored; per-run identities, counts and results are distilled
in evidence/xmb-default/capture.json:

- xmb-first-run.log (60 s): XMB aborted on an 8-byte UBO range; EndCommandBuffer
  returned -13, then Mesa asserted on submission of the invalid command buffer.
  Owner reported black/background/crash. Patch 0060 pads the ribbon upload.
- xmb-corrected-run.log (60 s): the range refusal cleared; missing sampler binding
  1 caused the next -13/assertion. Owner again saw no usable menu. Patch 0061 also
  fixes the independently found strip bounds/count bug; patch 0062 binds the
  unused sampler slots through the existing descriptor-fill path.
- xmb-bindings-run.log (60 s): zero refusals/API errors; title survived until the
  script closed it. Input worked, but the owner saw faint text and black output.
- xmb-assets-run.log (60 s): explicit /app0/assets default, startup diagnostics
  report assets present/fonts ready; zero refusals and full script-owned run.
  Owner still reported corrupted icons/text. This ruled out missing assets.
- xmb-capture-run.log (30 s): authorized readback on frames 120/121 reproduced the
  narrow background, glyph fragments and repeated/cropped icon mip fragments.
- xmb-textures-run.log (45 s): patch 0065 fixes physical-row alignment/UVs; 0066
  uses one static texture level. Readback shows readable XMB with intact icons,
  gradient and ribbon. Owner: "It's flawless!"; initial estimated refresh 25 Hz.
  This diagnostic wrote large synchronous snapshots; no performance claim made.
- xmb-final-run.log (45 s): screenshot source/hook removed. Identity
  2df0e277df59faf661a37920988d1b804394c8ffe509c2b57296270f3dcaf7b3;
  zero refusals, no API failures/assertion, successful presentations, current
  frontend log, assets present/fonts ready, no framebuffer writes. Script closed
  the still-running title. PS5_Vulkan remained unchanged at 6f0ce0d.

Host verification: tools/verify.sh PASS (format/unit/build/integration/evidence),
31 unit tests, 278/278 frontend sources. Fresh vendor replay and second-pass
idempotence PASS for 100 edits (86 plus 14 XMB compatibility/diagnostic edits).
The geometry test executes C and checks bounds/winding through the complete
ribbon; row-alignment cases include 336/720-wide R8 atlases. Asset tests verify
pinned checksums and every fixed icon named by upstream. Evidence replay after
adding this record: 13 captures, zero failures.

Intermediate host failures were fixed before deployment: the edit-count guard
was updated after intentional additions; the asset logger needed verbosity.h;
the temporary capture anchor was made distinct from the timing hook. Removing
that capture initially left its call in the generated source and failed linking
ps5_capture_images. Restoring that generated file from pristine vendor and
reapplying the final named edits removed it; the normal rebuild and gates passed.
The retired recipe documents this patch-removal requirement.

Reproduce acceptance with tools/verify.sh, tools/run-title.sh --no-build --watch
45 and python3 tools/evidence.py compare evidence/. Screenshot recipe:
parked/xmb-readback/README.md. Known audio/config-save errors remain logged.
No core, steady-state XMB FPS or general mip-chain correctness is claimed.

### 2026-09-19 — native PCM audio driver, registration and audible acceptance

User-assigned step: adapt ProsperoLight's native output to `audio_driver_t`,
register `audio_ps5`, verify audible playback and buffering, then commit. No SDL,
Opus, core implementation or sibling driver change is part of this step.

Implemented `src/audio_ps5.cpp`: 48 kHz S16 stereo, 256-frame AudioOut grains,
rate negotiation, bounded producer ring and paced worker, byte-based write and
capacity callbacks, blocking/nonblocking backpressure, pause/resume, error wakeup
and drain/close cleanup. Named patch 0067 adds registration/default (102 total
patch edits). The staged config agrees. Added an opt-in native tone/buffering test
and `tools/run-title.sh --audio-test` with current-build report validation.
Normal launches do not generate tones; useful development logs remain enabled.

Validation and artifacts:

- `python3 -m unittest discover -s tests -p test_audio_ps5.py -v`: PASS. The real
  backend runs against a paced native mock: exact native arguments, rate/byte
  contract, FIFO/wrap, partial/full nonblocking writes, blocked producer wakeup,
  zero-filled tails, pause/resume and injected output failure. Expected injected
  errors were observed and handled; no unexplained host-test failure remains.
- `tools/verify.sh`: all five gates PASS, 32 unit tests, signed PS5 title and
  143-file staged-tree manifest. Identity
  `801df0ac6a5754c3e6a6ea688f1e49e8ddd2041aeb131a637b8c8b59f4030d55`.
- Fresh patch replay/idempotence: PASS, 101 edits applied plus one marker already
  in upstream (`return "vulkan";`), then 102 present and bytes unchanged. An
  initial scratch assertion expected 102 newly applied edits; inspection showed
  the pre-existing upstream marker, and the corrected accounting passed. No
  vendor source was changed.
- `tools/run-title.sh --no-build --audio-test --watch 45`: verified upload;
  `klog/audio-native-run.log` and `klog/audio-test-123431.json`. The report passes:
  193,536 accepted/played frames, zero discarded frames/output errors, 1,536-frame queue
  and high water, 6,144-byte partial nonblocking write. Native drain returned
  `0x100` (success), close zero. Normal frontend audio reopened at 48 kHz and
  initialized successfully. Zero Vulkan refusals/API failures; XMB fonts/assets
  ready. Owner: "I heard left right left right, then retroarch started!"
  Runner found the title closed at 45 seconds; owner: "I closed it manually".
  This is not treated as an unexplained crash or a full-window survival pass.
- `tools/run-title.sh --no-build --no-deploy --watch 45`: normal follow-up,
  `klog/audio-normal-run.log`, current `klog/retroarch-123631.log`. No test-tone
  activation, audio initialized, XMB ready, zero refusals/API failures. Title
  remained alive for the full window and the runner closed it.
- Committed evidence: `evidence/native-audio/{capture,audio-test,expectation}.json`.
  `python3 tools/evidence.py compare evidence/` replays 14 records.

ProsperoLight and PS5_Vulkan were not modified; PS5_Vulkan remains `6f0ce0d`.
The known unset configuration-save directory remains outside this audio step.
Core audio and sustained content-based A/V synchronization remain unverified;
zero-filled idle/partial grains are not claimed to measure streaming underruns.

### 2026-09-19 — fix configuration and native directory browsing

User reported missing config loading and browsers containing only `/`. Inspection
confirmed the null platform lacked the callbacks/defaults needed by this title.
Implemented `frontend_ctx_ps5` (0068), preserved initial argv, supplied native paths
and roots, and separated the live config from the packaged seed. Main now passes
`-c /app0/config/retroarch.cfg`; the frontend creates only its own title directories
and installs the seed only if no live configuration exists.

Verification sequence, including failures:

1. Initial build failed because the new menu interface reached libretro-common's
   relative config include through the pristine vendor tree. The title now uses
   the configured libretro-common include directory; added the missing C++
   initializer-list include. No dependency/version was added.
2. First console run, `tools/run-title.sh --no-build --watch 90`, identity
   `c2d8df3799dc02af17d8f0694291f38f9cc30b1276ad8d992d6f86af5b18fa2a`,
   `klog/paths-first-run.log`: seed installed; libc opendir returned EPERM for
   known roots. Owner reported crash. Kernel SIGSEGV at zero, ELF-symbolized stack
   reached application-path discovery during config saving, from menu init.
3. Added native executable path (0069) and public SDK getdents adapter (0070).
   Second run, same command, identity
   `887759ce3cfbe4baf4e5d63b9f0abcc86c3c6e15deafe9a0e954e6de308386d4`,
   `klog/paths-native-run.log`: config saved; owner "It works! And it saved config".
   However, owner confirmed `/app0` remained empty. Root enumerated eight entries;
   4 KiB reads on `/app0` and `cores` reported EINVAL. Runner found title alive at
   90 seconds and closed it, then falsely rejected the frontend log because the
   next local diagnostic build had changed its comparison identity. The trace's
   identity and independently captured frontend log match this run. Fixed runner
   to snapshot identity before deployment; this partial run is not acceptance.
4. Increased the directory-read buffer to 64 KiB. Host regression explicitly
   rejects small mounted-directory reads: the prior 4 KiB variant fails, current
   adapter passes. Unit harness setup issues (stale linker-wrapper reference and
   an incorrect hardcoded string-length assertion) were corrected before gates.
5. Final run, same command, identity
   `26139a294d687ccf21c8fa779e5ad3883273b873b71ce67e1df48f48589f8f76`,
   `klog/paths-large-run.log`, frontend `klog/retroarch-130100.log`: 96 entries in
   `/app0`, two in the empty `cores` directory, errno zero; no frontend ERROR
   lines; native audio and XMB fonts/assets ready; zero Vulkan refusals/API
   failures; 90 seconds alive and script-closed. Owner: "Folders are visible,
   and when I select Load configuration file I could see the .cfg file".
6. FTP readback of live config before/after the final upload/restart: 108,188 bytes,
   unchanged SHA-256 `f7f2b7ba3545c0dd375004576c80d78a11b42aa2c351eb1f81349695571f15c1`.
   Only selected known port settings/digest are distilled; raw files stay ignored.
7. `tools/verify.sh`: all five gates PASS, 35 unit tests, 15 evidence records.
   Fresh patch replay 104 applied plus one upstream marker; second pass 105 present,
   unchanged bytes. Tests exercise config preservation, startup callback, roots,
   SDK directory records/errors, and executable-path discovery without procfs.

Artifacts: `evidence/native-paths/{capture,expectation}.json`;
replay: `python3 tools/evidence.py compare evidence/`. Documentation maps `/app0`
to the FTP title folder and explains live-config preservation and content locations.
No core/ROM execution, USB availability or complete storage milestone is claimed.
PS5_Vulkan and ProsperoLight are unchanged; Vulkan and CPU/RGUI fallback registration
are preserved. Packaged seed changes after the run are comments only; the tested
binary identity is unchanged and the saved live configuration is byte-identical.

## 2026-09-19 — Repair managed directory permissions for FTP

Scope: the owner requested `0775` or `0777` after FTP could create downloads but
could not place files in app-created cores/content. Existing folder listings
showed `0755`. No sibling project changes or core implementation were assigned.

- Regression: the former mkdir-only code fails the host permission assertion
  with existing `0755` directories and umask `0077`. The final fixture covers
  existing `0755` and `0775` plus new folders; all become `0777`, while seed and
  existing configuration preservation checks still pass.
- First console run: `tools/run-title.sh --no-build --watch 30`, identity
  `88367c83a15584015a71805f5599effc29875fd419a144ca042a5c59f2148a4f`.
  Alive throughout, script-closed; chmod worked, all seven modes were `0775`.
  `python3 tools/check-ftp-write.py` returned 1: all seven uploads denied with
  `550 Permission denied`. No probes remained. This was not acceptance.
- An initial attempt to deploy the fallback refused because a title was running.
  No title was interrupted; the owner closed it and confirmed availability.
- Final console run: same runner command, identity
  `347655429cf4a60f3d7608c3ba4e46ec1c1972f81704cbea9f9e654c9942cfed`.
  Upload readback matched, 30 seconds alive and script-closed. Zero permission
  errors, Vulkan refusals, GPU API failures or frontend ERROR lines; presentation,
  XMB assets/fonts and native audio initialization observed.
- Final FTP command returned 0: all seven directories `0777`, each disposable
  upload/readback/cleanup passed. No existing files were overwritten by probes.
- All five `tools/verify.sh` gates PASS; 35 unit tests, 16 evidence records.
  Machine-readable capture and expectations: `evidence/ftp-directory-permissions/`.
  Raw captures: `klog/permissions-{run,open-run}.log`, `retroarch-131931.log`,
  `ftp-write-*.json`; host/gate logs: `/tmp/permissions-*.log`.

Only the seven managed directory modes are repaired; user file modes are not
changed recursively. Error logging remains enabled. PS5_Vulkan is unchanged at
`6f0ce0d`. Actual core/content execution remains a separate, unverified step.

## 2026-09-19 — Native joypad fixes stick navigation and binding capture

The owner reported working menu buttons but no left-stick navigation and binding
capture timeouts. The input-only backend bypassed the joypad callbacks used by
both paths. Added `ps5_joypad`, its built-in default profile and registration;
`input_ps5` supplies no duplicate hardcoded gamepad state. The pad is polled only
through the joypad interface. Zero new samples preserve held input for the extra
binding-screen poll; errors/interception/disconnect clear input. Existing saved
configuration is preserved, and an empty joypad selection finds the new backend.

- Initial unit gate flagged the pinned patch counts (105 to 108, and three to
  four input_driver.c edits). Updated them for the new joypad declaration,
  registry entry and built-in profile. No additional dependency or SDK flag.
- Host fixture compiles the actual backend and upstream input mapping/analog
  helpers. Explicit button and axis bindings suppress old mappings; raw capture,
  left/right axes, triggers, deadzone, second polls and teardown/reinit pass.
  ELF relocation checks verify both registration and the profile in the build.
- Fresh patch replay: 107 applied plus one existing upstream marker. Second
  pass: 108 present, all bytes unchanged (`/tmp/input-patch-replay.log`).
- Interactive console command: `tools/run-title.sh --no-build --watch 120`.
  Build `e6211dea9fd0bb512e83a9979a6a44c0c8c5fdf175564c753e3fd2a25067e0a9`.
  Owner confirmed left-stick navigation and button/stick binding capture:
  "Everything works flawlessly!" The title was gone at the 120-second check;
  the runner's generic early-exit verdict was resolved by the owner's explicit
  confirmation "I closed it manually". No crash was inferred from that verdict.
  Raw capture `klog/input-joypad-run.log`, frontend `klog/retroarch-133020.log`.

Rumble, multiple controllers, saved remapping after restart and actual core
execution are not claimed. PS5_Vulkan remains unchanged at `6f0ce0d`.

Follow-up: `tools/run-title.sh --no-build --no-deploy --watch 30`, same build,
full window alive and script-closed (`klog/input-joypad-followup.log`, frontend
`klog/retroarch-133319.log`). Both runs selected/configured the PS5 joypad and
presented XMB with native audio initialization, zero GPU refusals/API errors and
no frontend ERROR lines. All five `tools/verify.sh` gates PASS: 37 unit tests,
17 evidence records. Sanitized capture/expectations: `evidence/native-joypad/`.


## 2026-09-19 — FCEUmm reproducible build; native core loading blocked

Requested step: build FCEUmm with this repository's SDK and native-title pipeline.
The websrv SNES9x2010 script supplied the fetch/build/stage pattern only. Added
`tools/build-fceumm.sh`, `make fceumm`, shared-ELF ABI verification, title staging
and core metadata defaults. Pinned source/info archives are digest-checked.
Upstream source remains untouched. Link uses the compiler driver, explicit native
runtime stubs, no payload CRT/static libc, and a content-derived build ID; reasons
and paths are in `docs/REFERENCE.md`.

Initial link experiments rejected raw-linker `-Wl` arguments/missing `-lm`, then
host Clang rejected `-nostdlibc`. The final command uses the compiler link driver,
`LIBM=` with the libc math stub, and supported `-nostdlib -nodefaultlibs`.
Clang's implicit `--build-id=uuid` changed hashes between identical builds;
`--build-id=sha1` fixed this. Multiple fresh builds now give core SHA-256
`d1afcd2ea84de627f4a28d8ec4e81d63d23ca5b531811ba57357d98f8b31759f`.

First console command: `tools/run-title.sh --no-build --watch 180` (manual
selection), identity `b6c7c509098a466630da0215a52bec1f3fd12e9ee1fe071bb39937faa0b5d7e4`.
Core open failed with null loader error. The user reported missing detection.
The `.info` was in `/app0/info`, but saved config had an empty info path; upstream
therefore searched `/app0/cores`. This was an actual placement compatibility bug.
That run ended early without a fatal signal; manual close was not confirmed.

Corrected staging includes identical info beside the core and in `info/`.
Cache refresh is required because upstream can cache missing metadata. Deployment
now writes/readbacks `core_info.refresh` after info upload; the running deployment
had loaded the earlier script, so equivalent markers were uploaded/read back
explicitly before the corrected run. Unit checks cover refresh location/content
and failed readback. Core uploads require exact SHA-256, without the old driver
marker fallback. No live user settings were rewritten.

Second command: `tools/run-title.sh --no-build --watch 120`, identity
`261d33099adec97c231205beb21d6011e1587436df099d4e6ac6f29949c50c8f`.
Full FTP core readback matches all 4,836,144 bytes and the final hash. Decoding the
console's RZIP metadata cache confirms full FCEUmm name, NES extensions and
`has_info: true`. Core open still fails. Owner: "App crashed after I started game".
The log reports missing dynamic-core path during content reinitialization; klog
records SIGSEGV at 0x80. Symbolization after subtracting image base 0x400000 shows
`vulkan_alive(NULL)` -> `runloop_iterate` -> `rarch_main` -> `main` -> `_start`.
This is a frontend failure path after a failed core load, not proof of core or
Vulkan-driver execution failure. Runtime loading/game acceptance remains FAIL.

Host verification: `tools/verify.sh` passes all five gates, 45 tests. Artifact:
`evidence/fceumm-build/` contains the ABI report, metadata result and explicit
failed target result. `parked/native-core-loading/README.md` records the remaining
loader/recovery work and its acceptance. PS5_Vulkan unchanged at 6f0ce0d; no ROM
or BIOS is distributed. This commit completes build tooling, not a playable port.

## 2026-09-19: native FCEUmm loader, failure guards and first gameplay

Implemented the requested native loading/recovery step. The loader maps checked
ELF64 segments, binds 71 explicit native imports, applies 16,001 relocations and
publishes handles only after protection succeeds. FCEUmm remains a dynamically
loaded core built with this project's SDK; PS5_Vulkan remains unchanged at
6f0ce0d. Core upload uses matching RGBA8 textures with an XRGB8888 conversion.

Runs and failures:

- `tools/run-title.sh --no-build --core-test --watch 30`, memory probe,
  `klog/run-PPSA99169-141420.log`: anonymous RW -> RX code returned 42; full window.
- Same command, `klog/run-PPSA99169-142423.log`: eight real-core load/export/API/
  identity/unload cycles passed before frontend startup; full window.
- `tools/run-title.sh --no-build --no-deploy --watch 180`,
  `klog/run-PPSA99169-142526.log`: menu-time core file read failed; owner saw
  No Core/archive failure and confirmed manual quit/close. Kernel exit(0) then
  SIGSYS is retained, not classified as a core execution crash.
- Mapped/chunked-read diagnostic, `klog/run-PPSA99169-143443.log`: startup and
  live-menu loader/rejection reports passed. Manual game start then aborted in
  `vulkan_copy_staging_to_dynamic` (ELF offset 0x6b87ff, ce7176a7 build).
  Symbolization: `addr2line -Cfipe klog/native-core-ce7176a7.elf 0x6b87ff
  0x6b3880 0x6b0129 0xae8f1b 0xada6fd 0x620a`. The format mismatch selected the
  RGB565-only compute path; matching RGBA core textures resolve it.
- `tools/verify.sh`: PASS, all five gates, 52 tests; log
  `/tmp/core-frame-verify.log`. Loader tests execute relocations/imports and
  malformed-file rejection; recovery test fails on upstream's unchecked return
  and passes with the guard; frame test verifies channels/alpha/pitch/in-place.
- `tools/run-title.sh --no-build --core-test --watch 180`,
  `klog/native-core-game-rgba-run.log`, kernel `klog/run-PPSA99169-143921.log`:
  identity 9b287ca7e0d4a04721c68d30fb88b678a62cc568c1f2d44b75716b464c21b6d5;
  core SHA-256 fec7dc4eb7ec6cae937ae778f0b59365d8bd7570927a73353424345e13589d9c.
  Both diagnostic JSON reports pass. Owner manually loaded the archive and
  answered “Works flawlessly!” to video/audio/controls, then reported closing
  manually because the menu shortcut was unset. Three launches occurred within
  the capture; the runner closed a later menu instance. Zero GPU refusals/API
  failure records and zero kernel fatal signals. Do not claim continuous runtime.

Evidence: `evidence/native-core-loading/`, replay with `tools/verify.sh evidence`.
Raw gameplay log was saved before a later menu launch replaced it. No ROM or
private filename is committed. Return-to-menu shortcut, repeated game unload,
saves/states and measured performance are not included in this acceptance.

Provenance correction for the preceding entry: this task did not edit
PS5_Vulkan, but its checkout advanced independently during the work to 8c2985a
and then 085c632. “Remains unchanged at 6f0ce0d” is therefore not a valid claim
about the sibling checkout or the linked driver revision. The accepted title
identity includes the actual archive bytes read at build time; no driver source
commit is inferred from the later checkout. Its archive changed after the
accepted title linked, so the accepted dist/ artifact has not been rebuilt
against that newer archive. Console acceptance remains tied to identity 9b287ca7.


## 2026-09-19 — Native mGBA archives and content lifecycle

Added `make mgba` and the normal title build/stage integration using pinned
upstream CMake libretro source 7a12d6d4b9acb14c0ae62c9166b6a2f3d08007f6.
Both GB and GBA engines are enabled; official metadata is staged in info/ and
cores/. The core has no SDL/Qt or payload CRT. CMake is used because it is this
revision's maintained libretro target. Native directory/time adapters and 21
validated init callbacks support its ABI. Pins and reasons for toolchain/link
flags are recorded in REFERENCE.md and the generated evidence build manifest.

Iterations, all run with manual archive selection:

- `656d34aa…`, `klog/run-PPSA99169-150419.log`: loader/recovery passed, but GB/GBC
  crashed in mCoreConfigPortablePath at native getcwd; GBA failed before core
  entry. Explicit /app0/config/mgba removed getcwd/getenv dependencies.
- `9c7baa14…`, `klog/run-PPSA99169-151335.log`: owner confirmed GB/GBC load.
  Archive diagnostic isolated a null 16 MiB allocation, extraction result 2;
  errno=2 was stale. Owner manually quit, exit(0) then SIGSYS.
- `773678fa…`, `klog/run-PPSA99169-152050.log`: tracked mmap allocations >=1 MiB
  enabled GBA archive loading. Owner reported swapped colours, menu flicker and
  Quit crash. The raw lld invocation initially rejected -Wl wrapper syntax;
  corrected --wrap flags passed all gates (56 tests). Native realloc ownership
  remains native; this is not an interposer inside the native libc library.
- `ef7004d3…`, `klog/run-PPSA99169-153046.log`: mGBA XBGR->XRGB callback copy,
  consistent RGBA menu textures and immutable cached frames fixed colours.
  Native LoadExec exit fixed Quit (zero fatal reports). Owner confirmed both;
  Close Content still flickered for mGBA and FCEUmm. All gates, 57 tests.
- `6a761ad7…`, `klog/mgba-transition-capture-run.log`: diagnostic screenshot
  trigger built and passed gates, but console became busy during deployment;
  launch refused. Trigger was removed; owner supplied an RGUI photo/video.
- UV crop host development: format lint first required formatting; regression
  initially compared double and float UVs exactly and was corrected to float32
  expectations. First crop build passed 58 tests/all gates. A follow-up made
  substituted sampled format determine the physical width consistently.
- Final `82024b89e74bbad4ad2ce2e2916d1a49baf26ac325fef20fc71a2c168b862337`:
  `bash tools/verify.sh` passed all five gates, 58 tests, log
  `/tmp/mgba-uv-format-final-verify.log`. mGBA SHA-256:
  feb1922c9fe9dd3424b42f538a80362d0574b4dfa0134980d3fd14c52593cff6.
  `bash tools/run-title.sh --no-build --core-test=mgba --watch 180` first refused
  a busy console. After owner confirmed idle, upload/readback matched and the
  launch succeeded. `klog/mgba-uv-crop-run.log`, kernel
  `klog/run-PPSA99169-155435.log`: one current-build launch, zero Vulkan refusals,
  zero GPU API failure records and zero kernel fatal signal reports. Eight
  loader cycles and actual failed-load recovery passed. Owner: “Menu and next
  game are clean.” Live frontend log records GBA -> menu -> GB/GBC -> menu,
  then frontend return 0/native exit before the window ended. Runner's wording
  “exited on its own” is not evidence of a crash or uninterrupted gameplay.

Patch 0078 crops core sampling to initialized logical pixels in padded images;
it uses per-pass/per-sync VBOs and preserves logical size semantics. No switch
away from Vulkan and no PS5_Vulkan edits. The sibling is independently developed;
linked archive hashes, unchanged through this build, are in the evidence.

Evidence: `evidence/mgba-native/`, replay `bash tools/verify.sh evidence`.
Private ROM names, console credentials and raw captures are not committed.
Save/state round trips, long-run A/V sync, measured performance, Slang presets,
and a fresh FCEUmm/XMB transition matrix are not claimed.


## 2026-09-19 — Snes9x with explicit colour and C++ lifetime contracts

Implemented the requested Snes9x port using pinned official libretro revision
fae2fea08f74180759ef540ee94259213f503480 (1.63), source archive SHA-256
0d4b0c4181d66668ec0040eb17f90a559f7b7fa6cbd9198593c3bdbe79acd029.
`make snes9x` invokes the upstream libretro Makefile with this project's SDK;
no alternate SDK or payload CRT is introduced. Upstream LTO-off, no-strict-
aliasing and no-exceptions/no-RTTI choices remain. Explicit native/C++ bindings
require -z undefs for core link but are resolved by the title link. Metadata
revision/digests and all port inputs are captured in the evidence build report.
Upstream info display_version remains 1.61; the actual core reports 1.63 + pin.

Colour handling leaves RGB565 renderer/filter arithmetic intact and converts
only final frames into a separate bounded XRGB8888 buffer. Both libretro pixel
format negotiation sites and every video callback path are patched. Host tests
exhaust all 65,536 RGB565 colours through the actual frontend RGBA upload, with
additional pitch, capacity, cached-source, hires/NTSC/4x dimension checks.

Initial static build 81f07c1a… passed ABI but was not deployed: its imported
__cxa_atexit would retain callbacks into unloaded code. The final core includes
an export-hidden C++ destructor registry; a validated fini-array callback runs
before last-close unmap. Loader tests exercise reverse destruction order,
reference counting/reload and invalid finalizer rejection before constructors.
The host C++ fixture initially hit GNU ld's overlapping EH FDE header error with
the PS5 linker layout; --no-eh-frame-hdr fixes that host-only fixture. The target
build linked normally. TLS/legacy init/fini/unwind registration remain unsupported.

Verification and console acceptance:

- `bash tools/verify.sh`: PASS format, unit, build, integration, evidence;
  60 tests. Log `/tmp/snes9x-verify.log`. Final core SHA-256:
  e8b66c5f6656afe927181e3fb4fb5d13152ae525fc64705b59af5d9a847a7cac,
  3,392,576 bytes, 25 exports, 4 initializers, 1 finalizer.
- Frontend identity:
  64bef2e7826e5a63a6644a933e24ef5403d28c754230c0038eb8a76b4baa1c53.
  Preserved ELF `klog/snes9x-64bef2e7.elf`.
- `bash tools/run-title.sh --no-build --core-test=snes9x --watch 180`:
  verified deployment, eight native loader cycles and resident-menu recovery
  passed. Deliberate missing-core errors are part of that successful test.
  Raw `klog/snes9x-first-run.log`, kernel `klog/run-PPSA99169-161400.log`.
  One current-build launch, zero Vulkan refusals/API failure records and zero
  kernel fatal signal reports. Title remained open through 180s and runner
  closed it. No claim of 180s uninterrupted gameplay.
- Owner-loaded archive negotiated XRGB8888, 256x224, declared 60.10 Hz and
  32040 Hz audio, then returned to the dummy menu. Owner answered the combined
  colours/sound/controls/Quick Menu/Close Content/next-content question with
  “Works flawlessly.” The geometry log independently records menu -> Snes9x ->
  menu; no exhaustive cross-core matrix or special-mode coverage is claimed.
- FCEUmm/mGBA binaries retain their accepted hashes. PS5_Vulkan was read only;
  its exact linked archive hashes stayed unchanged during verification and are
  saved in the evidence. No driver source revision is inferred.

Evidence: `evidence/snes9x-native/`; replay `bash tools/verify.sh evidence`.
Raw logs/private filenames remain ignored. BIOS subsystems, special chips,
interlace/hires/NTSC/HD Mode 7 console tests, saves/states and long-run timing are
separate acceptance work. Build/ABI reports are static checks; their false
console_loading_verified field is superseded only by the separate console
capture, not rewritten into an unsupported claim.

## 2026-09-19 — Native FBNeo with verified gameplay colours and transitions

Task: add FBNeo through the existing native core pipeline, use supplied metadata
only as a reference, and verify gameplay/menu/next-game colours before committing.
Official source and core-info are pinned and hash-checked; no game/BIOS is shipped.
The full core preserves native 32-bit rendering and converts 16-bit callbacks.
The native wrapper selects SDK-compatible flags, core-local C++ cleanup and a
bounded MPEG decoder escape. Catalogue names use two bulk allocations through
the existing frontend allocator. PS5_Vulkan was not modified.

Commands and outcomes:

- Initial `tools/build-fbneo.sh`: MPEG throw/try failed with exceptions disabled.
  Added a local non-unwinding bounds escape and differential decoder tests.
- `bash tools/verify.sh` → `/tmp/fbneo-verify.log`: all gates/62 tests passed.
  Console `--no-build --core-test=fbneo --watch 240` failed: metadata patch retained
  free() after changing the buffer to static. Build `a69d4ad4...`; raw
  `klog/fbneo-first-run.log`, `klog/run-PPSA99169-163509.log`. Owner reported crash.
- Removed invalid free, added 10,000-query sanitizer regression. All gates passed
  in `/tmp/fbneo-fixed-verify.log`; build `96ee4812...` passed eight loader cycles
  and recovery. Game load failed with NULL strcpy in BurnLibInit after catalogue
  small allocations exhausted libc heap. Raw `klog/fbneo-fixed-run.log` and
  `klog/run-PPSA99169-164532.log`. Owner confirmed core loads but game crashes.
- Bulk catalogue storage, rollback and pointer restoration fix the second failure.
  `bash tools/verify.sh` → `/tmp/fbneo-catalogue-verify.log`: all five gates pass,
  63 tests. Stress/failure/reload tests use 30,000 drivers; all 28,910 real names fit.
- `bash tools/run-title.sh --no-build --core-test=fbneo --watch 240` →
  `klog/fbneo-catalogue-run.log`: verified deployment and loader/recovery PASS.
  Kernel `klog/run-PPSA99169-165611.log`; live trace/frontend captured under
  `klog/fbneo-catalogue-live-*`. Owner manually loaded games and confirmed
  “Works flawlessly!” for colours/audio/input and menu/next-game transitions.
  Both native depth 32 and 16 negotiated XRGB8888. No GPU refusals/API failure
  records, kernel fatal signals or audio backend errors. Five frontend ERROR
  lines are intentional recovery tests. Optional serialization hint #87 remains
  unsupported, without preventing gameplay. No cross-platform save-state claim.
- Frontend returned 0 and native Quit status 0 before the watch window ended;
  no claim of 240 seconds of continuous gameplay. Core rates are declared values.

Final identity: `41cb59a98907d3c382fa7c6c2282e64740a2d079364fd7f231dd0ddef7a2ca10`.
FBNeo SHA-256: `0e4231a238af2e5cc2630568abcaec2d3fe5356d647ec87ab9d9c13e8da66e44`.
85,234,664 bytes; 25 callbacks; 7 initializers and 1 finalizer. Existing three core
hashes stayed unchanged; four linked Vulkan archive hashes stayed unchanged.
Symbol-bearing ELF: `klog/fbneo-41cb59a9-symbols.elf`. Committable evidence and
expectations: `evidence/fbneo-native/`, replay with `bash tools/verify.sh evidence`.

## 2026-09-19 — Native Genesis Plus GX, colours and transitions accepted

Task: add Genesis Plus GX through the existing native pipeline, use the supplied
.info as reference only, and verify gameplay/menu colours before committing.
Source c2838c7dc4236fc2fe94e5dbd08b41486067918e and official core-info
5a74858ab2f7a50cebb5a6330895bc38899531c0 are hash pinned. New script/target,
packaging/import union/build identity and loader selector are integrated. The
script preserves disc-image codecs while disabling host-detected physical CD-ROM
support and optional zstd weak tracing imports with no linked provider. The
core's RGB565 renderer remains intact; a bounded XRGB8888 adapter uses the actual
viewport offset and row pitch. update_geometry's unused return type is corrected.
Frontend video and PS5_Vulkan are unchanged.

Commands and results:
- `python3 -m unittest discover -s tests -p test_genesis_plus_gx_video.py -v`:
  three tests passed, including every RGB565 colour through upstream packing and
  the frontend upload helper, cropped/resized cached views and invalid bounds.
- `bash tools/verify.sh > /tmp/genesis-verify.log 2>&1`: all five gates passed,
  66 tests, existing 22 evidence captures replayed. Build source/ABI/import/SDK
  records are copied into `evidence/genesis-plus-gx-native/`.
- `bash tools/verify.sh format > /tmp/genesis-format-final.log 2>&1`: passed
  after the final build-script flags. Existing core hashes match all four
  accepted builds. PS5_Vulkan's four archives matched before/after the build.
- `bash tools/run-title.sh --no-build --core-test=genesis_plus_gx --watch 240
  > klog/genesis-first-run.log 2>&1`: exit 0; idle guard, full FTP readback,
  eight native loader cycles/25 callbacks/API/name and failed-load/menu recovery
  passed. Identity ecfcddd57febbb484c2e0724e95f43bbffb3ddd9b9c22aa9c1cf3e37af4a9445;
  core 881b5118e6afe700d8de21df679b7e51a458c0bb2fe11603b96373d87f6a2fe0,
  13,388,248 bytes. Symbol-bearing ELF/map preserved under klog/genesis-ecfcddd5-*.

Two current-build launches appear in trace, both with rarch_main/native quit 0
before the 240-second window ended. Kernel `klog/run-PPSA99169-171959.log` has no
fatal signals. There are no Vulkan refusals or GPU API failures; eight audio
close reports have errors=0. Frontend snapshots preserve both launches because
relaunch truncates retroarch.log. They show an archive member with .md extension,
XRGB8888 callbacks, 256x192 geometry, and return to the 320x240 dummy menu.

Failures are retained: the first snapshot has five deliberate missing-core
recovery errors plus two archive-extraction errors. The owner identified the
latter content as ARCADE - Sega System 16 & 32, which belongs with FBNeo. No
archive-structure inspection or parser fix is claimed. The second snapshot has
zero ERROR lines. Core files and private filenames remain in ignored storage;
only sanitized results are in evidence.

Owner: “Sega Genesis works flawlessly”; explicit follow-up for Quick Menu/Close
Content/next game: “Yes, transitions and next game are clean.” This accepts the
requested colour and transition check, not every Sega system, BIOS/disc/CHD,
filter/interlace option, save mechanism or long-run A/V/performance behaviour.
Replay with `python3 tools/evidence.py compare evidence/`; the new capture and
expectation are in `evidence/genesis-plus-gx-native/`.

## 2026-09-19 — Public README reflects the verified native port

Replaced the initial project-definition README with the current frontend and
five-core status, a linked table of contents, scoped console verification,
software-rendering versus Vulkan-presentation explanation, and a roadmap using
✅/❌ with an explicit pending-acceptance legend. Removed obsolete claims that
nothing runs and that Vulkan/XMB must wait for driver rung 1.0. Linked Mihawk's
public PS5_Vulkan repository and explained static linkage/rebuild requirements.

Build instructions now name the repository-local SDK, frontend fetch, required
sibling driver artifacts and initial title build before artifact-dependent tests.
Installation explains FTP versus /app0, metadata, live configuration and preserved
user folders. Credits name Mihawk, RetroArch/libretro, BlackBearReloaded's
ProsperoLight and native foundation, John Törnblom/ps5-payload-dev, core authors,
Mesa/Khronos, assets and toolchain projects, with repository links. Core author
names and license summaries were checked against pinned metadata/source notices.
The README separates executable-memory evidence from unintegrated PPSSPP JIT,
and keeps future targets distinct from console-verified features.

Documentation-only validation:
- `python3 /tmp/check-readme-links.py > /tmp/readme-links.json`: 30 local links
  and fragments resolve, including all ten TOC entries; code fences balanced.
- `bash tools/verify.sh format > /tmp/readme-format.log 2>&1`: PASS.
- `bash tools/verify.sh evidence > /tmp/readme-evidence.log 2>&1`: PASS,
  23 captures replayed, zero failures. README gameplay claims refer to those
  existing committed artifacts, especially evidence/genesis-plus-gx-native/.
- `git diff --check`: PASS. No source, dependency, binary or console changes;
  the accepted gameplay build remains 93a1332's recorded build identity.

## 2026-09-19 — XMB allocation diagnostic branch, host verified only

Owner requested a branch and diagnostic build, explicitly withholding automatic
upload/launch while working on another project. Created
`codex/xmb-allocation-diagnostics`. Added opt-in live allocation accounting to the
existing native/mapped wrappers, plus observation of `posix_memalign`, frontend
Vulkan image lifetimes and queue-idle calls. Fixed-capacity metadata and stack-only
`write` logging preserve OOM evidence without allocating. Allocation policy is
unchanged. Five-second summaries include counts, requested bytes, peaks, largest
caller groups, failure counts and explicit coverage/saturation limits.

The prior capture (`klog/xmb-crash-20260919-181233/`) maps the null write to Mesa's
error-reporting allocation following `vk_sync_create` allocation failure in
`vkQueueWaitIdle` during XMB texture unload. That diagnoses the immediate crash,
not the reason memory allocation failed. No driver fix or console result is claimed.

Command: `PS5_MEMORY_DIAGNOSTICS=1 bash tools/verify.sh` — PASS all five host gates.
The first unit pass had 67 tests; the added Vulkan dispatch test ran in the final
integration pass and final `bash tools/verify.sh format unit` (68 tests, PASS).
`python3 tools/check-memory-diagnostics.py` — PASS required symbols and archive
hashes. Machine-readable inspection and capture/expectation:
`evidence/xmb-memory-diagnostic/`. Raw host logs are in `klog/xmb-memory-verify.log`
and `klog/xmb-memory-final-checks.log`. Existing upstream core/compiler warnings
remain in the full build log; no diagnostic-source warning was observed.

Build identity: `000adf3edc2538bc563f833f0ccd2e91a01053aa6dac19dd568a580cdbbbb3cb`.
Output: `dist/PPSA99169/`; matching executable, symbol ELF and map preserved under
`klog/xmb-memory-000adf3edc25/`. The current libps5vk archive differs from the
crashing baseline; the other three archives match its recorded digests. The
opt-in build snapshots all four locally to avoid mutable archive inputs while
the owner develops PS5_Vulkan. Mode and linked bytes participate in identity.
No sibling writes, console access, upload, launch or settings change occurred.
Manual protocol and limitations are in `docs/MEMORY_DIAGNOSTICS.md`.

## 2026-09-19 — First allocation diagnostic froze; bound failure logging

Owner authorized upload/launch of diagnostic `000adf3edc25`, then reported a
complete freeze instead of a crash and explicitly confirmed manually closing it.
Deployment was verified, klog started before launch and the allocation-log build
identity matched. Raw: `klog/xmb-memory-run-20260919-183305/`, including prelaunch
copies, deployment log, launch marker and post-freeze files. No post-launch
kernel fatal signal was observed. Console later reported count=0 after manual close.

First observed allocation failure: 13,187 ms, 15,488 bytes at `xmb_list_insert`;
subsequent failures also include 648 bytes at `menu_entries_append` (decoded with
the exact preserved ELF and image load base). The logger wrote 12,793 failure
records and 12,793 summaries, totaling 5,404,264 bytes. This unbounded synchronous
I/O distorted the diagnostic and could aggravate a freeze. The initial allocation
failure precedes the flood; this does not prove logging caused the underlying
failure or explains the complete freeze. Observed native live requests grew from
4,559,423 bytes at the first sample to 11,627,255; mappings stayed 5,498,938. Net
frontend Vulkan image count remained 131. Foreign frees leave native coverage
incomplete; the observed errno=22 may be stale. No heap size/leak claim is made.

Revised logging emits the first four failures immediately, then at most one
detail and summary per five seconds even if presentation stops. All failures
remain counted; failure_records and failure_suppressed expose rate limiting.
Regression test uses a controlled clock: 10,006 failures, five records, 10,001
suppressed and <8 KiB. A supplementary 10,000-failure comparison against 971534c
produced 3,707,607 bytes before and 2,351 after. This fixes diagnostic flooding,
not XMB's allocation failure or Mesa's unsafe OOM logging.

`PS5_MEMORY_DIAGNOSTICS=1 bash tools/verify.sh` — PASS format/unit/build/integration/
evidence, 68 tests. `python3 tools/check-memory-diagnostics.py` — PASS.
Evidence: `evidence/xmb-memory-bounded-logging/`; raw host log:
`klog/xmb-memory-bounded-verify.log`. Revised identity:
`a4a751e58339deb55fd8b7b4cdae01c3e8479e79c22be53d717a696413ce34be`.
Matching ELF/map/executable are preserved under `klog/xmb-memory-a4a751e58339/`.
All four driver archive hashes match the first diagnostic. No PS5_Vulkan edits.
No revised console launch: further console validation awaits owner intervention.

Revised diagnostic was uploaded under the owner's standing upload authorization
and verified by `tools/deploy-title.py` after confirming console idle. Raw:
`klog/xmb-memory-bounded-upload.log`. No launch command was sent.

## 2026-09-19 — Bounded diagnostic reproduces original XMB SIGSEGV

Owner authorized record/run, then reported the crash reproduced. Verified the
already-uploaded `a4a751e58339` build identity, preserved old logs, started klog
before launch, and captured memory-diagnostics.log, retroarch.log, trace.txt and
configuration. No rebuild or additional driver change preceded this run.
Raw: `klog/xmb-memory-run-20260919-184732/`. Local collector stopped after final
retrieval; no automatic relaunch. Sanitized evidence: `evidence/xmb-memory-crash/`.

Kernel records one post-launch SIGSEGV at runtime `0x45f30e`, NULL write. Exact
preserved ELF and klog base symbolize the same chain as the original crash:
XMB wallpaper/context update -> white-texture unload -> vkQueueWaitIdle -> Mesa
allocation-error logging -> NULL dereference. First allocation failure at
6,429 ms requests 15,488 bytes at xmb_list_insert (inlined xmb_alloc_node).
Observed native requested bytes rise from 4,625,679 at 5,004 ms to 11,616,951 at
first failure. Tracked mappings stay 5,498,938 bytes; net tracked image count
stays 131; dropped records remain zero. The allocation log is only 3,217 bytes,
with four immediate failure records: later failures can be suppressed by rate
limiting. No exact heap limit, leak, fragmentation or corruption cause is proved.

This confirms that the bounded diagnostic can capture the original crash without
the previous unbounded failure-log flood. It is not a fix or a stable-runtime
acceptance. Asked the owner which menu/list they were entering to correlate the
allocation spike; that context is pending. Evidence checks reproduce the measured
record and match its expected failing outcome. No PS5_Vulkan writes occurred.

Owner clarification: “It was just holding a button and making the menu scroll
fast.” Reproduction does not require an explicit folder-open step in the owner's
account. Exact navigation direction/list was not specified; do not infer either.


## 2026-09-19 — XMB tab-switch allocation investigation

Owner clarified that the trigger is holding left/right between XMB tabs.
The configured source shows per-entry 15,488-byte nodes (15,360 bytes of inline
path arrays), 648-byte callbacks, one copied visible outgoing list, and normal
list clearing before tab rebuilds. Missing wallpaper candidates can repeatedly
recreate the white texture, leading to the queue-wait path seen in the crash.
The captured 6,991,272-byte rise remains unattributed: the only live-caller
breakdown predates it. No claim of a node leak, heap limit or GPU-memory leak.

`python3 tools/probe-xmb-allocations.py --expect evidence/xmb-allocation-investigation/host-probe.json`
passes 10,000 synthetic replacements using extracted upstream lifetime functions:
4,566,034 allocations, stable 259,288-byte end-of-cycle usage, 8,236,400-byte
peak, zero after cleanup. A separate check demonstrates the small console_name
ownership leak. The initial LeakSanitizer run failed because sandbox ptrace
prevents scanning; explicit accounting plus ASan/UBSan pass with leak scanning
disabled. The host harness is not a complete frontend/native heap/GPU replay.

Evidence and limits: `evidence/xmb-allocation-investigation/` and
`docs/XMB_ALLOCATION_INVESTIGATION.md`. No title launch, deployment, application
rebuild, allocator-policy change or PS5_Vulkan edit. The next discriminating
measurement is first-failure live callers plus bounded numeric tab/list counts;
it requires another owner-authorized launch.

Verification: `bash tools/verify.sh format evidence` passes format and all 27
evidence replays; `git diff --check` passes. Product build/integration gates
were not rerun because this step changes only offline tooling and documentation.


## 2026-09-19 — Bounded first-failure owners and XMB context

Added opt-in numeric context hooks before XMB allocation, list copy/clear,
destination selection and populate. A fixed eight-entry history records
boundary sizes and native requests; insertion updates one cached progress
snapshot. Node constructor/copy/free counters supplement the allocator table.
The first failure emits these plus up to 16 live caller groups per route, once.
No frontend callbacks or cached pointers are dereferenced by the failure logger.
Navigation writes no log bytes; ordinary failures retain their existing rate limit.

Patch 0079 adds 11 anchors, intentionally changing the patch-count expectation
from 139 to 150. The first gate attempt failed that check, then the corrected
full run passed. PS5_MEMORY_DIAGNOSTICS now also enables the frontend hooks; this
flag is in the existing build fingerprint so switching modes recompiles the
frontend. No dependency, allocator policy, menu behavior or driver source changed.

`PS5_MEMORY_DIAGNOSTICS=1 bash tools/verify.sh` passed all five gates, 69 tests,
278/278 frontend sources. Log: `klog/xmb-first-failure-verify-final.log`.
`python3 tools/check-memory-diagnostics.py` verified title symbols, references
from the actual XMB object and stable archive copies. The injected-clock test
captures 10,006 failures, five ordinary records, one expanded snapshot, eight
history entries, 48 owner rows and 9,404 bytes. Ring wrap, owner-route separation,
rank caps, zero navigation I/O and inert normal-build C hooks are verified.
`bash tools/verify.sh format evidence` and `git diff --check` also pass.

Build: `e60deca9412fcedaa59162cc5254fc8e0b13df5c2b3fb4a8187e0b059b0acf00`.
Evidence: `evidence/xmb-first-failure-diagnostic/`; exact ELF/map/executable,
inspection and raw captures: `klog/xmb-first-failure/`. The linked libps5vk
archive changed from the previous run's 900d496a9eb7 prefix to 8c2a1c46a38e;
three other archive hashes match. Full hashes are in inspection.json. The
owner develops the driver concurrently; PS5_Vulkan was not modified here.

Console was confirmed idle, previous logs/config preserved locally, and the
verified build uploaded with readback through the normal deployment function.
No title was launched or stopped. Runtime acceptance remains pending: the next
owner-authorized run should capture klog, reproduce held left/right XMB tab
switching, and preserve logs before relaunch. This is a diagnostic, not a crash fix.


## 2026-09-19 — First-failure capture attributes the XMB burst to a large playlist

Owner authorized d7ad1b3's diagnostic launch. Existing logs were preserved, the
uploaded identity verified, klog started before launch, and the running
e60deca9412f session confirmed. Owner reports a crash during natural scrolling;
rapid input is not a necessary condition. The read-only title-status query
returned zero afterward. Passive collection was stopped locally after capture;
no automatic title kill or second launch occurred.

At 6,486 ms, XMB selected custom tab 7 and freed the old list's node: live
observed node count fell 4 -> 3. It then successfully created 339 nodes in 23 ms
before xmb_list_insert failed a 15,488-byte allocation at index 339, list size
340. Native live requested bytes rose 4,497,239 -> 11,616,951. The first-failure
owner table attributes 5,265,920 bytes / 340 live requests to xmb_list_insert,
1,179,664 to playlist JSON array growth, 473,152 to file-list arrays, and 220,968
to menu callbacks. The new nodes explain 5,250,432 bytes of that interval's
7,119,712-byte increase. This is a destination-list construction burst, not
evidence of retaining successive old tab lists.

Read-only FTP inspection found one custom playlist with 7,384 entries. Its full
XMB node demand is 114,363,392 bytes plus 4,784,832 bytes for callback objects,
before strings and other metadata. Raw playlist content stays only in ignored
klog; committed evidence records counts, not names or game paths. The first
listing attempt used unsupported NLST and returned 502; the existing FTP helper
then enumerated successfully. No console files were modified during inspection.

One post-launch SIGSEGV is a NULL write in __vk_log_impl, reached through the
same queue-idle allocation-error/white-texture-unload path as the prior crash.
Symbolization used the exact archived ELF and recorded load base (no return-PC
adjustment for fault RIP; minus one for caller PCs). Native accounting excludes
system-internal allocations; no exact heap-limit or fragmentation claim follows.

XMB's non-diagnostic patch entries are identical to initial XMB commit e080d81.
The node allocator policy dates to mGBA's large-buffer mapping change; diagnostic
hooks did not change it. This does not exclude other regressions. Whether an
older binary opened this same full playlist/configuration remains unverified;
the owner was asked. No fix, policy change, new build or driver edit in this step.

Artifacts: `evidence/xmb-playlist-allocation-crash/`; raw capture and private
playlist snapshot: `klog/xmb-first-failure-run-20260919-192044/`. Acceptance is
attribution of the observed burst, not menu stability. `bash tools/verify.sh
format evidence` replays the record; no product build was changed.


## 2026-09-19 — XMB list safety and normal-build acceptance

Owner confirms the list-safety diagnostic no longer freezes or crashes and the
normal build eliminates its five-second navigation hitches. Requested landing on
main with current logging unchanged. RetroArch.log, trace and passive klog remain;
only the existing opt-in memory observer is disabled in the accepted normal build.
No PS5_Vulkan sources were modified and no agent launch/kill was issued in this step.

XMB nodes shrink from 15,488 to 96 bytes with visible-only optional icon paths.
Reclaimable mapped slabs hold nodes/callbacks; checked list insertion and animation
copy preserve valid ownership on failure, stop failed population and permit retry
after clear. Patched headers now invalidate all frontend consumer objects. No new
SDK, dependency, compiler flag or version pin was added. See docs/XMB_LIST_SAFETY.md.

Both `PS5_MEMORY_DIAGNOSTICS=1 bash tools/verify.sh` and
`PS5_MEMORY_DIAGNOSTICS=0 PS5_VULKAN_DIR="$PWD/build/xmb-normal-vulkan" bash tools/verify.sh`
passed all five gates, 71 tests and 278 compiled frontend sources. The first gate
attempt failed the pinned edit count (150); updating it to the actual 176 resolved
that failure. Fault-injection and 7,384-entry host tests passed ASan/UBSan with
explicit ownership/mapping accounting. GPU operations are stubbed in host tests.
Normal inspection initially found different Mesa object hashes; stripped bytes
match, and recompiling with the original source path reproduced the old hashes:
the difference is debug compilation-directory information, not runtime code.

Diagnostic identity 88d30106ce625e783e899c97ddeab43d4ca16949ddd98cfb129a06190e0c4ec6
has zero allocation failures, dropped records, image/idle failures, frontend ERROR
lines and GPU API failure/refusal records in the matching 55.422-second capture.
Native requested peak was 6,518,064 bytes; mapped tracking returned to zero at clean
exit. Kernel backlog collection was post-run, not full launch-to-exit coverage.
Raw capture: klog/xmb-stutter-20260919-200720/; exact diagnostic artifacts and gates:
klog/xmb-safe-lists/. Diagnostic sampling scans 131,072 slots synchronously before
presentation every five seconds, matching the reported hitch cadence.

Normal identity 2d0743abbcf289efd0a549929d25ce7512968fc93cba8163467fdd3641c8fcf6
uses the same four archived driver libraries; observer symbols/hooks are absent
and the safe allocator is present. Artifacts/inspection/gates: klog/xmb-normal/;
upload identity and unchanged configuration: klog/xmb-normal-upload-20260919-201708/.
The owner reported “That fixed it”; no fresh normal-run logs or measured stall
latencies are claimed. The unsafe driver logger under arbitrary OOM remains outside
this frontend fix. Sanitized evidence: evidence/xmb-safe-list-run/. Replay with
`bash tools/verify.sh format evidence`; runtime and current logging are unchanged
since the accepted normal build.


## 2026-09-19 — Thumbnail channel order and input after configuration reset

Owner reported swapped save-state thumbnail colours and loss of input after Reset
to Defaults. Patch 0081 preserves the asynchronous image task's supports_rgba flag,
so decoded pixels match the Vulkan menu texture upload format. Gameplay conversion,
PNG encoding and synchronous asset loading are unchanged. Patch 0082 defaults the
joypad backend to ps5 (input already did), and asks the connected controller to
announce itself again after a live reset clears auto-binds. The next poll restores
profile discovery once without reopening the device or requiring a new pad sample.
Logging and PS5_Vulkan sources are unchanged.

Three executable regressions were seen failing before the fixes: wrong red pixel,
null joypad default, and absent binding refresh. Focused image/default/input tests
pass afterward. Full verification used:
`PS5_MEMORY_DIAGNOSTICS=0 PS5_VULKAN_DIR="$PWD/build/xmb-normal-vulkan" bash tools/verify.sh`.
All five gates passed, with 74 tests, 278 frontend sources and 179 patch edits. The
first full attempt failed the old four-edit configuration.c inventory; it now counts
the added joypad-default edit as the fifth. Total inventory grows from 176 to 179.
No new SDK, dependency, compiler flag or version pin is introduced. The existing
archive override retains the accepted XMB driver inputs; all four archives and
three Mesa utility objects match that build.

Identity: e11018a79a62ce91fa723deaad6939c1e48a47bace21b3527ae84f4452148451.
Exact ELF/map/title/manifest and gate/regression logs: klog/thumbnail-input-build/.
Eboot-only upload: klog/thumbnail-input-upload-20260919-205531/. Idle was checked,
runtime identity was read back, and live configuration remained byte-identical.
Saved input was ps5 with automatic joypad selection, so no config repair was needed.
No agent launch or kill was sent. Owner subsequently replied “Perfect. Commit”.
This is acceptance of the reported fixes; individual checklist results and fresh
runtime/kernel logs were not captured, so no new log-health claim is made.

Evidence: evidence/thumbnail-input-defaults/ (source/artifact hashes, sanitized
machine deployment report, owner acceptance). Replay: `bash tools/verify.sh format evidence`.
Future manual checks and scope: docs/THUMBNAIL_INPUT_DEFAULTS.md.

## 2026-09-19 — PPSSPP Track A: the platform branch and the cross build

The owner asked for an implementation plan and for Track A to start. The plan is
PPSSPP_Implementation_Plan.md: software GPU core first (no PS5_Vulkan dependency),
then the Vulkan renderer once the driver work lands, one core binary and two runtime
configurations. This entry records the first step, which is verified.

**The build.** tools/build-ppsspp.sh fetches pinned PPSSPP at
f293b10fb2d9dc0c2bc10281444ee3d3e932e6ad with its 29 submodules, resets the tree,
applies patches/ppsspp/0001-platform-ps5.patch and 0002-cross-build-ps5.patch with
`patch --fuzz=0`, configures the pinned tree directly with
tooling/ppsspp/ps5-toolchain.cmake, builds the libretro target with the SDK's own
prospero-clang wrappers, and runs tools/check-core.py.

Result: `core ABI PASS: ppsspp_libretro.so; 25 exports; kernel_web;
sha256=038001216b59698c28268860349e0b27e8e8cb940f98dbb12650a90d063d71c6`,
18,485,448 bytes. readelf on the artifact: zero PT_TLS segments, three 16 KiB load
segments (flags 5/4/6, no writable-executable), and NEEDED limited to
libkernel_web.sprx, libSceLibcInternal.sprx and libScePosixForWebKit.sprx.
Reports: build/cores/ppsspp/abi.json and build.json (revision, 29 submodule SHAs,
seven port-input hashes). This is a host and artifact result: the core has not been
deployed or loaded on the console.

**What the toolchain decided, with the measurements.** A TLS probe compiled and
linked with prospero-clang++ and tooling/native/ps5-core.ld showed zero PT_TLS and one
undefined import, __emutls_get_address: the SDK compiles with -femulated-tls, so
PPSSPP's thread_local caches (GPU/Software/Sampler.cpp, DrawPixel.cpp) need no source
change, contrary to the handoff plan's expectation. Two flag shims were required:
-Dstatic_assert=_Static_assert, because PPSSPP's own -D_XOPEN_SOURCE=700 pins
__ISO_C_VISIBLE to 1990 on this FreeBSD-derived libc and ext/xxhash.h then calls an
undeclared C11 static_assert, and -DZSTD_TRACE=0, the same switch the frontend build
already uses for zstd's weak tracing hooks. Four libc entry points that vendored
third-party code calls are declared but not exported by the SDK stubs — swab
(ext/libpng17), nl_langinfo (ext/armips, ext/SPIRV-Cross), tmpfile (ext/lua/liolib.c)
and tmpnam (ext/lua/loslib.c) — so tooling/ppsspp/ps5-libc-shims.cpp implements them
inside the core; the two temporary-file calls refuse with NULL, which both callers
handle, because this target has no writable /tmp contract and neither is on a boot or
gameplay path.

**The import audit.** The core has 479 undefined symbols. Cross-checked against every
SDK shared stub plus the libc++, libc++abi, libunwind and clang-builtins archives the
title links, exactly one is unresolvable: localtime_r, which tools/core-imports.py
already aliases to RetroArch's locked rtime_localtime (the mGBA precedent). The union
of the five shipped cores plus PPSSPP generates a 497-binding table for six cores with
no TLS or type conflicts; the shipped five-core table was regenerated afterwards, so
no unfinished core is in the title's import surface yet.

**Gates.** `bash tools/verify.sh format unit` — PASS, 74 tests, 19 s. The build gate is
untouched: PPSSPP is deliberately not in core_names in tools/build-title.sh until the
title link and ABI gate pass, which is the next step.

**Not proven.** The title link with ppsspp in core_names; console load, initialisation
and teardown; a game. No console was contacted; nothing was uploaded or launched.

## 2026-09-19 — PPSSPP A3: the title links with the core in it

A3 of PPSSPP_Implementation_Plan.md is the proof that the port's import surface is
complete: build the whole title with `ppsspp` in `core_names`, and let the converter
refuse any symbol no public SDK stub exports.

**It links.** `ppsspp` joined `core_names` and the build-identity input list in
tools/build-title.sh, and `bash tools/build-title.sh` (JOBS=14) completed with exit 0:
all six cores report `core ABI PASS … 25 exports`, `tools/core-imports.py` reports
**497 explicit native bindings for 6 cores**, and the title built to 161 files with
`eboot.bin` 34,655,444 bytes at identity
ed3f78147e015696bb44550e7151352e341d333d78d0dc6d7b4315bf6fd7a9fc. Six cores are
staged in dist/PPSA99169/cores/, PPSSPP's included as 18,485,448 bytes plus its
pinned 1,793-byte metadata.

The two imports worth naming are resolved: `__emutls_get_address` is defined in the
title (local symbol at 0x6c6ae0, pulled from the clang builtins archive the Vulkan
archives already require) and `localtime_r` goes through the existing
`rtime_localtime` alias. The title carries 465 undefined symbols, every one of them
accepted by the converter's stub check.

**Gates.** `bash tools/verify.sh format unit` — PASS, 74 tests. `bash tools/verify.sh
integration` — PASS, including check-manifest on the rebuilt folder. The build gate
now builds PPSSPP as well, because that is what A3's acceptance means: the unfinished
core is in the shipped staging only after its ABI and link gates pass.

**Open: the artifact digest is not reproducible across builds.** Four builds of the
same pinned revision produced four different sha256 values — 038001216b59698c and
e27177a3d4e56d80 from a /tmp checkout, then 56627f010acd6cb2 and 79647489be7b794e
from two consecutive runs against the same .deps checkout, with identical toolchain,
flags and patches. The generated version string is the same in each
(v1.20.4-1868-gf293b10fb, or "unknown" on a checkout without tags), and neither
armips — the only vendored code using __DATE__/__TIME__ — nor any date-like literal
is linked into the core, so the differing bytes are not yet localized. What is pinned
is the source revision, the 29 submodule SHAs, the two patches, the toolchain file and
the shims; what varies is the resulting byte image. The ABI report records the
artifact hash, so every console claim stays tied to one build. Localizing the
difference is a follow-up, not an A4 dependency.

**Not proven.** Console load, initialisation, teardown and gameplay. Nothing was
uploaded or launched, and no console was contacted.

## 2026-09-19 — PPSSPP A4 preparation: reproducible builds, and the shipped cores are untouched

Two gaps left by A3 are closed, and the console step is armed.

**Reproducibility: found and fixed.** Four builds had produced four hashes. Comparing
two of them byte by byte localized the difference: .rodata (40,843 bytes), .text
(3,237), .rela.dyn (971) and the build-id. Dumping both .rodata sections showed the
same size (1,793,764) with one string in the multiset differing —
" translated Sep 19 2026 22:48:28" against " translated Sep 19 2026 22:50:27" — which
is libpng's banner from ext/libpng17/pngerror.c, built from __DATE__ and __TIME__. The
large footprint comes from the linker's string tail-merging: one varying string
changes the merge layout, so ~40 KiB moves. clang honours SOURCE_DATE_EPOCH for both
macros (verified with a two-line probe compiled twice), so tools/build-ppsspp.sh now
derives the epoch from the pinned commit itself
(`git show -s --format=%ct $revision`) and exports it. Two consecutive full builds
after the change: identical, sha256 8346e010a8781f8c… over 18,501,832 bytes. The
epoch is recorded in build.json as source_date_epoch.

**The five shipped cores are undisturbed, measured rather than assumed.** Rebuilding
fceumm and fbneo in a detached worktree at the pre-change commit b1dced9 gives
8f21cea01c018141c0e8e3493b9d73cf5e0f7534dbd5149249684f6bfbab04c1 and
8c9c402ffa82cb049d346b305294ac824178b5dd14e8cefb394a0ec882861e1e — the same two
hashes this tree produces with PPSSPP added. mgba (feb1922c…), snes9x (e8b66c5f…) and
genesis_plus_gx (881b5118…) are byte-identical to their committed evidence. The older
fceumm and fbneo hashes in klog/thumbnail-input-build/ and evidence/fbneo-native/
predate changes to those cores' own inputs and are not a PPSSPP regression.

**The A4 vehicle is wired.** --core-test=ppsspp is accepted by tools/run-title.sh and
by src/core_loader_probe.cpp, which loads /app0/cores/ppsspp_libretro.so eight times,
checks all 25 libretro exports, the API version and the reported library name
("PPSSPP"), and writes core-loader-test.json with the build identity. It never calls
retro_init, so this step creates no Vulkan device and measures the loader alone: an
18.5 MB module, 479 imports and a full C++ static-initialisation pass, eight times
over.

**Gates.** `bash tools/verify.sh` — PASS on all five: format, unit (74 tests), build
(the title now builds and links with six cores), integration, evidence (31 captures
replayed, 0 failed).

**Not proven.** Everything console-side: load, initialisation, teardown, and a game.
The launch needs the owner; the assets (system/PPSSPP/) and content are owner-supplied.

## 2026-09-19 — PPSSPP A4: the core loads on the console, and the assets land in system/

**The core loads.** The console loader test passed for PPSSPP:
`{"build_identity":"build identity: 73d2aad8a13e5ad0…","core":"ppsspp","passed":true,"cycles":8,
"exports":25,"api":1,"missing_rejected":true,"unknown_symbol_rejected":true}` — eight full
load/unload cycles of the 18.1 MB core, all 25 libretro exports, the API version, and
both negative cases still rejected. The loader log names the real work:
`ran 110 initializers`, `ready symbols=19430 relocations=24812 mapped_bytes=18923520`.

Three separate blockers had to fall before that, and each was found on the console
rather than by reading code:

- **Unresolvable imports.** The first run failed with `unresolved native runtime
  import: gai_strerror`, then `glGetString` once that was fixed. The cause is the
  loader's rule that every import must have an address: the title can carry imports
  it never calls, but a core cannot, so any symbol the console leaves null is fatal.
  Two groups qualified. `libScePosixForWebKit` supplies `gai_strerror`, `getaddrinfo`,
  `freeaddrinfo` and `isatty` — and no core this title has ever shipped imported
  anything from that module, so it had never been exercised. They are now implemented
  in `tooling/ppsspp/ps5-libc-shims.cpp` (resolver refuses with EAI_FAIL, isatty
  answers 0, which is true). The other 83 were the OpenGL backend, which this port
  can never use: `patches/ppsspp/0003-no-opengl.patch` drops `Common/GPU/OpenGL/*`,
  `GPU/GLES/*`, the two libretro GL contexts and PS5's `PPSSPP_API_ANY_GL`, so the
  core has **zero** gl*/egl* imports and the import table fell from 497 bindings to
  410. Every remaining import resolves from `libkernel_web` or `libSceLibcInternal`,
  the two modules the frontend already proves.

- **The thread pool.** The first successful load logged `ThreadManager::Init(compute
  threads: 256, all: 512)`. PPSSPP's `Init` multiplies its two arguments
  (`numRealCores * numLogicalCoresPerCpu`), so they are cores and threads-per-core,
  not two totals; the platform branch had set both from `sysconf`. It now takes the
  physical core count from CPUID leaf 0x80000008 and derives SMT siblings from the
  online count: **16 compute / 32 total**, which is right for this console.

- **The runtime assets.** PPSSPP resolves them as `<system>/PPSSPP` and reported
  "Core system files missing, expect bugs". `tools/build-ppsspp.sh` now stages the
  **source tree's** `assets/` — the canonical, complete tree (193 files, 22 MiB) —
  rather than the 187-file partial copy CMake leaves in the build directory (it omits
  `cheats.json`, `knownfuncs.ini` and four others), and `tools/build-title.sh` copies
  it to `dist/PPSA99169/system/PPSSPP`, which the deploy publishes. Verified on the
  console: `/data/homebrew/PPSA99169/system/PPSSPP` holds the full tree including
  `flash0` (18), `debugger` (12), `shaders` (54) and `lang` (48). The warning is gone
  from the run log, and the tree's digest is recorded in `build.json`
  (`ppsspp_assets_sha256 223c78b2…`).

**Still open: the first frame.** With the core loading and the assets found, the run
reaches `retro_init`'s end and then the title takes a **SIGSEGV on a worker thread**:
`proc name: eboot.bin`, `thread ID: 102061` (not the main thread), `page fault (user
read data, page not present)`, `fault address 0x70`, and the backtrace's frames
(`0x80004055c`, `0x800040638`, `0x8000407fb`) all lie in `libkernel.sprx`'s text
(0x800000000–0x800044000 per the capture's own `xotext` line), i.e. inside the
console's thread machinery rather than in PPSSPP's code. The frontend log ends at
`ThreadManager::Init`, which logs *before* it creates the pool's 32 `std::thread`s —
so worker-thread creation is where it dies. `patches/ppsspp/0004-software-backend-default.patch`
is in place for the next run: on PS5 the `ppsspp_backend` default is now `"none"`
(the libretro software context) because the Vulkan renderer cannot come up against a
driver with no combined depth/stencil format, and the stalled Vulkan attempt is why
earlier runs showed no progress past core init. The console's saved core options were
cleared so the new default applies.

**Gates.** `bash tools/verify.sh` — PASS on all five.

## 2026-09-20 — PPSSPP relinked against rung-1.0, and what that rung does not cover

The owner closed PS5_Vulkan's rung 1.0 (`d4e73ff`, "the depth pair proved -- rung 1.0
closed": D16_UNORM's and D32_SFLOAT's SAMPLED_IMAGE and BLIT_SRC were the last four
unreached features) and asked for RetroArch to be built against it, then for PPSSPP to
follow through it once stability testing is done.

**The relink.** `bash tools/verify.sh` rebuilt the frontend and title against the
sibling's current archives and passed all five gates (32 captures replayed):

    libps5vk.ps5.a      d41f934b720e9ffff4c8a05b450e7a5a200abaaf9274a94eeefc1f9179704c21
    libvk_runtime.ps5.a 4106a2c56b269bc52a3976b6bc0134c00b5006ff5358a4b45355afce4a907aa7
    libpsbc_driver.ps5.a 47db73edf649023688c536e6c1a2b3e5180d582db1440c2f54dbf5bdb4fe64b2
    libpsbc_support.ps5.a e36d3e2a3fa93b0a74db15b9efce7aad4ed7ac4e024de98d5f23dc5fee90acf8

Title identity 735f7eb1acdc4f41a004eab0e6a86f2d4eab1ab2cf953eed0325ccbd74727c77.
The sibling's tree is being rebuilt while this work runs — `libps5vk.ps5.a` changed
twice inside this session (82055034 -> 3f816850 -> d41f934b) and its working tree
carries uncommitted vertex-format work (`driver/ps5vk_image.c`, `driver/ps5vk_pipeline.c`,
`tools/build-psbc-ps5.sh`, `tooling/psbc/patch-vertex-formats.py`), one edit of which
postdates the archive. The hashes above are the ones this link embedded; a fresh
relink is owed once the sibling's build settles.

**The review: rung 1.0 gives PPSSPP its format matrix, not its renderer.** The rung
closed reachable format features, the depth pair (attachment, sample, blit, transfers),
wide, narrow, packed and signed colour targets, transfer coverage and the command and
limits audits — all of which PPSSPP's texture cache wants. It did not touch the six
things PPSSPP's Vulkan renderer needs, and the source still refuses each by name:

- Combined depth/stencil: the table holds `VK_FORMAT_D16_UNORM` and
  `VK_FORMAT_D32_SFLOAT` and no `S8` format at all (`driver/ps5vk_image.c:345,359`),
  while PPSSPP's `VulkanContext::CreateDevice` requires one of D24_UNORM_S8_UINT,
  D32_SFLOAT_S8_UINT or D16_UNORM_S8_UINT and asserts without it. This is the device
  creation blocker, and it is why the earlier Vulkan-default runs stalled rather than
  progressed.
- Attachments smaller than 3840x2160: `driver/ps5vk_draw.c:526,570,587,709,754` still
  reject any other extent, depth, colour, render area and inherited target alike.
  PPSSPP renders PSP framebuffers at 512x272, 480x272, 384x272 and more.
- Cull mode and front face (`driver/ps5vk_pipeline.c:825`), stencil test and dynamic
  stencil state (`:831`, `:819`), per-channel colour write masks (`:858`) and dynamic
  blend constants (`:819`) are unchanged refusals.

**The other blocker is ours, not the driver's.** The console run after the loader test
showed the title taking a SIGSEGV on a worker thread inside `ThreadManager::Init`
(klog/run-PPSA99169-002106.log: fault address 0x70, frames in `libkernel.sprx` text).
That happens whatever the renderer is, so it is the next step: PPSSPP cannot be
"loaded through" anything until its thread pool survives creation.

**Console evidence committed.** `evidence/ppsspp-native/` records the loader-test
result, the import and thread fixes, the asset tree and the open first-frame blocker;
`bash tools/verify.sh evidence` replays it (32 captures, 0 failed).

## 2026-09-20 — The PPSSPP worker crash, narrowed to libkernel's module lookup

PPSSPP dies in `ThreadManager::Init` on the console, on the first worker thread. This
entry records what the crash *is*, and what it is not, because the next attempt should
start from here rather than repeat the probes.

**The crash, read from the console's own dump.** `klog/run-PPSA99169-002106.log` and
its successors: SIGSEGV on a thread whose id differs from the process (a worker), fault
address `0x70`, `rip 0x8000407fb`, and a backtrace of
`0x80004055c, 0x800040638, 0x20185ea6a, 0x800074d0`.

**The console's libkernel can now be symbolized.** `/system/common/lib/libkernel.sprx`
is readable over FTP (545,752 bytes, plain ELF) and its 1,270 exports are named by
NID. SharpProspero's `NidEncoder` documents the mapping — the NID is the first eight
bytes of `SHA1(name || salt)`, byte-reversed and base-64-encoded with a custom
alphabet — and it validates against seven documented pairs (`puts=YQ0navp+YIc`,
`malloc=gQX+4GDQjpM`, …). Computing NIDs for the SDK's 4,328 libkernel/libc symbols
names 1,111 of that module's exports, which turns every console backtrace inside
libkernel into function names. That tooling is the reusable result of this round.

**What the backtrace says.** `0x8000407fb` is `pthread_get_specificarray_np + 0x3b`
(NID `td+DYvbbDfk`), reached from the thread-creation path `pthread_create_name_np`
(`0x800074d0`). Disassembling it shows why it faults: the function walks the frame
pointer chain, takes a return address two frames up, calls a table lookup that searches
0x98-byte records for the range containing that address, and reads `[result+0x70]`
without checking for null — with `rbx = 0x8000711d0` in the register dump matching
libkernel's `__stack_chk_guard`, which confirms the reading. The address it could not
resolve, `0x20185ea6a`, is the third backtrace frame: **a return address inside this
title's core, which the loader mapped anonymously.** The core is not a registered
module, so the lookup misses.

**What the probes ruled out.** `src/thread_probe.cpp` (opt-in, `/app0/thread-test.txt`)
now runs, and passes, every case that could be built in the title: 32 concurrent
`std::thread`s, `thread_local` access in each of them (the SDK compiles it as emulated
TLS), a heap object with `std::mutex` and `std::condition_variable` used by 1 and 32
threads, a thread whose entry point is in an anonymous executable page, a thread whose
entry point is a *core* export, a thread that calls core code, a libkernel call whose
immediate caller is an anonymous page, and a thread-specific-data call
(`pthread_getspecific`) from an anonymous page. All passed
(`{"stage":"done","created":32,"ran":32,"joined":32,"tls_ok":32}` and successors).
PPSSPP still dies at the same instruction, so the trigger is not any of those alone.

**Two changes stand regardless.** `patches/ppsspp/0005-ps5-thread-name.patch` stops
PPSSPP taking its `__FreeBSD__` branch — PS5 defines the macro, so every worker called
`pthread_set_name_np`, which no earlier core used — and
`patches/ppsspp/0006-diagnose-thread-pool.patch` is the temporary instrumentation that
named the failing step (`creating 0`, `created 0`, then death).

**The next attempt.** Remove PPSSPP's `thread_local` uses (the plan's original A2 step,
now motivated by the emutls path: core TLS becomes `__emutls_get_address` →
`pthread_getspecific`, which is exactly the family in the backtrace). If that is not
enough, the fix has to make libkernel's lookup succeed for core addresses, or keep
libkernel's thread-specific-data path off every stack that contains core frames.

## 2026-09-20 — relinked against the migrated driver with the red/blue compensation withdrawn

**Task.** Two asks: build the frontend against `../PS5_Vulkan` now that its migration
landed (`8b311e3`), and remove this port's red/blue compensation, which existed only
because that driver could not sample the frontend's own byte order.

**Driver review — `8b311e3`, "Migrate the driver to ps5-opengl 0.3.0's compiler fork,
and close the console SIGFPE at its owner".** The compile-side surface moved: the
driver links ps5-opengl 0.3.0's `opengnm-psbc` tree (upstream `a92a1228` + the
release's patch, tree `a27cbecc`, metadata v14) with this repository's compiler patches
on top, `patch-compute-metadata.py` is dropped, `patch-vertex-formats.py`'s anchor moved
and a new `patch-aco-min-waves.py` restores `program->workgroup_size`'s
"unknown is UINT_MAX" invariant before `calc_min_waves`. The console SIGFPE that round
15 had laid at ACO's door was the *probe's* own: earlier symbolization had missed the
PIE load base (`xotext: 0000000000400000`), and rebased, `rip 0x430b34` is `div %r13d`
in `run_sampled_format_frames`, dividing by an unfilled `std::array<SampledFormat, 10>`
row's zero texel size; both tables now declare their rows and a `static_assert` makes a
declared-but-unfilled row a build error (console: pid 299, seven of seven cases, 1581
PASS records, no `signal:`). Three fixes in the same commit matter here: `ps5vk_compile_stage`
dereferenced a null shader module for the NIR stages Mesa's meta operations hand it —
every meta clear, blit and resolve faulted — `ps5vk_queue_flip` recorded the wrong
submission's words, and shader compilation now runs on a driver-allocated 32 MiB stack
via `pthread_attr_setstack`, so an application thread's small stack cannot overflow the
compiler's recursion. **What the port depends on is unchanged**: `driver/ps5vk_image.c`
is not touched, `VK_FORMAT_B8G8R8A8_UNORM` still carries `SAMPLED_IMAGE_BIT` with the
`8_8_8_8_UNORM` word and the `ZYXW` selectors, and the format audit's counts are the same
(179 required, 58 reported, 0 missing, 55 conditional, split 0/0/0/0/0).

**Withdrawn: 0014's format substitution, 0075, 0077.** `tools/apply-port-patches.py`
grew an 0083 group (the substitution goes, the usage mask stays, and 0027's and 0015's
comments are restated for the reasons they still have) and a `WITHDRAWN` tuple that
exchanges each named insertion for the upstream text it replaced, so an existing tree
and a fresh one converge — checked by patching a copy of `vendor/retroarch`'s
`gfx/drivers/vulkan.c` and the withdrawn `build/ra-conf` and comparing (identical).
`src/core_frame_ps5.cpp` no longer converts channels: the upload is a row copy of
libretro's little-endian XRGB8888 into a B8G8R8A8 image, which is the same four bytes.
Four tests now assert the core's own bytes instead of the conversion's output, and two
new `tests/test_frontend.py` cases pin the mechanism (a withdrawal must name exactly one
EDITS block, and the withdrawn set is named). The patch-count pin moved 179 → 185.

**Build and gates.** `PS5_VULKAN_DIR=$PWD/build/frozen-driver
PS5_VULKAN_ICD=$PWD/build/frozen-driver/build/driver/ps5/libvulkan.so.1 bash
tools/verify.sh` — format PASS (1s), unit PASS (12s, 75 tests), build PASS (247s),
integration PASS (10s), evidence PASS (0s), `verify: PASS (format unit build
integration evidence)`. Title identity `f9e4ea6c…`, eboot 34,703,515 bytes sha256
`82534b86…`, and two consecutive gate runs built it byte for byte the same. The four
driver archives were frozen into `build/frozen-driver/` before the build and re-hashed
unchanged after it, and the printed identity was recomputed from `tools/build-title.sh`'s
own input list against those hashes and matched, so the eboot's embedded identity names
the driver it carries. Record: `evidence/driver-1.0-native-byte-order/`.

**Four runs before the green one, at the same detail.** 1) The first `tools/verify.sh`
failed at `unit`: three new 0083 markers did not appear in their own replacements — the
port pins that so a rebuild cannot append an edit twice — and the patch-count pin was
stale; both fixed (`build/verify-native-byte-order-first-run.log`). 2) The second failed
at `build` after 324 s because the frozen tree's `.deps/work` symlink was one level short
and the Mesa utility sources read as missing (`…-second-run.log`). 3) A third was
interrupted mid-build when the driver's `8b311e3` landed and the freeze was rebuilt
(`…-third-run-killed.log`). 4) The fourth passed format, unit, build and integration and
failed only at `evidence`, because the new step directory held no `capture.json` /
`expectation.json` yet (`…-gates-4-of-5.log`).

**Also found.** `tools/core-imports.py` rewrites `build/core_imports.inc`, and that file
is insertion-ordered by the core list: running the tool by hand to read a count changes a
build input and with it the build identity. The identity does not lie about what it
hashed — recomputing it from its own inputs reproduced both printed identities — but a
tree whose include was rewritten by hand no longer describes the eboot built from it.

**Pending, and the only thing that decides the colours: a console run.** The withdrawn
path is the one the owner accepted earlier (`native-core-loading/`, `mgba-native/`,
`xmb-safe-list-run/`), and the native byte order has host evidence plus the driver's own
console `v0-formats` battery. The command is
`JOBS=14 bash tools/run-title.sh --no-build --deploy --watch 120` with the owner
launching; the check is menu colours, core colours, and the Quick Menu over a paused
game.

## 2026-09-20 — the native-byte-order build is published (upload only)

`python3 tools/deploy-title.py` published the build from the previous entry and read it
back: 354 files, `eboot.bin` stored as 35,144,676 bytes (the console re-signs the fake
self; the tool re-uploaded once when the first read-back differed and then confirmed this
build's four markers), `libvulkan.so.1` at the title root and in `sce_module/` as
16,869,880 bytes with the build's library confirmed, and all six cores
SHA-256-verified. The console's own `sce_module/libc.prx` was kept, as designed.

The console was **not idle** when the deploy started: the control endpoint answered
`count=1 title=PPSA99988`. `tools/run-title.sh` refuses an upload in that state, so this
was the owner's explicit decision to publish anyway; no title was launched or closed by
this step, and the running title's files are not touched by the title folder's paths.
Record: `evidence/driver-1.0-native-byte-order/deploy.json`; the raw deploy log is
`build/deploy-driver-8b311e3.log` (git-ignored).

Still open, and the next thing to record: the owner's launch of this build, which is what
decides the colours.

## 2026-09-20 — the owner accepts the native byte order on the console

The owner launched the published build and reports it "flawless", with the colours
accurate now that the red/blue compensation is gone. That is the acceptance of this
step: the menu, the core frames and the transitions between them draw with the correct
channels while the frontend keeps libretro's own byte order, which is what the
withdrawal of 0014's substitution, 0075 and 0077 was for.

**What kind of evidence this is, and is not.** It is an owner observation, like the
accepted baselines before it: no klog run was started for this launch, so there is no
distilled capture, no `/app0/trace.txt` and no kernel log for it, and no per-core
checklist was walked. What is machine-checked about the deployed bytes is in
`deploy.json` (sizes, read-back and the six cores' SHA-256), and what is machine-checked
about the build is in `capture.json` (the five gates, the identity, and that the identity
covers the frozen driver archives). The acceptance itself is the owner's words.

**Not covered.** PPSSPP was not launched on this build, so its parked first-frame blocker
is unchanged and untested here; the other cores' individual behaviours were not
re-checked one by one; and there is no captured 90-second run for the build.

The step's record is `evidence/driver-1.0-native-byte-order/`: `capture.json` (gates,
identity, driver revision), `expectation.json` (what the run must show, now carrying the
owner's words as its human check), `driver-archives.json` (which driver the link carries)
and `deploy.json` (what was published and read back).

## 2026-09-23 — Rebuilds in seconds

A rebuild that changed nothing took 269 s: the six cores (255 s, PPSSPP 122 s and
FBNeo 77 s of it) were extracted and compiled from scratch every time, the host
signing tool was recompiled every time (6.5 s), and the patcher rewrote files it
did not change (0075/0077's withdrawn text was inserted and withdrawn again), so
their objects recompiled.

- tools/core-stamp.sh: each core is skipped when a hash of its build script (which
  pins revision and digests), its patches and port tooling, the shared loader
  files, the ABI checker and the SDK compiler wrappers matches the stamp and its
  staged outputs exist. PS5_FORCE_CORES=1 builds anyway.
- ccache in tooling/prospero-clang18 (frontend and title sources) and on the
  cores' make lines and CMake launchers; PS5_DISABLE_CCACHE=1 opts out.
- tools/apply-port-patches.py computes each file's final text in memory and writes
  only when it differs.
- tools/build.sh rebuilds the host tool only when its sources, headers, zlib or the
  host compiler change.
- tools/deploy-title.py uploads only files whose digest differs from what this
  checkout last verified on the same console (build/deployed/<host>-<title>.json);
  --all uploads and verifies everything.

Measured on this host: no-change rebuild 269 s -> 2.2 s; a port-file edit 2.4 s;
a driver archive change 2.4 s (relink, sign, manifest); a forced FBNeo rebuild with
a warm cache 77 s -> 2.8 s; the first build after these changes 304 s (fills the
stamps and the cache). Deploy: full 74.4 s, nothing changed 0.3 s, after a
port-file edit 12.4 s (eboot.bin and the metadata). build/ is only removed by
`make clean`.

## 2026-09-23 — 120 Hz by default, 60 Hz fallback, on the current driver

The title declares high-frame-rate output (attribute3 0x80040) and links
../PS5_Vulkan 8577153, which offers 3840x2160 at 119880 mHz first and selects it at
swapchain creation. Patch 0084: the display context picks the largest mode, then
the highest refresh. Patch 0085: `video_refresh_rate` follows the chosen mode and
a saved swap interval of 1 becomes automatic above 100 Hz. The seed config ships
119.88 and automatic for fresh installs. main.cpp drops `--menu` when args.txt
names a core or content.

Console, FCEUmm with 1943 for 1,800 frames through args.txt, driver profile armed:
119.88 Hz selected, `video_refresh_rate` 60.000 -> 119.880, swap interval 1 made
automatic, 1,199 presents per 10 s (8.342 ms), ~30 s for the 1,800 frames of a
60.10 FPS core (evidence/output-120hz). Same build with attribute3 0: 59.94 Hz only,
119.880 -> 59.940, 600 presents per 10 s, the same ~30 s (evidence/output-60hz-
fallback). My configuration and history were snapshotted before and restored to
their exact bytes after. tools/verify.sh PASS (five gates, 75 tests, patch count
185 -> 188 for 0084's two edits and 0085), 35 captures replay.

## 2026-09-23 — VideoOut kept across swapchains; every core rechecked; FBNeo screenshot fixed

Black screen on menu open/close, content close/load and video reinits: RetroArch
recreates its swapchain for each, and the driver closed VideoOut with it (restoring
60 Hz) and reopened it (selecting 119.88 Hz). ../PS5_Vulkan now keeps one VideoOut
per process: `ps5vk_display_retain(true)` (main.cpp) keeps it, its framebuffers and
the image on screen between swapchains, on any device; `false` (on quit) closes it
and restores the default mode. The same driver change configures 119.88 Hz when the
display modes are listed, so the mode is offered only once the console accepted
it, closing the edge where a refusal left RetroArch believing 119.88 Hz.

Console, FCEUmm 1943 with a pad script (menu, Close Content, History run, menu
twice): retained, every handover kept the output with 0 black presents (menu 6-7 ms,
close content 280 ms, History 437 ms); with /app0/ps5vk-no-retain.txt the same
events opened a new output each time, 297-760 ms and a mode switch each. Quit from
the main menu: "output: default mode restored", status 0
(evidence/display-retention).

Core recheck at 119.88 Hz, 1,200 frames and an exit screenshot each: FCEUmm,
Snes9x, mGBA (GBA .7z, GB), FBNeo, Genesis Plus GX (MD, SMS, GG), all correct
colours, audio errors=0 discarded=0, status 0 (evidence/core-recheck-current-
driver). FBNeo's exit screenshot crashed: a new crash reporter (src/crash_report.cpp)
put the return address in scaler_ctx_scale via screenshot_dump_direct; the scaler
had failed to allocate ~97 MB of intermediate frames a same-size conversion never
uses. Patch 0086 allocates them only for scaling and fails a screenshot whose
conversion cannot be set up. The first Master System and Game Gear files were web
pages saved under ROM names; RetroArch refused them and exited cleanly, and the
replacements ran.

vkQuake on the same driver: 119.88 Hz selected, 119.88 FPS at 4K. tools/verify.sh
PASS (patch count 188 -> 193 for 0086's five edits), 37 captures replay; driver
check PASS.

## 2026-09-24 — God of War: Ghost of Sparta at 10x: correct picture, full speed

The run is PPSSPP with the torture defaults (4800x2720, 16x anisotropy, auto
max-quality filtering) from a slot-0 save state, loaded with `--entryslot=0`.

- **Picture.** The blue, banded Kratos in the menu and the brown tint with
  flat-silhouette characters in gameplay were stale GPU state in
  ../PS5_Vulkan (a draw left the previous draw's blend, write mask, cull and
  clip words in place). With every draw recording them, both match PPSSPP's
  software renderer (`klog/final-menu-ss-gow.png`, `klog/fifo-ss-ss-gow.png`).
- **Speed.** The driver's present used to wait until its flip was shown,
  which gave RetroArch's doubled 120 Hz presents one vblank per frame. Its
  swapchain is now a three-image FIFO queue: the audio windows went from
  ~420,000 of 480,000 frames with silence filled in (`klog/pace1-trace.txt`)
  to 480,000 of 480,000 with none (`klog/pace2-trace.txt`), at 1198 flips per
  10 s. Sonic on Genesis Plus GX runs at full speed with the same driver.
- **Startup.** About one launch in three aborted with `thread constructor
  failed: Resource temporarily unavailable`; the core thread factory now
  retries EAGAIN for up to two seconds, and the terminate handler logs an
  exception's what() and code.

Still open: 8x MSAA (the driver exposes neither renderpass2 nor depth/stencil
resolve, and supports 4x only), and a long gameplay soak.

## 2026-09-24 — Dolphin boots Wind Waker; save states work

libretro/dolphin (`c6630001e0`, Dolphin 2609) cross-builds with one patch and
ships in the title. In order, what stopped it on the console and what fixed it:

1. `openat` resolved to address 0 inside the title's libc++ (`remove_all`); the
   `*at` family and libc's refused `opendir` are now the title's own (`--wrap`).
2. VMA could not load `vkGetPhysicalDeviceMemoryProperties2KHR` on RetroArch's
   Vulkan 1.0 instance: patch 0089 enables the extension when offered.
3. `vkCreateSampler` refused clamp-to-border: ../PS5_Vulkan R57.
4. Jit64's 200 MB code space did not fit flexible memory: 32 MiB on PS5.
5. The driver's compiler faulted in `copy_entry_create` on a NULL realloc
   from libc's exhausted private heap: a refused realloc now moves the block to
   direct memory (located with the driver's new opt-in SPIR-V dump; the same
   modules compile on the host).
6. The fastmem logical views failed to map and flexible memory fell to 10 MB:
   guest RAM is now direct memory mapped at each mirror.
7. JIT code first landed inside the GPU window, then its hint was refused:
   code is mapped at 0x3_0000_0000.
8. 101 pipelines were refused for primitive restart: ../PS5_Vulkan R58.

Wind Waker then reached its title screen with zero refusals and clean audio
(dol12). I first recorded that screenshot as correct; it is not, because the
mountain in the background is missing, which is open.

Save states: saving crashed in the thumbnail readback, which overran its buffer
by ~1 MB after re-rendering the cached frame recomputed the viewport (patch
0090). Loading a state then crashed at exit on a NULL strdup: libc's private
heap was exhausted because the title's small blocks filled it first; they now
go to direct memory first (dolss1-3, dolld1-2). The startup popup "Failed to
load Vulkan library" was Dolphin probing a host library a libretro core does
not use; it is an info line now.

tools/verify.sh PASS (patch count 199), 39 captures replay.

## 2026-09-24 — Wind Waker's 3D scene: two primitive-restart faults in the driver

Wind Waker drew its 3D scenes as a huge near-camera surface whose colours
changed every frame. The method was a FIFO log recorded on the console (debug
mode `fifo`, ten frames of Link on the ship deck) played both by the core
(single core: FIFO playback hung in `CPU::Run()` before `Core/Core.cpp`'s fix)
and by desktop Dolphin built from the same tree, then bisected by object range
(debug mode `objects A B`).

1. Objects 0-89 matched desktop Dolphin and object 90 added the surface: the
   first draw past 2048 indices, 398 short restart strips. With Dolphin drawing
   lists instead of restart strips it matched. ../PS5_Vulkan R64 reduced it to a
   probe: the driver turned restart off with a bare register write right after
   each restart draw, and the write landed while the draw was still fetching
   indices. The enable is now state, each change behind an SQ_NON_EVENT (as
   RADV writes it).
2. After R64, thin triangles ran from the screen's corner to the minimap, and
   the minimap's panel spilled to the left. Objects 310-319 (the minimap)
   isolated them; neither the software vertex loader nor skipping line and
   point draws changed anything, and lists instead of restart strips fixed
   them. ../PS5_Vulkan R65: every submission a copy splits off starts with
   restart off on the GPU, while the recording still held it on.

Evidence (`evidence/dolphin-restart-strips`): the FIFO log's screenshots
against desktop Dolphin, mean difference per channel 64-71 before, 1.23-1.53
after R64, 1.13-1.16 after R65 (resampling between the console's and the host's
resolutions). My save state on Outset Island loads and draws cleanly, with no
audio errors.

Also: the title's stale-test-file list now covers `/app0/dolphin-debug.txt` and
`/app0/ps5vk-shader-cache-dir.txt` (a test hook a previous run had left on the
console, which redirected the driver's shader cache). The core patch keeps the
debug modes, the options override, the FIFO recorder, the object-range bisect
(debug mode only) and the single-core FIFO playback fix; the temporary host A/B
hooks are gone.

## 2026-09-25 — Dolphin acceptance: the three games, Profile 0

The acceptance targets are Resident Evil 4, Super Smash Bros. Melee and Wind
Waker, each from my save state, through the profiles in
`tooling/dolphin-profiles/`. A run is summarised from its trace: audio windows
(played against silence, which is what full speed reads as), the driver's
presents and GPU time a present, hitches, errors and screenshots on SELECT,
which the GameCube pad ignores.

1. **RE4 crashed at boot** (SIGSEGV writing JIT code at 0x3808000). Dolphin's
   JIT maps 56 MB of executable memory from the title's flexible memory (about
   450 MB), and found 45 MB: RetroArch's state-load task had just read RE4's
   260 MB entry state into one buffer, and the frontend's `malloc` gave buffers
   that large an anonymous mapping -- flexible memory -- before direct memory.
   Large frontend buffers now take direct memory first (`src/memory_ps5.cpp`),
   leaving flexible memory for what only it can hold: executable code and
   thread stacks. The host test asserts the order and the fallback; its stand-in
   direct heap is now locked like the real one. The core's allocator reports the
   flexible memory free when an executable mapping fails.
2. **RE4 drew its frame in its haze colour.** A FIFO log of the scene plays
   correctly in desktop Dolphin; desktop Dolphin reproduces the console's frame
   with dual-source blending forced off. ../PS5_Vulkan R71 added dual-source
   blending (the compiler already exported the second source); RE4 now draws
   Leon, the car and the forest as desktop Dolphin does.
3. **Melee ran at 95-99%.** Its EFB copies split every frame into ~105
   submission steps, each a CPU wait; ../PS5_Vulkan R70 made them GPU barriers
   (0.07 steps a present). Melee still shows up to 1% audio silence in some
   windows, with shader-compile hitches during play; that is the next lead.

Profile 0 on the R71 build: RE4 and Wind Waker 100% in every steady window,
correct pictures; Melee 96-100%.

## 2026-09-25 — Profile 1; one frame per retro_run, so the display paces Dolphin

Profile 1 (the accurate baseline: safe texture cache, scaled EFB copies, GPU
texture decoding, exact depth) needed two driver rounds and one fix in the
core's libretro glue.

1. **RE4: "Failed to allocate 65536 bytes from texel buffer".** GPU texture
   decoding streams through a texel buffer Dolphin sizes by
   maxTexelBufferElements, which the driver reported as Vulkan 1.0's minimum.
   ../PS5_Vulkan R72 reports the descriptor's 32-bit record count, as RADV does.
2. **Wind Waker: 32 frame periods over 40 ms in 130 s**, at full speed. The
   title's CPU sampler found the driver re-reading AGC's colour-target
   defaults for every target of every rendering; ../PS5_Vulkan R73 reads them
   once (32 → 14).
3. **The other 14 were pacing, not work.** With the sampler's threshold just
   above the game's 33 ms frame, every late frame had the CPU thread blocked in
   the audio driver's write and the main thread waiting for it. libretro asks
   for one video_cb per retro_run, NULL for a repeated frame; Dolphin's
   presenters call it only for a frame they draw, and with duplicate XFBs
   skipped (the default) a 30 FPS game's every other field drew none. Those
   runs returned without presenting, so nothing waited for the display and the
   audio writes alone paced them. Presenting every field (`dolphin_skip_dupe_frames`
   off) left no late frame, which confirmed it. The core now hands RetroArch
   NULL for a skipped duplicate at the point Dolphin would have presented it
   (`Presenter::ViSwap`), and at the end of a run that output no field.
   A first version sent it only at the end of the run: no late frames, but the
   frontend's present then waited on the display after the frame step had
   stopped the CPU thread, and the audio ran 37-7268 frames short in four of
   five windows. Both are kept: `evidence/dolphin-pacing-before`,
   `evidence/dolphin-pacing-end-of-run`, `evidence/dolphin-pacing-duplicates`.

Profile 1 on title f2585b8f (`tooling/dolphin-profiles/p1-accurate.txt`,
screenshots on SELECT): RE4, Melee and Wind Waker draw as before, 1,200
presents per 10 s, every window without a screenshot 480000 played and 0
silent. Every late frame left is either the state load (the first 1-4) or a
screenshot: RetroArch's read-back of the 4K output costs 55 ms in the driver's
copy path, and a window with one runs ~2000 frames (0.4%) short. Under display
pacing the audio buffer is kept half full (32 ms) rather than full, so a stall
that long now empties it; before, a screenshot window was ~750 frames short in
RE4 and Wind Waker and 2000-3800 in Melee. The driver's copy throughput (the
55 ms read-back, a 218 ms copy in RE4's load) is the next lead, and matters
most for Profile 5's EFB copies to RAM.

## 2026-09-25 — Profile 2: five swapchain images at 120 Hz

Profile 2 (synchronous ubershaders, 1x and 6x) ran RE4 and Melee at full speed
at both resolutions, and Wind Waker at 89-99% at 1x and 80-84% at 6x with the
GPU busy 0.7 and 2.2 ms a present: slow at 1x, so CPU or presentation, not GPU
throughput.

- The driver's new `cpu_copies` profile field showed Wind Waker reading back
  40-90 images a second (EFB copies encoded to RAM, and the EFB peeks its game
  settings enable), each a wait for the GPU; ubershaders lengthen what each
  wait covers.
- The CPU sampler at a 1 ms threshold found neither thread busy: the main
  thread waited in vkAcquireNextImageKHR and the CPU thread between frame
  steps.
- A one-shot timeline of retro_run (removed since) showed why. RetroArch
  emulates a swap interval of 2 at 119.88 Hz by presenting each frame twice,
  and three images leave room for two queued flips: one frame. A new frame's
  run took 7 ms and queued both presents at once; the repeated field's run
  then waited 13-14 ms for two vblanks inside its present, and Dolphin's GPU
  thread -- the thread that waits -- cannot drain the FIFO while it does, so
  the CPU thread stalled behind it. The pair of runs crossed 33.4 ms often
  enough to lose 4-12%.
- A temporary driver with five images and `video_max_swapchain_images = 5`
  ran the same state at 100%.

../PS5_Vulkan R74 gives a swapchain the images it asks for, three to five, and
patch 0091 has RetroArch ask for (video_max_swapchain_images - 1) * interval + 1
when it emulates an interval. The default of three becomes five at 120 Hz with an
interval of 2: two frames of lookahead, the same frames and the same 33 ms as three
images at 60 Hz, so the latency is what a 60 Hz display has.

Title 5beca7cc (`evidence/dolphin-p2-swapchain-before`, `-after`): Profile 1 and
Profile 2 at 1x and 6x on all three games, 1,200 presents per 10 s and every
steady window at 100% (99% in one screenshot window). Melee's first run on a new
build still stalls 30-95 ms on each pipeline it compiles for the first time: the
driver's shader cache is per build, and Profile 8 is where that is measured.

## 2026-09-25 — Profiles 3-7: torture, EFB, the scaling ladder and MSAA

On title 5beca7cc (../PS5_Vulkan R74) and, for MSAA, its successors:

- **Profile 3** (6x, ubershaders, 16x AF, forced filtering, every frame
  presented), **4** (6x GPU EFB copies) and **5** (6x EFB copies to RAM):
  RE4, Melee and Wind Waker at 100% in every steady window, correct pictures.
  RAM EFB copies read back 270 images a second in RE4 at full speed; Wind
  Waker's game settings already send its EFB copies to RAM, so its Profiles 4
  and 5 are the same run.
- **Profile 6**, 1x to 6x at the accurate baseline: all eighteen runs at 100%,
  1,200 presents per 10 s; Wind Waker's GPU time grows from 0.21 to 1.37 ms a
  present, Melee's and RE4's stay under 0.2 ms.
- **Profile 7**: 4x MSAA first stopped every game at its first resolve. The
  driver refused to sample a four-sample image and could not resolve into
  Dolphin's resolve texture, which it stores in rows; ../PS5_Vulkan R75-R77 added
  multisampled sampling, GPU resolves and rendering into rows. R78 added 2x and
  8x and moved the sample locations to the registers of every pixel of the 2x2
  quad (C8 had left the lower two at AGC's defaults). 2x, 4x and 8x at 1x: all
  three games at 100%, antialiased. The maximum torture (6x, 8x MSAA,
  ubershaders, 16x AF): correct and stable at 92-97% (RE4), 72-77% (Melee) and
  60-64% (Wind Waker), GPU-bound -- the uncompressed 8x surfaces at 6x
  quadruple the GPU work of Profile 3.

## 2026-09-25 — Profile 8, the shader cache; Profile 9, reloads

**Profile 8.** Profile 1 on each game twice with the driver's shader cache in a
new, empty directory (`CACHE_DIR`), then again with the cache that run left
(title cbcf5b03). Cold, the pipelines compiled for the first time stalled the
game 88 ms in total (RE4), 124 ms (Melee) and 156 ms (Wind Waker); Melee lost
3% of one window. Warm, nothing compiled, every steady window was at 100% and
Melee's late frames fell from 19 to 6. The test caches were removed afterwards;
my own cache was not touched.

**Profile 9.** Four reloads of each game in one process: the menu opened and
closed ten times, three state loads, a screenshot, then Close Content through
the Quick Menu and the content loaded again with its core, a state load and a
screenshot after each (`<seconds> RELOAD`, a new pad-script action that pushes
the same load the menu's history does, with the core and content from
`/app0/args.txt`). No crash, no refused call, the right picture after each load,
and 100% in each window without a menu or a load in it.

- A first pad-script action that closed the content itself crashed Dolphin: it
  unloaded the core from the input poll, which runs inside retro_run, and the
  core's next step read freed state. That is the test aid's fault, not the
  core's -- a menu close happens between runs -- so the action was removed and
  the script closes through the Quick Menu as a player does.
- The driver's new `direct_live` field showed each reload keeping 13 more
  direct-memory mappings (title 26c9daa0). This port's patch 0021 was the
  cause: it creates wide default and blank textures after upstream's 1x1 and
  4x4 ones and stores them in the same slots, so the small ones -- an image,
  its memory and its view each -- were never destroyed, on every swapchain
  change for the default and every video init for the blank. Patch 0092
  releases each small texture before its slot is reused.
- The same count was wrong in one direction: destroying a graphics pipeline's
  zeroed compute mapping released direct memory at offset 0 and took a mapping
  off the count. ../PS5_Vulkan now releases and counts only a mapping with
  bytes.
- With both (title 9c623566; `evidence/dolphin-p9-reload-before`, `-after`),
  the count grows by 2 a reload (182, 184, 186, 188
  at each reload's first state load) and the driver's bytes stay at 592 MiB; before,
  its bytes rose from 592 to 593 MiB over the same four. Flexible memory drops
  2 MiB once, after the first reload, and then holds. The two mappings a reload
  are small -- under the MiB the profile resolves -- and still unidentified.

Two side effects of my earlier test runs remain on the console: RetroArch's
content history and runtime logs recorded the test loads, and the FTP account
may not change `playlists/` or the core's state directory, so I cannot clean
them from here. Every run now turns history and runtime logging off in its
configuration, and Profile 10 keeps its states in a directory of its own.

## 2026-09-25 — The title's folders and files are reachable over FTP

FTP answered `550 Permission denied` for `savestates/dolphin-emu`, so my save
states could not be copied off the console. The title runs as its own user and
the FTP server as another, so only the bits for everyone else count. The
frontend gave its seven top-level folders 0777, but RetroArch creates every
folder below them 0750 (libretro-common's `retro_vfs_mkdir_impl`), its
unbuffered files with mode 0, and the rest under the process umask.

`src/permissions_ps5.cpp` wraps `mkdir`, `open` and `fopen` at link time (a
core's imports of them bind to the same wrappers), and each wrapper gives what
it creates 0777 for a folder and at least 0666 for a file. A first build that
cleared the umask instead jumped to address 0 at start: the console's libraries
do not resolve `umask`. At each start a thread walks /app0 and gives every
folder and file the bits it lacks. Only modes change; no contents are touched.
From inside the title every entry's owner reads as uid 0, so the walk cannot
skip what FTP uploaded and covers all 17,551 entries: 389 ms, which is why it
runs beside the frontend's start, not before it (141 ms on a warm cache).

The first start repaired 42 folders and files, none refused. FTP now lists
`savestates/dolphin-emu` (`drwxrwxrwx`) and its states (`-rw-rw-rw-`); the next
start, after a full Profile 10 run that wrote states, screenshots and logs,
found nothing to repair.

## 2026-09-25 — Profile 10: save-state stress

Each game from a copy of my entry state in a test directory, `/app0/p10-states`
(the title makes the copy, `/app0/state-copy.txt`, because FTP could not read my
states then): 15 saves and 15 loads three seconds apart, then 6 of each 0.6 s
apart, and screenshots between them. RetroArch saved into the entry slot, so the
test states replaced only the copy; my own states were read and never written.
The test directory was emptied afterwards.

Title 718b41d7 (`evidence/dolphin-p10-state-stress`): Melee, RE4 and Wind Waker
each save 21 times and load 22 times with no error and no crash, and every
screenshot after a load shows the loaded scene correctly. Windows with no state
operation run at 100%. Windows with three or four operations run at 96-100%
(Melee), 97-100% (RE4) and 94-98% (Wind Waker), because each load stalls the game.
Flexible memory holds at 243,712 KiB throughout. The driver's direct mappings level
off in RE4 (256 to 265) and Wind Waker (346 to 382). In Melee they rise from 266
to 333 and were still rising when the run ended, so Profile 11's soak is where that
is decided.

Where a load's stall goes (Wind Waker, a bounded timing capture in the core,
removed since): RetroArch's serialize takes 13-20 ms and unserialize 25-76 ms.
The two fields after a load then take 130-245 ms and 80-170 ms, because Dolphin
clears its JIT cache on every load and recompiles what runs: 6,000-9,400 blocks,
about 20 µs each, 124-219 ms of the first field. The driver's part of those
frames is a few milliseconds. So a load costs 250-400 ms and a save about 20 ms,
the same cause desktop Dolphin has. The CPU sampler first pointed at an INI file
written on the main thread; that was Dolphin's configuration saved at boot,
outside the stress.

## 2026-09-25 — Profile 11 so far: Melee 30 minutes, RE4 18

Title 718b41d7, the accurate profile, from my entry states, a screenshot every
five minutes, driver profile armed:

- **Melee, 30 minutes:** no crash; 184 of 185 windows at 99% or better, the
  exception being the first (boot and the entry state). The driver's direct
  mappings reach 356 within the first seven minutes and hold there to the end,
  so Melee's rise in Profile 10 was a plateau being reached, not a leak.
  Flexible memory holds at 243,712 KiB. Each screenshot costs its 55 ms read
  of the 4K frame; three stalls in Dolphin (129, 86 and 87 ms) took their windows
  to 99.0-99.1%.
- **RE4, 18 minutes** (stopped to free the console for other testing): no crash,
  every window after the first at 99.6% or better, direct mappings 261-262 and
  flexible memory flat.

Still to run: RE4 and Wind Waker for 30 minutes and one game for an hour.

## 2026-09-25 — Mario Kart Wii's black scenery; Rogue Leader's FIFO desync

**Mario Kart Wii** (my save state, Luigi Circuit): the grass bank and palm tree
left of the track drew black. Dolphin asks for samplers biased -2.1875 to
-3.1875; the driver reported a limit of 2 and refused them (three refusals in
100 s, title ad98b960). ../PS5_Vulkan R79 reports the +/-16 its sampler word
holds, proved by a console probe; the scenery draws (klog/mkw-base-*, mkw-r79-*).

**Rogue Leader** crashed for a tester after about three minutes. From my save
state (Death Star Attack) it plays without error; from boot, the attract
sequence fails at the same point every run: a burst of "Negative
fifo.CPReadWriteDistance" alerts -- 221,472 in one run -- then commands decoded
from stale FIFO words, then a fault in LoadIndexedXF, the tester's crash. It
needs dual core: single core runs past that point with no alert, and without
fastmem the desync still happens, earlier and milder.

A bounded one-shot capture (removed since) recorded the FIFO register writes and
the GPU loop's starts and stops, dumped at the first negative distance: the GPU
loop resumed at a field with one chunk still queued, the game -- resuming at the
same moment -- switched GP reads off and wrote 0 to the distance, and the loop,
already inside that chunk, subtracted 32 from the zero. A negative distance
reads as more to fetch, so the loop fetched the FIFO's old words as commands.
Standalone Dolphin has the same unguarded window, but its GPU thread is almost
always idle when a game resets its FIFO; the libretro core restarts the loop
with a backlog at every field, exactly when the game runs again.

The fix (patches/dolphin/ps5-port.patch): the CPU's writes to the read pointer
and the distance are counted before they land, and the dual-core GPU loop
commits a chunk -- read pointer past it, distance a chunk shorter -- by
compare-and-swap, only if no such write came while the chunk was in flight. The
CPU's values win, as a CPU write does on the hardware; a gather-pipe burst, which
only adds, is retried. Title 08e8ed4c, twice from boot for seven minutes (368
emulated seconds, past the old failure and through a second attract loop): no
alert, no unknown opcode, no crash.

## 2026-09-25 — Profile 9's last two mappings: the HDR uniform buffer

Profile 9 left two direct-memory mappings a reload unidentified. A bounded
one-shot registry in ../PS5_Vulkan's allocator (removed since; title 1ecbfd3a)
recorded the caller of every live VkDeviceMemory. After each reload, one entry grew
by 2 (1, 3, 5, 7, 9 over four reloads): 256 bytes of host-visible memory from
`vulkan_create_buffer`, called by `vulkan_init` for `vk->hdr.ubo`. RetroArch
creates that uniform buffer whenever the build has `VULKAN_HDR_SWAPCHAIN`, but
`vulkan_free` destroyed it only when the context has HDR support, which this
console's surface never reports. The video driver is initialised twice a reload,
so two buffers leaked each time.

Patch 0093 releases the buffer whenever it exists; the HDR images stay with the
support flag that creates them. Title 9a84b116, the same four-reload Melee run
(`evidence/dolphin-p9-reload-flat`): every reload returns to `direct_live=180`
and `memory:152`, the count the first boot held. Before 0093, memory grew 152,
154, 156, 158, 160. The stage and table mappings return to the same counts each
cycle. There were no crashes, and each steady window ran at 97-100%.

## 2026-09-25 — Rogue Leader's slow attract sequence: which layer owns it

A tester reported Rogue Leader stuttering at first and then lagging in video and
audio. From my save state (Death Star Attack) it runs at 100% once the shaders
it needs are compiled. From boot, the attract sequence's in-engine fly-bys drop
to between 52% and 81% for a minute at a time. I profiled two runs from boot
with the title's sampler on the main thread (Dolphin's GPU loop and the driver)
and Dolphin's CPU thread, both on title 9a84b116 (klog/rl-prof-*, klog/rl-prof-warm-*).

- **Cold** (the first run after a driver build; the driver's shader cache is
  one directory per driver build, so every rebuild starts empty): 392 shader
  compiles land in the hitch reports, 4.6 s in all. In the worst window (52%)
  the main thread spends 45% of its samples compiling and 9% opening cache
  files that are not there yet.
- **Warm** (the same sequence straight after, the cache filled): 20 compiles
  and 0.25 s, but the same stretch still runs at 65-81%. Dolphin's CPU thread
  is idle 97% of the time, waiting for its next frame. The main thread is the
  limit: about 30% of it is the driver's `ps5vk_queue_run_step` spinning until
  the GPU finishes a step, so that a copy the driver still performs on the CPU
  can run; 11-14% is `ps5vk_flush_cpu_cache`, the driver flushing the CPU's
  cache lines for every draw's descriptor tables, registers and constants.

So the owning layer is the driver. The first run's stutter is shader compilation
(inherent, and cached from the second run). The lasting slowdown is the
driver's CPU cost on the thread that runs Dolphin's GPU emulation: CPU-side
copies that split a submission and wait for the GPU, and a cache flush per
draw. Neither is Dolphin's, and both will cost PS2 workloads more, so they are
in ../PS5_Vulkan's round plan for the LRPS2 port (docs/LRPS2_GAPS.md) and not
fixed here.

The warm run is itself an interrupted test. I stopped its harness mid-launch,
the title ran by itself until its frame limit, and I fetched its trace and
screenshots afterwards and removed its test inputs from the console.

## 2026-09-25 — Profile 11 complete: RE4 and Wind Waker 30 minutes, Wind Waker an hour

Title 9a84b116 (driver 2f1e447, patch 0093), the accurate profile, from my
entry states with no input, the driver profile armed, a screenshot every five
minutes (every ten in the hour-long run):

| Run | Windows | After the first | Direct mappings | Flexible memory |
|---|---|---|---|---|
| RE4, 30 min | 185 | all at 99.5% or better | 256 → 264 (666-668 MiB), flat after the first quarter | 245,760 KiB throughout |
| Wind Waker, 30 min | 185 | all at 99.6% or better | 349 (680 MiB) start to end | 245,760 KiB throughout |
| Wind Waker, 60 min | 365 | all at 99.6% or better | 349 (680 MiB) start to end | 243,712 KiB throughout |

None crashed. The first window of each is the boot and the state load (72-83%):
its hitches are the state load (1.0-1.1 s) and, in Wind Waker, the first
shader compiles (116 in 1.5 s at 30 minutes; 408 ms at 60, the cache warmer).
Every later hitch is a screenshot's read of the 4K frame (55-64 ms), which
leaves its window 0.4% short, and one 40 ms stall in the hour. The last frame
of the hour shows Outset Island correctly. With Melee's 30 minutes (above),
Profile 11 passes for all three games, and nothing grows: the mappings each
game holds level off within minutes and flexible memory does not move
(`evidence/dolphin-p11-soak30-re4`, `-soak30-ww`, `-soak60-ww`).

## 2026-09-25 — LRPS2's first boots on the console

LRPS2 (my fork, ../PS5_LRPS2, branch ps5-port) is built by tools/build-lrps2.sh
at its pinned revision with patches/lrps2/ps5-port.patch and staged in the
title. BIOS-only runs, from my test options and test folders
(/app0/lrps2-test), BIOS 2.30 (USA):

- **First load:** the core loader stopped on `fstatat`, which the SDK links
  and the console resolves to nothing. The core's libretro-common now stats
  directory entries by joined path on the PS5, as it does on the PS4.
- **Second:** SIGSEGV in libretro-common's process barrier, whose x86 page
  flip faulted on its write after mprotect. The PS5 now takes the barrier's
  NONE tier, like the other multi-core consoles.
- **Software renderer:** the BIOS reaches its main menu and runs 150 s with no
  crash and every audio window at 99.6% or better. Sampled every 10 s, the
  menu is right from 10 s to 100 s; at 110 s and 120 s the whole picture sits
  lower in the frame. I have not yet established whether that is the BIOS's
  own idle behaviour or an emulation fault: my BIOS never leaves the console,
  so there is no desktop reference for this frame, and it is the first thing
  the hardware renderer's run will be compared on.
- **Vulkan renderer:** the core loaded libvulkan itself and ignored the
  vkGetInstanceProcAddr the frontend passes it; it now uses the frontend's
  when no library loads. With ../PS5_Vulkan R81-R84 it creates its 1.1
  device and GS resources; its draws then met the driver's dynamic line width
  refusal (R85, fixed) and then its refusal of a non-indexed draw with a
  first vertex, the next driver round.

Everything the runs created is reachable over FTP (0777/0666). The shared
memory cards and empty per-core folders they left outside the test folder
were removed afterwards; my own saves, cards and configurations were not
touched.

## 2026-09-26 — The payload SDK fork and its platform layer, adopted

The title and every project now build with my fork of the payload SDK
(../PS5_PayloadSDK, pinned in tools/setup-native-dependencies.sh): the v0.42
release binaries with the fork's headers and its platform layer installed over
them (docs/PLATFORM_FIRMWARE_ANALYSIS.md has what the layer rests on). What
moved into it, and out of the title and the cores:

- **The machine context.** The fork's `sys/_ucontext.h` declares the console's
  48 extra bytes, so `uc_mcontext.mc_rip` is the faulting instruction. The
  crash reporter, the sampler and the Dolphin, PPSSPP and LRPS2 ports read the
  header's fields; the six-word offset each carried is gone. The probe built
  with the fork's header finds rip at its field.
- **The libc gaps.** `src/ps5_directory.cpp` and most of `src/libc_shims.c`
  are the platform's `ps5_*` functions now. `src/platform_wraps.c` binds the
  title's `--wrap` links to them, and tools/core-imports.py binds a core's
  imports (the whole `*at` family, `utimensat`, `futimens`,
  `clock_nanosleep`, `arc4random*`, `statvfs`/`fstatvfs`). The directory
  fixture test moved to the fork with the code. The platform's `statvfs`
  refuses a path that does not exist, as a desktop build sees it; the title's
  answered for any path.
- **Executable code.** Dolphin's `AllocateExecutableMemory`, PPSSPP's, and
  LRPS2's code area come from the platform's executable direct memory, near
  each core's own code, instead of anonymous flexible memory. The code caches
  are upstream's sizes again: Dolphin's near and far caches 128 + 64 MiB (32
  + 16 before), LRPS2's recompilers 305 MiB (161 before). Dolphin frees its
  code blocks through `FreeMemoryPages` off iOS, so that returns platform
  memory too.
- **Guest memory.** Dolphin's `MemArena`, PPSSPP's arena (a shared-memory
  object before, every view charged to flexible memory) and LRPS2's memshm are
  the platform's direct-memory objects, views and reserved ranges. PPSSPP maps
  its VRAM view 80 KiB into its guest memory; the platform first refused a view
  off the 64 KiB allocation unit (PPSSPP's memory init failed). The probe then
  measured a view at a 16 KiB page offset mapping and aliasing correctly, and
  the platform now takes page offsets.
- **Reporting.** A profiled run's memory line is followed by the platform's:
  `memory: platform exec=<regions>/<MiB> shm=<objects>/<MiB> views=<n>
  ranges=<n>/<MiB>`.

Measured on the console with this build, against the runs before it:

| | before | now |
|---|---|---|
| Dolphin, flexible memory free in game (RE4, Melee, Wind Waker) | 243,712 KiB | 303,104 KiB |
| Dolphin, JIT code | 48 MiB of flexible memory | one 200 MiB direct region (288 MiB with the MMU: Rogue Leader) |
| LRPS2, code area | 161 MiB, 163,840 KiB flexible left | 305 MiB, 328,704 KiB flexible left |
| PPSSPP, guest memory | a shared-memory object, views in flexible memory | 144 MiB of direct memory, 19 views |

- **Dolphin, Profiles 0 and 1** on Melee, RE4 and Wind Waker: no crash, every
  steady window at 100% but one Melee window at 98% in each profile.
- **Dolphin, Profile 9** (Melee, four reloads): no crash; the platform holds
  one code region, one guest-memory object and one arena after each reload,
  so each reload returns them; flexible memory drops 2 MiB once and holds; the
  driver's direct mappings are flat at the reloads (180 each).
- **Rogue Leader** from boot: no crash, no FIFO alert, no unknown opcode; the
  attract sequence's windows are as before (its heavy section read a few
  points lower on this run).
- **PPSSPP, God of War: Ghost of Sparta** from its state: full speed (480,000
  of 480,000 frames every window after the first), 1,200 presents per 10 s,
  no crash.
- **LRPS2** boots its BIOS with the new code area; the hardware renderer still
  stops at the driver's first-vertex refusal, the next driver round.
- When content closes, the platform's counters go back to zero.

Still to run on this build: Profile 10 (save-state stress), whose test harness
I have to rebuild, and a second Rogue Leader run for its heavy section.
