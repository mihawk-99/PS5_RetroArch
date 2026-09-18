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
