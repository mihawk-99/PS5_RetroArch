# Findings

Append-only measurements. What the platform, the dependency, the customer or the
data actually does — as opposed to what the documentation says — and what each
one forces the code to do. A finding is written the moment it is measured,
because it is the thing that is expensive to rediscover and impossible to guess.

Never rewrite an entry. If a later measurement contradicts one, append a new
entry that names the old one and says what changed.

## 2026-09-18: Upstream RetroArch has no PS5 platform code

**Measured.** The pinned tree carries the PS4/Orbis port only:
`frontend/drivers/platform_orbis.c`, `gfx/drivers_context/orbis_ctx.c` and
`Makefile.orbis` are present, and no `platform_ps5.c`, no `Makefile.ps5` and no
PS5 context driver exist. `grep -ril ps5` over the 1.22.2 tree returns
`CHANGES.md` and binary art, nothing in the sources. Measured by directory listing
and grep over `vendor/retroarch` (`docs/REFERENCE.md`, "The environment").

**Consequence.** The platform work is ours: a context driver, a platform driver,
an input driver and an audio driver, each behind our own seam in `platform/`,
plus whatever `configure` flags the frontend needs. It also means the port is a
patch series on a pinned tarball, never a fork we maintain file by file —
`docs/PLAN.md`, invariant "Upstream stays upstream".

**Boundary.** True for 1.22.2 and every earlier tag. If upstream ships a PS5
platform driver, the patches that only add one are dropped rather than ported,
and this entry is superseded by a new one.

---

## 2026-09-18: The payload SDK ships no Vulkan and no SDL2, but it does ship the console APIs

**Measured.** `$PS5_PAYLOAD_SDK/target/include` holds 322 entries including
`EGL/` and `GLES2/` and no `vulkan/` and no `SDL2/`; `target/lib` holds
`libScePad.so`, `libSceAudioOut.so`, `libSceVideoOut.so`, `libSceUserService.so`,
`libSceSysmodule.so`, `libSceNet.so`, `libSceHttp.so` and `libSceSsl.so` among
others. Measured by listing the sysroot of the SDK unpacked 2026-09-17
(`docs/REFERENCE.md`, "The environment").

**Consequence.** Controller input, audio output, display, user selection and
networking come from public console APIs — no third-party port is needed for
them. GPU access and windowing do not: the EGL/GLES2 headers are the SDK's own,
and a full OpenGL stack or a Vulkan driver comes from a sibling project.
Padding in a build script cannot fix this; the missing pieces have to be
linked from `../ps5-opengl-sdk-0.2.0` or `../PS5_Vulkan` (`docs/REFERENCE.md`,
"The graphics backend").

**Boundary.** The SDK's own sysroot, as unpacked on 2026-09-17. A later SDK
release could add either, in which case this entry is superseded rather than
edited.

---

## 2026-09-18: Cross-compiling with the toolchain works and is cheap to check

**Measured.** `source $PS5_PAYLOAD_SDK/toolchain/prospero.sh` followed by
`$CC -o t.elf t.c` on a hello-world C file produced an ELF 64-bit LSB
pie executable, x86-64, version 1 (FreeBSD), 110,712 bytes, `prospero-clang`
version 22.1.8 with target `x86_64-sie-ps5`. Measured in this session from a
temporary directory; the SDK also ships `prospero-nm`, `prospero-objcopy`,
`prospero-strip`, `prospero-cmake`, `prospero-meson` and
`prospero-pkg-config`.

**Consequence.** The `build` gate can prove the toolchain and the platform code
without a console, and every PS5 compile belongs in that gate rather than in a
spontaneous command. "It is FreeBSD-flavoured x86-64 ELF" is also why
`check-ps5-object.sh` cannot use the ABI to tell our output from a host Linux
object: the import table is the only reliable signal
(`docs/TROUBLESHOOTING.md`).

**Boundary.** This host, this SDK unpack, clang 22.1.8. A toolchain bump is a
pin change and gets its own line in `docs/PHASE_LOG.md`.

---

## 2026-09-18: The OpenGL backend is relocatable; the Vulkan backend is not yet consumable

**Measured.** `../ps5-opengl-sdk-0.2.0` describes a relocatable package under
`build/sdk/ps5-opengl-core33` with `share/ps5-opengl-core33/ps5-opengl-core33.mk`,
`lib/pkgconfig/ps5-opengl-core33.pc` and a CMake config; its published
`libps5_opengl_core33.pc` links `-lPS5OpenGLCore33 -lSceAgc -lSceAgcDriver
-lSceVideoOut -lkernel_web -lSceSystemService` and records 344 Core exports
(`docs/consumer-build.md`, `docs/validation.md`). `../PS5_Vulkan` holds driver
sources and static archives (`build/driver/ps5/libps5vk.ps5.a`, 14.8 MB,
2026-09-18) and vendored Vulkan headers at
`third_party/Vulkan-Headers/include`, but no installed consumer package.
Measured by reading those two checkouts; neither was modified.

**Consequence.** M2 starts by linking against the OpenGL package, because it is
the only backend with a consumer contract and a recorded hardware acceptance
run on this machine. RetroArch resolves its GL entry points through `glsym`, so
the open question is not the header but the export list: M2.1 compares the
driver's request list with the backend's export list and records the difference
before any rendering step is planned (`docs/REFERENCE.md`, "The graphics
backend").

**Boundary.** The 0.2.0 package and the PS5_Vulkan checkout as they stand on
2026-09-18. When PS5_Vulkan reaches rung 1.0 and publishes a consumer package,
this entry is superseded by the measurement that switches the default backend.

---

## 2026-09-18: The menu cannot be drawn without a GPU context, so "menu first" is a graphics milestone

**Measured.** RetroArch 1.22.2 has no software menu path. `gfx/gfx_display.c`
registers display-context drivers for Direct3D, OpenGL, OpenGL1, OpenGL3,
Vulkan, Metal, vita2d, ctr, wiiu, rsx and gdi only, and
`gfx_display_init_first_driver()` selects one by matching `dispctx->ident`
against the video driver's identity, skipping entries whose type is
`GFX_VIDEO_DRIVER_GENERIC`. No entry is generic, and the generic video drivers —
`sdl2_gfx.c` among them — register no display context at all. All four menu
drivers (`rgui`, `xmb`, `ozone`, `materialui`) draw through that context.
Measured by reading `RetroArch-1.22.2` (`gfx/gfx_display.c:50`,
`gfx/gfx_display.c:1215`, `menu/menu_driver.c:333`); XMB itself contains no
GLSL, so it is the display context and not XMB that needs the GPU.

**Consequence.** A PS5 video driver presenting a CPU-drawn framebuffer is not a
shortcut to a visible menu: it produces no display context and therefore no
menu. The first visible menu requires one of the registered backends — for this
console, the Vulkan path (`gfx/drivers_context/khr_display_ctx.c`) or the
OpenGL3 path — and the platform work is the same driver plus context pair either
way. `docs/REFERENCE.md`, M2.2, now says so instead of treating presentation as
a later step.

**Boundary.** RetroArch 1.22.2. A future release that adds a software display
context changes this, and the entry is then superseded rather than edited.

---

## 2026-09-18: RetroArch's khr_display context matches what PS5_Vulkan exposes

**Measured.** RetroArch's `gfx/drivers_context/khr_display_ctx.c` creates its
surface through `vulkan_surface_create(..., VULKAN_WSI_DISPLAY, ...)`, which
`gfx/common/vulkan_common.c` serves by requiring `VK_KHR_display` and calling
`vkCreateDisplayPlaneSurfaceKHR`. `../PS5_Vulkan`'s instance extension table
declares `.KHR_surface` and `.KHR_display`, its device extension table declares
`.KHR_swapchain`, its instance `apiVersion` is `VK_API_VERSION_1_3` and its
device `apiVersion` is `VK_API_VERSION_1_0` with `driverVersion` 0.2.0
(`driver/ps5vk_instance.c:25`, `driver/ps5vk_physical_device.c:56`,
`driver/ps5vk_private.h:51`), and `driver/ps5vk_wsi.c` presents to VideoOut.
Measured by reading both checkouts.

**Consequence.** The two halves fit: RetroArch's display-based Vulkan context
driver is the right seam for this console, and the driver's own VideoOut
presentation is what fills it. A barebones frontend therefore does not need a
new windowing layer — it needs the driver's consumer package and its remaining
Vulkan surface. Whether RetroArch's Vulkan renderer asks for more than the
driver implements is the open question, and it is answered by loading the menu,
not by reading headers.

**Boundary.** The PS5_Vulkan checkout at `5fd2626` (2026-09-18) and RetroArch
1.22.2. Rung 1.0 is the point at which this becomes a support statement rather
than a fit between two interfaces.

---

## 2026-09-18: The existing PS5 RetroArch payload is a build recipe, and its first prerequisite is missing here

**Measured.** `homebrew/RetroArch/` in `ps5-payload-dev/websrv` at commit
`1afd476` is twelve files and 48 KB: `build.sh`, seven per-core recipes
(`build-fbneo.sh`, `build-fceumm.sh`, `build-genesis_plus_gx.sh`,
`build-mednafen_gba.sh`, `build-puae2021.sh`, `build-snes9x2010.sh`,
`build-vice.sh`), `fetch-assets.sh`, `fetch-databases.sh`, `homebrew.js` and a
`.gitignore`. `build.sh` downloads the upstream RetroArch **1.21.0** tarball,
rewrites `SDL_RENDERER_ACCELERATED` to `SDL_RENDERER_SOFTWARE` in
`gfx/drivers/sdl2_gfx.c`, drops `md5.o` from `Makefile.common`, configures with
`OS=BSD`, `CROSS_COMPILE=$PS5_PAYLOAD_SDK/bin/prospero-` and `LDFLAGS=-rdynamic`
plus `--enable-sdl2 --enable-mmap --enable-dylib` and every GL and Vulkan switch
disabled, then stages `retroarch.elf`, `retroarch.cfg`, `sce_sys/icon0.png` and
three appended config keys. `fetch-assets.sh` pins retroarch-assets 1.20.0 and
`fetch-databases.sh` pins libretro-database 1.21.1.

The recipe's first requirement does not hold on this host: it enables SDL2, and
SDL2 is not visible to the build. `$PS5_SYSROOT/user/homebrew/` contains only an
empty `include/`, there is no SDL2 header anywhere under the sysroot, and
`prospero-pkg-config --exists sdl2` exits 1. Measured by sparse-cloning that path
(`git clone --filter=blob:none --no-checkout`, `sparse-checkout set
homebrew/RetroArch`), reading the files, and probing the local SDK.

**Consequence.** The recipe is the right shape to build on — fetch a pinned
tarball, patch it, configure, build, stage — and it already solves two pieces of
the PPSA packaging problem (the `icon0.png` from the upstream tree, and the
config seed). It is not runnable as it stands, and its graphics switches are the
opposite of what this project wants. Adopting it therefore means: supply the
ports the recipe needs or drop the SDL2 switch for ours, replace the graphics
flags with the Vulkan ones, and take the staging and packaging from here.
`reference/ps5-retroarch/PROVENANCE.txt` records the copy and its file digests;
the recipe is kept read-only so a later change to it is a visible step.

**Boundary.** websrv commit `1afd476` (2026-08-10) and the local SDK unpack of
2026-09-17. If a ports image is installed into the sysroot, the SDL2 half of this
finding stops being true and is superseded rather than edited.

---

## 2026-09-18: Building for this console on this host needs four host-side facts

**Measured.** Four things had to be true before the vendored recipe would build
here, each established by a failing build and then a passing one:

1. **The toolchain resolves an absolute include against the host filesystem, not
   its sysroot.** `-I/user/homebrew/include/SDL2` fails with `-isysroot $SDK/target`
   and fails with `--sysroot=$SDK/target`; it succeeds only when the path exists
   on the host. Verified by compiling a one-line file that includes `SDL.h` three
   ways. `prospero-clang` adds no sysroot rewriting of its own for `-I`.
2. **The ports prefix must be reached by a host path.** A pkg-config wrapper that
   exports `PKG_CONFIG_LIBDIR=<ports>/libdata/pkgconfig` with
   `PKG_CONFIG_SYSROOT_DIR=<ports>` produces a doubled path, because the `.pc`
   file already records `prefix=/user/homebrew`. `PKG_CONFIG_SYSROOT_DIR` must be
   empty when the `.pc` is read straight from the ports tree.
3. **The SDK's pkg-config and the SDK's compiler look in different places, and
   both must be offered the ports.** `prospero-pkg-config` searches only
   `$PS5_SYSROOT/user/homebrew/{lib,libdata}/pkgconfig`; the compiler needs the
   host path. The build satisfies both.
4. **`/user` cannot be created here without root**, and an unprivileged mount
   namespace cannot create it either: `unshare -Urm` gives a private namespace but
   `mkdir /user` still fails with EACCES because the root mount is not writable,
   and `bwrap --tmpfs /user` fails the same way. Overlay-mounting `/` is refused.
   So the console's own prefix cannot be spelled on the host, and the build must
   be told the host spelling instead.

**Consequence.** `tools/build-baseline.sh` carries all four as measured facts
rather than as guesses: a pkg-config of its own for the ports, a rewrite of the
generated `config.mk` include paths, and `-j` with ccache. None of it changes what
the payload links against at runtime — those paths stay the console's own.

**Boundary.** This host (CachyOS, no `/user`, unprivileged), this SDK unpack, and
the v0.40.2 ports prefix. A host that happens to have `/user/homebrew` populated
would not need points 1, 2 or 4.

---

## 2026-09-18: The recipe's build is 34 s from cache, against about five minutes cold

**Measured.** With the pinned tarball cached in `.deps/cache/`, the prepared
upstream tree kept in `work/baseline/`, every compile routed through ccache 4.14
and `MAKEFLAGS=-j14` on this 14-core host, a full build takes **33–35 s**, and an
incremental build after editing a frontend source file takes **35 s**. The first
cold build, which downloaded the tarball, extracted it and compiled with neither
cache, took about five minutes. Three fixes were needed to get there: the recipe
moves the upstream icon out of the source tree, which broke every rebuild until
the extraction guard learned to re-extract when the icon is missing; the log
directory was created before the recipe's files were copied over it; and the
recipe's own log is enormous, because it passes `V=1` to make, so the build
captures it to `work/baseline/logs/build.log` instead of the terminal.

**Consequence.** The console run is now the slow part of the loop, not the build,
which is what makes the "one step per commit, verified on the target" workflow
practical. `docs/ACTIVE.md` records the numbers so a later regression is visible.

**Boundary.** Measured on this host with a warm ccache. A `--clean` build clears
the ccache and returns to the cold path.

---

## 2026-09-18: This console's FTP service has three behaviours a deploy must respect

**Measured.** Against ftpsrv v0.21.1 on the console:

- it answers a successful `DELE` with **226** rather than 250, which `ftplib`
  raises `error_reply` on, so a naive delete looks like a failure after it has
  happened;
- it ignores the path argument of a listing command: `MLSD /some/dir` returns the
  **root** listing, and only an explicit `MLSD .` after a successful `CWD` lists
  the intended directory. That silently defeated a verification check and reported
  a correctly uploaded file as missing;
- a nested relative `CWD` can fail with 550 while the same directory opens fine by
  absolute path.

**Consequence.** `tools/deploy.py` navigates by absolute path, verifies every
uploaded file's size on the console before publishing it under its real name, and
treats the 226 delete as success. It also refuses to remove anything but this
project's own remote directory.

**Boundary.** ftpsrv v0.21.1 on this console. Another service, or a later version,
may answer differently, and the size check is what would catch it.

---

## 2026-09-18: The application-image converter has three requirements, and two are now met

**Measured.** Getting from a working payload to an `eboot.bin` runs through the
image converter in `../ps5-native-app-boilerplate-main`
(`tooling/native/native_app_builder.cpp`, built as `build/host/ps5-native-tool`
— its host half builds without the `clang-18` its full app build wants). Three
requirements were found by running it, in this order:

1. **The linked layout must leave room for the process parameters.** The
   converter computes where the console's process-parameter record and its
   parameter blocks go, and refuses the layout when that space would overlap the
   writable data: `error: LLVM layout leaves no room for PS5 process parameters`
   (`sce_module_writer.cpp:684`, which needs `relro_end <= data_start`). The
   recipe links with the compiler driver and its default PIE layout, which fails
   this. Linking instead through `prospero-lld` with the boilerplate's
   `ps5-pie.ld`, plus one page-alignment between the RELRO and data segments,
   produces a layout the converter accepts — verified twice: on a minimal
   program, which converted to 121,536 bytes, and on the real 68 MB RetroArch
   payload, which then failed at the next requirement instead of this one.
   `linker/ps5-pie.ld` is that script and `tools/prospero-clang-link` is the
   shim that applies it.
2. **Every referenced symbol must be defined somewhere.** The converter refuses
   to write an image while a referenced symbol has no definition:
   `error: no public SDK stub exports required symbol __dlopen`, then
   `... kernel_mprotect`. Compiling for this target makes clang take FreeBSD's
   libc as its model, so every dlopen() user carries a **weak** reference to
   `__dlopen` and its siblings — harmless to the linker, fatal to the converter
   — and libretro-common's memory-mapping layer references `kernel_mprotect`.
   Enumerating the difference between the payload's undefined symbols and the
   definitions in every SDK stub left exactly one such symbol after `__dl*`:
   `kernel_mprotect`. `platform/ps5_dl_stubs.c` defines all six, and forwards
   `kernel_mprotect` to `mprotect` rather than stubbing it, because a failing
   stub is precisely what denies a dynamic recompiler executable memory.
3. **The converter does not yet publish application exports.** It requires every
   dynamic symbol to be undefined: `error: native converter does not yet publish
   application exports` (`sce_module_writer.cpp:698`). This is the current
   blocker. Our payload defines exports — that is what `-rdynamic` is for — and
   the converter's own tool is the thing that would have to change, or the
   payload would have to be linked with an export list that hides them. This one
   is a limitation of the tool, not of the payload.

**Consequence.** Two of the three are solved in this repository and the third is
named with its exact text. The remaining work is a decision about which side to
change: relax the converter, or publish a narrowed export list from the link.
Either way it is a small, well-defined change rather than an open question.

**Boundary.** The converter as it stands in `../ps5-native-app-boilerplate-main`
on 2026-09-18, and RetroArch 1.21.0 as the recipe links it. A converter that
gains export publishing removes the third requirement.

---

## 2026-09-18: The application image needs the SDK startup object, and three symbols no stub can supply

**Measured.** With the layout accepted and the weak `__dl*` references satisfied,
the converter produced `eboot.bin` — and then revealed that the earlier
"missing symbol" errors had a single root cause and a different set of
consequences than they appeared to have.

The root cause: going straight to the linker skips what the compiler driver adds
by itself. `prospero-clang -###` shows it passing `-l:crt1.o`, `-l:crti.o`,
`-l:crtbegin.o`, `-l:crtend.o`, `-l:crtn.o`. Without `crt1.o` there is no
`_start`, so the image's entry point stayed 0 and the console would have had
nothing to call — and `crt1.o` is also what defines `kernel_mprotect` and the
`__dl*` family, which is why the converter complained about them. Naming those
objects in the linker invocation resolved both: `_start` is now at 0x10 and the
entry point points at it. The `platform/ps5_dl_stubs.c` written before this was
found was redundant and has been deleted; the record of why is kept here.

Three symbols remain, and they are of a different kind: `__bss_start`,
`__bss_end`, `__image_start` and `__image_end` describe the image's own layout, so
no stub library can export them and the compiler driver never adds them either.
They are defined in `platform/ps5_image_symbols.S` and linked into the image,
which is where they belong. With them the conversion completes:

```text
wrote 51870448 bytes: build/eboot.elf
wrote 49968821 bytes: build/eboot.bin
container: signed, plaintext; segments: 12
authority: 0x3100000000000002; program type 0x1; integrity: valid
```

**Consequence.** The PPSA folder is complete: `dist/PPSA99005/` holds the signed
application image, the loader module, the title's identity and icon, the
configuration seed, the payload beside them and a digest manifest. Two further
things were needed and are now in the link: `--exclude-libs=ALL`, without which
the linker exports about 185 symbols pulled out of static libraries and the
converter refuses the image for publishing exports.

**Boundary.** The converter and SDK as they stand on 2026-09-18. The layout
requirement, the startup objects and the layout symbols are all properties of
this image format, not of RetroArch, so any large application built this way
needs the same three.

---

## 2026-09-18: The launcher icon comes from the project's own artwork

**Measured.** The title's icon is generated, not copied: `title/assets/retroarch.png`
(640x640) resampled to 512x512 by `tools/stage-ppsa.sh` and again by
`tools/build-baseline.sh`, so both the PPSA folder and the payload folder carry
the same image. The 512x512 requirement is the console's, taken from the
boilerplate's asset validator and confirmed against the sibling project's own
title folder, where `icon0.png` is also 512x512.

**Consequence.** One source image in the repository, two generated copies, and no
hand-edited icon in either output. Before this the icon was the one the vendored
recipe lifts out of the upstream RetroArch tree, which was a dependency of that
recipe rather than a choice; the generated icon also removes the recipe's habit
of *moving* that file out of its tree, which had broken incremental rebuilds
until the extraction guard learned to repair it.

**Boundary.** The artwork is the project's own file. A different source image
needs only to be square; the scripts stretch to 512x512 rather than padding, so a
non-square source would be distorted and should be rejected by whoever replaces it.

---

## 2026-09-18: The installed title crashed because its eboot.bin was not a converted image

**Measured.** With the title installed on the console, `launch PPSA99005` through
the resident control payload produced `EndAppMount(0x00000018)` and no running
process, and the kernel log captured on port 3232 recorded a fatal signal inside
the application:

```text
[SceLncService] EndAppMount(0x00000018)
[kstuff.elf] Title Mounted Successfully: /data/homebrew/PPSA99005 -> /system_ex/app/PPSA99005
# A user thread receives a fatal signal
# signal: 11 (SIGSEGV)
# thread name: eboot.bin
# reason: page fault (user read instruction, page not present)
# fault address: 0000000000000001
/app0/sce_module/libc.prx
[Syscore App] App Crash : reason=0xb
```

The installed `eboot.bin` was 51,870,448 bytes — the size of a **link-stage ELF**,
and larger than the converted and signed image this repository now produces
(49,968,821 bytes). The loader therefore started a file that is not in the
application-image format `eboot.bin` has to be, and faulted before `main()` ran.
The thread name and the process name both read `eboot.bin` because that is what
the loader had just started.

**Consequence.** `tools/install-title.sh` exists to prevent exactly this: it
installs only `dist/<TITLE_ID>/eboot.bin`, verifies the stored bytes against the
local image by size and SHA-256, and keeps the previous image as
`eboot.bin.previous` so the change is reversible. The crash is recorded as
evidence in `evidence/ppsa-99005-startup-crash/` so the repaired run has
something to be compared against.

**Boundary.** This is a property of the console's loader, not of RetroArch: any
payload placed at `eboot.bin` without being converted will fault the same way.
The three converter requirements in the previous finding are what stands between
a link output and a runnable title.

---

## 2026-09-18: This console's FTP service can lose a directory during a replace

**Measured.** Replacing `eboot.bin` in `/data/homebrew/PPSA99005/` was performed
as an upload to a temporary name followed by delete-and-rename, and reported
success at every step (`226 File deleted`, `226 Path renamed`). Immediately
afterwards the whole directory was gone: `LIST /data/homebrew/PPSA99005/` is
empty, the folder no longer appears in `LIST /data/homebrew/`, and a later `CWD`
into it fails with `550 No such file or directory`. The neighbouring homebrew
folders (`PS5_RetroArch`, `RetroArch`, `PPSA99988`) were unaffected, and the
control payload stayed healthy throughout.

**Consequence.** The folder has to be treated as reconstructable rather than
durable. Everything it held is reproducible: `dist/PPSA99005/` holds the
converted image, the configuration, the payload, the module, the identity and the
icon with a digest per file, and the same payload also still exists on the
console under `/data/homebrew/PS5_RetroArch/`. Any install path must therefore
recreate the folder rather than patch a file inside it, and must verify what it
wrote.

**Boundary.** ftpsrv v0.21.1 on this console, which also answers deletes with 226
and ignores the path argument of a listing command. Rewriting a file in place is
not a safe operation on this service; writing a whole directory and checking it
is.

---

## 2026-09-18: The title's eboot.bin and libc.prx could not be replaced over FTP

**Measured.** Publishing the converted folder through the deployment path that
works on this console — the helpers from `../PS5_Vulkan/tools/deploy.sh`, which
handle this server's 226-on-delete and root-relative paths — the image and the
loader module never changed:

```text
==> [deploy] 7 files to /data/homebrew/PPSA99005/
sce_module/libc.prx: the console did not store the whole file
```

Five consecutive attempts, each an upload to a temporary name followed by a
delete and a rename, all reported success with `226 File deleted` and
`226 Path renamed`, and every read-back returned the previous file:
`libc.prx` 1,335,962 bytes where the local file is 1,284,674, and `eboot.bin`
51,870,448 bytes where the local file is 49,968,821 — the latter still starting
`7f454c46` (ELF) rather than `4f153d1d` (the converted image).

The server itself is not the limit, which was established separately in the same
folder: uploading freshly generated blobs and hashing them back round-trips
exactly at 2 MB, 8 MB and **60 MB**. A neutral file name made no difference —
`neutral.img` also read back as the 51,870,448-byte image. Files whose names the
folder does not already contain do persist (a 40 KB marker written earlier was
still listed and read back correctly).

**Consequence.** The remaining difference between this folder and a working
title is exactly two files, and they cannot be replaced from here while they keep
reverting. `tools/deploy-title.py` and `tools/ps5_ftp.py` do the job correctly —
they verify every stored size and refuse to call it done when one does not match,
which is why this was detected rather than believed — so the tooling is ready for
whoever can write those two paths. The likely cause is outside this repository:
the other session working on this console recreates the title's image and module,
and its writes win.

**Boundary.** This console, ftpsrv v0.21.1, while a second session is publishing
the same title. A console with one writer does not show this.

---

## 2026-09-18: A title's eboot.bin must be a converted image, and here is how to tell in one byte

**Measured.** The sibling project that owns this console's working title converts
its image before publishing, and the result is recognisable at a glance:

```text
PS5_Vulkan/dist/PPSA99988/eboot.bin   16,979,653 bytes   starts 4f 15 3d 1d
PS5_RetroArch/dist/PPSA99005/eboot.bin 49,968,821 bytes  starts 4f 15 3d 1d
console's /data/homebrew/PPSA99005/eboot.bin
                                       51,870,448 bytes  starts 7f 45 4c 46
```

`4f153d1d` is the development-container magic; `7f454c46` is a plain ELF. The
console's copy is the intermediate link output, not the converted application
image — and that is the whole of the crash documented earlier: the loader starts
it, the first call goes through a pointer that was never populated, and the
process dies at `rip=0x1` before `main()`. The same project's build script shows
the two steps that produce the right file:
`"$tool" link --in llvm-pie.elf --out eboot.elf` then
`"$tool" self --sign --in eboot.elf --out eboot.bin`.

**Consequence.** Any image placed at a title's `eboot.bin` must have gone through
both steps, and `tools/deploy-title.py --check` now reports the magic bytes so a
wrong image is caught before a launch rather than inferred from a crash. This also
means the difference between the working title and this one is exactly one
pipeline step, not a design problem.

**Boundary.** This console's loader. A payload started by a launcher instead of
the loader does not need the container, which is why the same frontend runs as
`retroarch.elf` and crashes as `eboot.bin`.

---

## 2026-09-18: The title folder was writable, and the two loader files were still reverted

**Measured.** `/data/homebrew/PPSA99005` and `/system_ex/app/PPSA99005` are the
same directory, it accepts writes, and a file written into it stays: a 40 KB
marker, a 2 MB, an 8 MB and a 60 MB blob all round-tripped byte-for-byte, and the
512x512 `sce_sys/icon0.png` published by our deploy matched exactly. The same
deploy nevertheless could not change `eboot.bin` or `sce_module/libc.prx`:

- five attempts as upload-to-temporary, delete, rename: every read-back was the
  previous file;
- writing `eboot.bin` in place with a plain `STOR`, no delete and no rename: the
  same;
- the same converted bytes under an unused name (`zz-image.bin`): the same;
- and the icon, written by the same helpers in the same run, took.

So the refusal is not about the directory, the name, the transfer size or the
method. Something outside this repository restores those two files — most
plausibly the second session working on this console, publishing the title's own
image and module.

**Consequence.** The deployment path is ready and verified; what it cannot do is
win a race against another writer. The check to run when that writer stops is
`tools/deploy-title.py --check`, and the line to look for is
`eboot.bin magic: 4f153d1d`.

**Boundary.** This console, while a second session publishes PPSA99005. A console
with one writer does not show this.
