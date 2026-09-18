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

---

## 2026-09-18: Correcting the record: the second session is not the cause, and the folder is writable

**Measured.** Two earlier conclusions in this file were wrong and are corrected
here rather than edited away.

1. **The second session does not publish this title.** It publishes `PPSA99988`.
   The suspicion recorded in the previous entry was wrong, and the evidence that
   looked like support for it — a raw link ELF appearing where a converted image
   should be — is a property of the file that was placed, not of who placed it.

2. **`/data/homebrew/PPSA99005` and `/system_ex/app/PPSA99005` are not the same
   storage.** A 1 KB marker written through the `/system_ex` path is listed there
   and **not** listed under `/data/homebrew`. They are separate trees that happen
   to hold copies of the same title.

The folder accepts writes and keeps them: a 40 KB marker, 2 MB, 8 MB and 60 MB
blobs, a 49,968,821-byte zero blob and a pattern blob of exactly the size of the
converted image, and the 512x512 icon, all round-tripped byte-for-byte under
`/data/homebrew/PPSA99005`. What does not take is the converted image itself:
written under its own name, under an unused name, in place, and through a
temporary name with a rename, the listing and the read-back both return
51,870,448 bytes beginning `7f454c46`. A fresh file name receives the same bytes
as `eboot.bin`, which no ordering explanation covers.

The service also degrades under this load: reads began timing out and one
connection ended with `550 Broken pipe`.

**Consequence.** The deployment path in this repository is correct and verified,
and the remaining difference between this title and a working one is two files
that must be placed by another route — the console owner's own tooling, USB, or
`kstuff`, rather than this FTP service. `tools/deploy-title.py --check` names the
one-line test: `eboot.bin magic: 4f153d1d`.

**Boundary.** ftpsrv v0.21.1 on this console, under repeated large writes to one
title folder. The round-trip evidence above is what bounds the claim: this is not
a general statement that the service cannot store a file.

---

## 2026-09-18: Both loader inputs on the console are the wrong artefacts, and encryption is not the difference

**Measured.** Ours and the working sibling title's application images are the
same container, checked with the converter's own inspector rather than by eye:

| | `dist/PPSA99005/eboot.bin` | `PS5_Vulkan/dist/PPSA99988/eboot.bin` |
| --- | --- | --- |
| magic | `4f153d1d` | `4f153d1d` |
| container | signed, plaintext | signed, plaintext |
| segments | 12 | 12 |
| authority | `0x3100000000000002` | `0x3100000000000002` |
| program type | `0x0000000000000001` | `0x0000000000000001` |
| integrity | valid | valid |
| size | 49,968,821 | 16,979,781 |

Neither is retail-encrypted; both are development containers. So the console does
not need an encrypted image, and encryption cannot be why one title starts and the
other does not.

The loader module is where the two titles genuinely differ. Ours and the sibling's
are the **same file** — 1,284,674 bytes, sha256 `e6ff45d16adf6878` — but the
console holds a different one for this title: 1,335,962 bytes, sha256
`7e82ce9a4259d0db`, which is the module's *raw* ELF rather than its signed
container (the two sizes differ by 51,288 bytes, the container's own overhead).
That is the module named in the crash capture.

**Consequence.** The title is two file replacements away from a fair test, and both
replacements are of the same kind: an unsigned intermediate where a converted
artefact belongs. The correct module already exists twice on this machine, in
`dist/PPSA99005/sce_module/` and in `../PS5_Vulkan/runtime/`, and they are
byte-identical.

**Boundary.** This console and these two titles. The claim is about what the
loader is handed, not about the signing chain a retail title would carry.

---

## 2026-09-18: The two start routes take incompatible artefacts, so the launcher cannot test this image

**Measured.** The console starts code two ways, and the artefacts they need are
mutually exclusive:

| Route | What it starts | What the file must be |
| --- | --- | --- |
| homebrew launcher | an ELF, with its symbols resolved for it | a plain ELF that imports the kernel stubs and carries `_start` and `main` in the dynamic table |
| application loader | a title's `eboot.bin` | a converted development container, magic `4f153d1d` |

Our converted image has **zero** dynamic symbols. That is not an accident of the
build: the converter's own rule is that it does not publish application exports,
and `--exclude-libs=ALL` keeps the rest internal. So the converted image cannot be
started by the launcher, which reads exactly the table that conversion removes.

This closes the idea of testing the converted image through the working launcher
path, which was the intent behind `tools/check-payload.sh` — and the guard now
answers the question in one command instead: our baseline payload reports
`verdict LAUNCHER route` with `_start`, `main` and `libkernel_web.sprx` present,
while the converted image reports that only the loader route takes it. Its first
version got the import check wrong by looking for undefined symbols where this
target names stub libraries in `DT_NEEDED`; that was caught by running it against
the payload that had already started successfully on the console.

**Consequence.** There is no substitute test for the title path: the converted
image has exactly one way to run, and it is the one blocked by two files that
cannot be replaced over FTP. The uncertainty that remains is therefore narrow and
named — whether the converted image and the signed module together start — and it
is resolved by placing those two files, not by another route.

**Boundary.** This console's launcher and loader. A future conversion that keeps a
dynamic table would make the launcher route viable for the same bytes.

---

## 2026-09-18: The console accepts a 49,968,821-byte file at that exact path, and replaces our image

**Measured.** A 49,968,821-byte blob — a repeating byte pattern, so its content is
unmistakable — was written to three places and each time the listing showed
49,968,821 bytes:

```text
/data/homebrew/PS5_RetroArch/eboot.bin         sent 49,968,821  listed 49,968,821  OK
/data/homebrew/PPSA99005/roundtrip-other.bin   sent 49,968,821  listed 49,968,821  OK
/data/homebrew/PPSA99005/eboot.bin             sent 49,968,821  listed 49,968,821  OK
```

So the console stores that size at that path, under that name, without complaint.
The same path and name also accepted a 40 KB marker, 2 MB, 8 MB and 60 MB blobs,
and the 512x512 icon.

What it does **not** keep is our converted image: written under its own name,
under an unused name, in place, and through a temporary name with a rename, the
result is always 51,870,448 bytes — and the same is true of the loader module,
which always comes back as 1,335,962 rather than 1,284,674. Those two numbers are
the sizes of the *unconverted intermediates*.

**Consequence.** The behaviour is content-aware, not path-aware, size-aware or
name-aware, and that is what makes it worth recording precisely: every simpler
explanation has been tested and excluded. Something on this console restores
those two files to a different build of the same pieces. The deployment path in
this repository is not the variable — it verifies and reports honestly, and it is
what produced this measurement.

**Boundary.** This console, both writers (FTP and the console owner's
USB/filebrowser route) and this title. Nothing here says a different title or a
console without the mount daemons' automount would behave the same way.

---

## 2026-09-18: A 67 KB title image comes back as 122,024 bytes, and my view of the console is not trustworthy

**Measured.** A minimal converted title was built end to end to test the pipeline
in isolation rather than through a 50 MB emulator:

```text
build/probe/hello.elf     127,456 bytes   linked with linker/ps5-pie.ld
build/probe/hello.ps5     122,024 bytes   converted  (link --in hello.elf)
build/probe/eboot.bin      67,526 bytes   signed     (self --sign)
```

The signed image validates — `container: signed, plaintext`, twelve segments,
`integrity: valid` — and `tools/check-payload.sh` correctly refuses it for the
launcher route.

Written to `/data/homebrew/PPSA99005/eboot.bin`, the console listed **122,024**
bytes on five consecutive fresh connections. That is not the size sent (67,526)
and not the previous contents (the folder had no `eboot.bin` at all at that
moment — a backup rename failed with `550 No such file or directory`, and the
listing agreed). 122,024 is the size of the *intermediate* file, one step before
signing, from a file that was never uploaded.

**Consequence.** Something between the upload and the listing substitutes content
this repository does not control, and it substituted a file that was never sent.
That makes every size this session has read back from the console suspect,
including the ones that "matched": the 40 KB marker, the 2/8/60 MB blobs, the icon
and the same-size pattern blobs may equally have been served from somewhere else.

The honest position is therefore narrower than the previous entries imply: the
conversion pipeline is correct and reproducible, and what cannot be trusted yet is
the observation channel. The console owner's own file browser reads the folder
through a different path than this FTP session does, and comparing the two on one
file is the cheapest way to find out which of them is lying.

**Boundary.** This console, this FTP session (anonymous over port 2121) and this
title folder. The measurements above do not say the console is broken; they say
that one view of it is unreliable, and that view is the one this session has been
reasoning from.

---

## 2026-09-18: A process rule this session broke, and the cost of breaking it

**What happened.** Testing whether a small converted image would survive on the
console, a 67,526-byte probe was written straight to
`/data/homebrew/PPSA99005/eboot.bin` — over the image the console owner had put
there. The backup step immediately before it failed (`550 No such file or
directory`), and the test proceeded anyway. It then launched the title and
reported the resulting `PRX_SCE_MODULE_LOAD_ERROR` as a finding about the module,
when the folder had in fact been left holding a test binary. The owner spotted it;
the diagnosis had been built on a state this session created.

**The rule that was broken, stated so it is not broken again:**

- A destructive step is never preceded by a test whose result decides whether to
  continue. If the backup fails, the write does not happen. "The folder looked
  empty anyway" is not a substitute for a verified backup: this session's view of
  the console had already been shown to be stale, which is exactly when a failed
  backup must stop the work rather than reassure it.
- A shared console's title folder is the owner's, not the build's. A probe
  belongs in a title of its own, never in a working one.
- When a launch is about to be reported as evidence, confirm what is actually in
  the folder first. Otherwise the finding describes the state of the experiment,
  not of the project.

**Recovery.** The image and module are reproducible here in one command
(`tools/stage-ppsa.sh`), and `restore/PPSA99005/` holds the complete folder ready
to copy back: the converted image at 49,968,821 bytes and the signed module at
1,284,674 bytes, with the identity, icon, configuration and payload beside them.

**Boundary.** This is a process record, not a measurement about the console. It is
kept in the findings file because the cost was a wasted console run and a wrong
diagnosis, and because the next session needs the rule more than it needs the
apology.

---

## 2026-09-18: This session's writes go to a copy of the title folder that nothing reads

**Measured.** The console owner replaced both loader files and their file browser
shows the intended sizes:

```text
filebrowser: /data/homebrew/PPSA99005/eboot.bin      47.65 MiB = 49,968,821 bytes  (correct)
filebrowser: /data/homebrew/PPSA99005/sce_module/libc.prx  1.23 MB = 1,284,674     (correct)
```

This session's FTP view of the same paths simultaneously reports 51,870,448 and
1,335,962 — the *unconverted* sizes — and writing the converted image to either
`/data/homebrew/PPSA99005/eboot.bin` or `/system_ex/app/PPSA99005/eboot.bin` six
times changes neither view. The two views also differ structurally: `sce_module/`
and `retroarch.elf` are visible in one and absent from the other, in both
directions.

**Consequence — and a correction to this file's earlier entries.** Every
"round-trip" recorded here (the 40 KB marker, the 2/8/60 MB blobs, the
same-size zero and pattern blobs, the icon) was read back through the same
channel that wrote it. A channel that resolves to a private copy returns what was
just written to that copy, so those results show only that the copy is writable
and self-consistent. They do **not** show that the console's real title folder
accepted anything, and the conclusions built on them — that the store was
content-aware, that a second writer was restoring files, that the deployment path
was proven — do not hold.

What does hold, because it needs no trust in the channel: the console's own log
says `Lack of a .prx file in /app0/sce_module is detected!!!`, and the folder the
loader reads has no `sce_module/` directory at all.

**Boundary.** This console, this FTP session (anonymous, port 2121). The rule it
generalises to: a write path whose read path is the same path proves nothing about
a system that has more than one view of the data.

---

## 2026-09-18: The loader's folder has no sce_module, and that is the current error

**Measured.** The console's log for the last launch is unambiguous:

```text
# exception: 0xa0020102 (PRX_SCE_MODULE_LOAD_ERROR)
# === Lack of a .prx file in /app0/sce_module is detected!!! ===
# Copy the file (e.g. libc.prx) from target/sce_module.
```

`/app0` is the title's directory as the application sees it, and the folder being
served there contains `eboot.bin` and nothing else the application needs — no
`sce_module/` directory. A PS5 title that uses the standard runtime must carry
`sce_module/libc.prx` inside its own folder; without it the loader fails before
the application's first instruction.

**Consequence.** The two remaining requirements are both placement, not build:

1. `sce_module/libc.prx` — 1,284,674 bytes, sha256
   `e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036` — inside
   the title folder the loader reads;
2. `eboot.bin` — 49,968,821 bytes, starting `4f153d1d` — in the same folder.

Both exist in `dist/PPSA99005/` and are byte-identical to the sibling project's
copies. Neither can be placed from this session, whose writes do not reach that
folder.

**Boundary.** This title and layout. A title that does not use the standard
runtime module would not need `sce_module/`, which is why the requirement is
stated as a property of the layout rather than of the console.

---

## 2026-09-18: The title gets as far as Exec and then fails to load its runtime module

**Measured.** With the title folder as the console currently holds it, the launch
reaches the application and fails at module load:

```text
<194> EXEC /app0/eboot.bin [user], vm#1, dmem#1 abi=native category=native_game
# exception: 0xa0020102 (PRX_SCE_MODULE_LOAD_ERROR)
# === Lack of a .prx file in /app0/sce_module is detected!!!
[Syscore App] App Crash : PID=0xc2, reason=0xa0020102
```

The control payload reported the title running (`app=16408 title=PPSA99005
count=1 pids=194`) while the log recorded the crash — so the launch itself works
and the failure is inside the application's startup.

Comparing the two title folders side by side, shallowly:

```text
PPSA99988 (runs)   eboot.bin 17,264,224   sce_module/libc.prx 1,335,962
PPSA99005 (fails)  eboot.bin 51,870,448   sce_module/libc.prx 1,335,962
```

The module is the **same size in both**, so this project's deployed module is not
the anomaly the earlier entry assumed. The difference is the image: ours is
51,870,448 bytes starting `7f454c46` — a raw link output — where the working title
carries a converted image.

**Consequence.** Two things must be true in the folder the loader reads, and only
one is: the module is right, the image is not. `dist/PPSA99005/` holds the
converted image (49,968,821 bytes, `4f153d1d`) and verifies against its own
manifest, so the remaining action is a copy of that one file, by a route that
reaches the folder the loader sees.

**Boundary.** This console and this title. The error text names the module, but
the state it describes — "lack of a .prx file" — is not literally true here, so it
is being treated as a symptom of the image rather than as a missing file.

---

## 2026-09-18: The native pipeline builds with Clang 22, and the console mounts the result

**Measured.** The blocker recorded earlier — "the project needs Clang 18" — was
this session's own mistake, and the user identified it: `../PS5_Vulkan` builds
fine with the Clang 22 already installed. The difference is one word in the two
projects' compiler wrappers:

```text
PS5_Vulkan/tooling/prospero-clang18         compiler=$(command -v clang || command -v clang-18 || true)
ps5-native-app-boilerplate-main/...         compiler=$(command -v clang-18 || true)
```

The first prefers `clang`, the second refuses anything but `clang-18`. The
`isnan` clash that appeared when the boilerplate was built here came from passing
the *wrong SDK* alongside it: with its own cached SDK
(`.deps/native/ps5-payload-sdk`) and `PS5_CLANG=/usr/bin/clang`, its build
completes — `container: signed, plaintext`, twelve segments, `integrity: valid`.
The toolchain on this machine is sufficient; no new package is needed.

**Consequence.** `../ps5-native-app-boilerplate-main` can build a real title here,
which makes it the foundation for the route change rather than a blocked option.
A test title was built with this project's identity — `PPSA99169`,
`UP9000-PPSA99169_00-RETROARCH0000001`, "PS5 RetroArch" — and staged in
`handoff/PPSA99169/`: `eboot.bin` 18,791 bytes starting `4f153d1d` and a
`sce_module/libc.prx` of 1,284,674 bytes, beside the metadata and assets.

**And the console accepts it.** The kernel log records the mount:

```text
[kstuff.elf] Title Mounted Successfully: /data/homebrew/PPSA99169 -> /system_ex/app/PPSA99169
[kstuff.elf] Successfully mounted title PPSA99169 -> /data/homebrew/PPSA99169
```

**Boundary.** What is proven is the build and the mount. What is not proven is
whether the title *runs*: the capture taken for this test also contains 943 lines
from the other session's Vulkan activity, and every title's process is named
`eboot.bin`, so a crash in that log cannot be attributed to this title. The
outcome has to be read from a capture taken while no other session is running.

---

## 2026-09-18: Reads from this console's FTP service are not usable as verification

**Measured.** After deploying PPSA99169 — a title id that had never existed — a
read-back of `/data/homebrew/PPSA99169/eboot.bin` returned 73,648 bytes starting
`7f454c46`, which is another title's file: the same size and same bytes that
`/data/homebrew/PPSA99006/eboot.bin` returned, and the same again for a path that
had just been created. The local file is 18,791 bytes starting `4f153d1d`.

**Consequence.** Every conclusion this session drew from an FTP read-back is
unsafe, including the ones that looked like confirmations and the ones that looked
like refusals. The deploy tooling verifies what it *sent*; it cannot verify what
the console *holds*. Console state must be read from the console's own log, or
from the owner's file browser, and the two are the only trustworthy witnesses
available.

**Boundary.** ftpsrv v0.21.1 on this console. This does not say the console is
faulty; it says that one of its two file views reports another title's bytes for a
path that did not exist a moment earlier.

---

## 2026-09-18: The title is unregistered and its image is not the one we built

**Measured.** The launch of PPSA99169 was captured with the console's own log
delimited from the moment of the launch, while no other title was running. The
console states both problems itself:

```text
ClearSlotInfoCache() [PPSA99169] is not registered
[sceProcessStarter] ProcessTerm() big/mini app title_id = [PPSA99169]
[SceShellUI] E/Base.BgmController : invalid path /user/app/PPSA99169/sce_sys/snd0.at9
```

and the folder contents do not match what was deployed:

```text
/data/homebrew/PPSA99169/eboot.bin   73,648 bytes   (ours is 18,791)
/data/homebrew/PPSA99988/eboot.bin   17,264,368     <- the title that runs
```

The registration *layout* is correct, though — compared folder by folder with the
title that runs, ours has every piece in place:

```text
/user/app/PPSA99169/       icon0.png, mount.lnk, sce_sys/
/user/appmeta/PPSA99169/   icon0.png, param.json, pic0.dds, pic1.dds, snd0.at9
/data/homebrew/PPSA99169/  eboot.bin, assets/, sce_module/, sce_sys/
/user/app/PPSA99988/       icon0.png, mount.lnk, sce_sys/          (identical shape)
/user/appmeta/PPSA99988/   icon0.png, param.json, pic0.dds, ...    (identical shape)
```

**Consequence.** Two independent things remain, and both are outside this
session's reach:

1. **The image in the folder is not the built one.** Our `eboot.bin` is 18,791
   bytes; the console's is 73,648. Every write path available here reports
   success and leaves the old bytes, so the 18,791-byte image — staged in
   `handoff/PPSA99169/` — has to be placed by the console's owner.
2. **The title is not registered with the shell.** `is not registered` and the
   missing `/user/app/PPSA99169/sce_sys/snd0.at9` say the title is absent from the
   shell's app database. The folders exist, so the remaining step is whatever the
   console's own installer does to register them — the console owner's route.

**Boundary.** This console and these two titles. A console where the owner has run
its installer for the title does not show the registration half.

---

## 2026-09-18: A launch is refused while another title is running

**Measured.** With PPSA99988 running (`procs` reported `title=PPSA99988 count=1
pids=214`), `launch PPSA99169` returned `0x80940010` and nothing started: the log
shows no execution and no crash for the new title, and `procs` was empty
afterwards.

**Consequence.** This console runs one title at a time, so a launch attempted
while another title is up proves nothing about the title being launched, and a
crash read from an undelimited log cannot be attributed. `tools/console-run.sh`
now checks the console first, refuses to launch into a busy console, records the
listener's position before launching, and judges only the lines after that mark.

**Boundary.** This console's launcher behaviour. `--force` exists for the case
where demonstrating the refusal is the point.

---

## 2026-09-18: VideoOut's constants and tiling are not derivable, and guessing costs a console run

**Measured.** Five things in the display path must be exactly what the console
expects, and the first version of `src/display.cpp` got all five wrong. The title
built, deployed and launched cleanly, then reported
`eboot.bin calls exit() exit_value=1` — the display had refused to open:

```text
symptom   eboot.bin calls exit() exit_value=1
```

| What | Wrong | Right |
| --- | --- | --- |
| pixel format | `0x80000000 \| 0x0a` | `0x8000000022000000` (64-bit) |
| direct-memory size | `int64_t` return | `size_t` return |
| mapping protection | `0x3` | `0x33` |
| buffer structure | three fields | four pointers: data, metadata, two reserved |
| frame addressing | row-major | tiled: 128x128 blocks in a fixed order, XOR'd block-local offsets |

The tiled layout is the one that would have produced a wrong-but-plausible result
rather than an error: a row-major write into a tiled frame does not fail, it
scrambles. `tiled_offset()` in `src/display.cpp` is the sibling application's,
kept verbatim with its reason.

**Consequence.** Everything in the display path is now taken from
`../ps5-native-app-boilerplate-main/src/demo_renderer.cpp`, which had already
proved each constant on hardware, instead of being reasoned out here. The
verification that matters is the console's own log: zero fatal signals, the
process visible in the shell's accounting, and the control payload reporting it
running.

**Boundary.** The console's VideoOut ABI as this project's SDK declares it.
Anything that changes one of these five values is a console-side change, not a
build option.

## A null joypad driver is this console's normal state, and upstream dereferences it

**Measured.** With every joypad driver upstream ships unavailable - udev, linuxraw,
SDL, XInput, dinput all need a library or a header this SDK does not carry -
`input_st->primary_joypad` is NULL. `input_driver_collect_system_input` passes it
straight to `input_joypad_analog_axis`, which reads `drv->axis` without checking
`drv`: the function guards every axis member against `AXIS_NONE` and never guards
the struct.

**What it looked like.** Nothing like a null pointer. The title started, the
display opened, the first frame was presented, and then it died on the *second*
pass through the runloop with SIGSEGV and fault address 0x18. The reason it
survived the first pass is that the call is inside `if (menu_is_alive)`, and
`MENU_ST_FLAG_ALIVE` is set after the first frame. Finding it took a probe in the
caller, then one at the function entry, then reading the loop: the fault address
0x18 is `joypad_info.joy_idx` (0x10) plus the `auto_binds` member (0x18).

**Fix.** `patches/series` 0004 returns 0 when `drv` is NULL, which is what the
function already answers when there is no axis to read.

## The menu is alive but its framebuffer is never marked dirty

**Measured.** `/app0/trace.txt` from a 25-second run: `rgui_fonts_init` completes,
RGUI holds a 320x240 framebuffer, and `rgui_set_texture` is called every frame -
but `GFX_DISP_FLAG_FB_DIRTY` is 0 on every one of those calls, so it returns
before handing anything to the driver. The driver therefore reports
`no-menu-source 4x4` for every frame: nothing is ever drawn, which is what "the
screen stayed black" is.

**Where the flag should come from.** `GFX_DISP_FLAG_FB_DIRTY` is set at the end of
`rgui_render`, and `rgui_render` is only reached through
`menu->driver_ctx->render` inside `if (BIT64_GET(menu->state, MENU_STATE_BLIT))`.
So the menu's renderer is never running. That is the next thing to measure: which
of the conditions above the call is false.

**Also measured, and separately true.** RGUI's bitmap fonts are downloaded assets,
not built-ins: `bitmapfont_10x10_load` returns NULL when
`<assets>/rgui/font/bitmap10x10_eng.bin` is missing, `rgui_fonts_init` then fails,
and `rgui_init` jumps to its error label with the menu dead. They are now bundled
under `assets/rgui/font/` and shipped in the title folder.

## Nothing this title submits has ever reached the display

**Measured.** The driver presents every frame and `sceVideoOutSubmitFlip` returns
success, but the screen shows nothing - not the menu, not a black frame, not a
painted test pattern. A run that paints three full-screen colour bands and
presents them 1800 times changes nothing on the television. The owner's own words:
"Nothing shows on the screen."

**The frame the driver writes is never flushed to the GPU.** The framebuffer is a
write-combined mapping of direct memory (`memory_type_write_combined_garlic`), and
the GPU does not see the CPU's dirty cache lines. `src/display.cpp` wrote pixels
and flipped without any cache flush, so the display read whatever was in that
memory before. The sibling project that works on this console flushes after every
write to the same kind of mapping - `_mm_clflush` over 64-byte lines then
`_mm_mfence()`, in `../PS5_Vulkan/driver/ps5vk_direct_memory.c`. That flush is now
in `src/display.cpp`.

**Where the sibling project actually differs, for the record.** `../PS5_Vulkan`
never calls `sceVideoOutSubmitFlip` at all: it uses `VK_KHR_display` and lets the
Vulkan driver own presentation, so its swapchain is fed by the GPU rather than by
CPU writes. What it does share with this port, and what is therefore proven on
this console, is the setup: `sceVideoOutOpen(0xff, 0, 0, NULL)`,
`sceVideoOutSetFlipRate(handle, 0)`, an 80-byte attribute zeroed then filled by
`sceVideoOutSetBufferAttribute2`, and `sceVideoOutRegisterBuffers2` with 2 buffers
of the same descriptor shape this port uses.

**One error is now named.** Asking for flip mode 0 returns `0x80290006`
immediately on the first flip, so mode 0 is not what this display wants; the flip
is back to `(1, 1)`, which returns success. Whether `(1, 1)` actually presents is
exactly what the cache flush will now decide.

**Also settled: why the menu has no pixels even though it is alive.**
`rgui_render` is called every frame with `width = 0, height = 0`, and returns at
its own guard. Those come from `video_st->width`/`video_st->height`, which nothing
sets because no core is loaded and the dummy core's AV info is empty. So there are
two separate faults, and the cache flush is the one that decides whether any pixel
this title writes becomes visible.

## Vulkan is prepared and switched off, and switching it back on is written down

**What was done.** RetroArch's Vulkan video driver is the retirement plan for the
hand-written path in `src/display.cpp`: it drives the menu itself
(`menu_driver_frame`), it uploads RGUI's framebuffer as a texture
(`vulkan_set_texture_frame`), and it loads the GPU side by filename -
`dylib_load("libvulkan.so.1")`, then `"libvulkan.so"` - so ../PS5_Vulkan's
libps5vk drops in beside the title. RGUI needs no GPU context of its own: its
render path references `gfx_display` eight times and every one is a type, not a
call.

**It builds.** With `--enable-vulkan` the frontend configures, compiles 268 of 278
sources and links, and the title grows from 8,030,159 to 8,227,775 bytes. Three
things had to be added, and all three are kept:

- `--enable-builtinglslang`: configure refuses to build the Vulkan driver without
  a GLSL-to-SPIR-V compiler, and RetroArch vendors glslang in `deps/glslang`.
- `src/video_filters_stub.cpp`: a Vulkan build names the filter chain's twenty
  entry points and one preset parser, and their implementation is not in
  upstream's tarball. They are stubs because this port has video filters off and
  the menu needs no shader; every create returns NULL, which is the driver's own
  "no chain" case.
- The glslang, SPIRV-Cross and `gfx/include` include paths, in
  `tools/build-retroarch.sh`.

**A tooling bug fell out of it, and it was a real one.**
`tools/retroarch-sources.sh` rewrote every object in `make info`'s list to `.c`,
but RetroArch's list is not all C: `gfx/drivers_shader/slang_process.cpp` is C++,
and the rewrite turned it into a path that does not exist. The frontend then
linked with `slang_preprocess_parse_parameters` undefined - a missing C++ source
reported as a missing symbol, which is a much longer walk back to the cause than a
path that says `.cpp`. The list now keeps each source's own extension (47 C++
sources in the full configuration) and the compile loop picks `-std=c++20` and
`-fno-exceptions -fno-rtti` for them.

**Why it is off.** With Vulkan enabled the title exits **1** within a second of
EXEC, with no signal and no message from RetroArch - and it does that whatever
`video_driver` the config names, including `"ps5"`, so a Vulkan build cannot fall
back to another driver. There is no ICD yet (`libvulkan.so.1` is not beside the
title), and that is the most probable cause, but it is not proven: the failure is
silent, and the next step is to make RetroArch say why - its own logs go to a file
in the title folder once `log_verbosity` is on.

**What switching it on costs, when the ICD exists.** One line in
`tools/retroarch-sources.sh` (`--enable-vulkan`) and one in the title's
`retroarch.cfg` (`video_driver = "vulkan"`). Everything else is already there.

## The shell's splash screen was covering every frame this title presented

**Measured.** The display's own flip status says so. `sceVideoOutGetFlipStatus`
fills sixteen 64-bit words and word 3 carries the marker of the latest flip the
display has *shown*:

    before:  flip status: call=0 marker=0 shown=0
    after:   flip status: call=0 marker=1 shown=1

`call=0` is a successful query in both. The marker is the difference: zero means
the display had not shown a single flip, one means it has shown flip 1.

**The change is one call.** `sceSystemServiceHideSplashScreen()`, before the
display is claimed in `Display::open`. It is the shell's startup splash, it sits
over the frame, and this port never asked for it to go. Nothing else changed: the
same two registered buffers, the same flip mode, the same cache flush.

**How it was found, because the route matters more than the fix.**
../PS5_Vulkan's `src/demo_renderer.cpp` is a minimal CPU-to-VideoOut template -
two frames drawn into direct memory, flush, register, one flip, one vblank wait,
and **no** `sceVideoOutGetFlipStatus` at all. Its constants and its sequence are
otherwise identical to this port's, character for character in the parts that
matter (frame size, `frame_bytes` 0x1000000, alignment 0x200000, memory type 3,
map protection 0x33, pixel format 0x8000000022000022's sibling
0x8000000022000000, `sceVideoOutOpen(0xff, 0, 0, NULL)`, `SetFlipRate(handle, 0)`,
the same 80-byte attribute, the same two-buffer registration, `SubmitFlip(...,
1, 1)`). Diffing the two sequences and taking each difference in turn left the
splash call as the one that mattered.

**What this does and does not settle.** Frames written by the CPU now reach the
screen through `sceVideoOutSubmitFlip` - a path this project had no evidence for
and which the sibling's driver does not use at all. It does not settle the menu,
which is the separate fault already recorded: `rgui_render` is called every frame
with `width = 0, height = 0`.

**The instrument that found it is the one to keep.** A flip status that says
"shown" is the only evidence this project has ever had that a pixel arrived, and
it is worth more than the return code of a submission call.

## A CPU-written 1920x1080 frame from a title does reach this console's screen

**Measured, by the console's owner.** The `../PS5_Vulkan` demo renderer
(`src/demo_renderer.cpp`, built as PPSA99999 "PS5 Vulkan Compatibility Probe") was
deployed and launched, and its diagnostic pattern appeared on the television:
three panels, a white rule, a cyan circle, a yellow square, a magenta triangle and
the text "PS5 DIAGNOSTIC HARNESS".

This settles the question this port has been circling for several rounds. A title
on this console can allocate direct memory, write 1920x1080 pixels into it with the
CPU, flush, register the buffers with VideoOut and present with
`sceVideoOutSubmitFlip` - and see them. It is not a path that requires the AGC
command processor, `sceAgcDcbSetFlip`, or a Vulkan swapchain.

**What the working sequence is, exactly** (`../PS5_Vulkan/src/demo_renderer.cpp`):
`sceSystemServiceHideSplashScreen()`; `sceVideoOutOpen(0xff, 0, 0, NULL)`;
`sceKernelAllocateDirectMemory(0, pool, 0x2000000, 0x200000, 3, &physical)`;
`sceKernelMapDirectMemory(&mapped, 0x2000000, 0x33, 0, physical, 0x200000)`;
draw both 16 MiB frames; `flush_range(mapped, 0x2000000)` - `clflush` per 64 bytes
then `mfence`; `sceVideoOutSetFlipRate(video, 0)`;
`sceVideoOutSetBufferAttribute2(&attr, 0x8000000022000000, 0, 1920, 1080, 0, 0, 0)`;
`sceVideoOutRegisterBuffers2(video, 0, 0, buffers, 2, &attr, 0, NULL)`;
`sceVideoOutSubmitFlip(video, 0, 1, 1)`; `sceVideoOutWaitVblank(video)`. Note that
it flips **buffer index 0** and never requests a status - the flip status query is
not part of the working path.

**This port's differences from it are now the whole of the remaining problem.** They
are small and enumerable, which is a much better position than the one this work
started from: the port rotates `registered_[back_]` rather than flipping a fixed
index; it queries `sceVideoOutGetFlipStatus` after the flip and reads the marker;
and it presents from RetroArch's callback (up to 30 times a second) rather than
drawing two frames once and holding. Each is testable against a known-good
reference, and the first thing to do is the smallest possible one: paint the bands
into a single buffer, register it, flip index 0 once, wait a vblank, and hold.

**One correction to an earlier entry.** The splash call helped - the flip status
went from `marker=0` to `marker=1` - but it did not make the port's frames appear.
The demo does call it too, so it is necessary and not sufficient, and the marker
value is weaker evidence than this entry: an owner's eyes on a rendered pattern.

## The fault is before presenting, and every static comparison matches

**Measured.** `present()` was reduced to the working sequence exactly: paint the
bands into buffer 0, `clflush` + `mfence`, `sceVideoOutSubmitFlip(handle, 0, 1, 1)`
once, `sceVideoOutWaitVblank`, then hold and never touch the display again. The
trace confirms the shape - `probe: flipped buffer 0 once, status=0 marker=1 (then
holding)` - and the title stays up until the loop closes it. The screen is still
black.

**What that rules out.** All three differences this port had from the working
sequence are gone from this build - buffer rotation, the `sceVideoOutGetFlipStatus`
query, and re-flipping every frame - and the screen is unchanged. Presenting is not
where the fault is.

**What has been compared and matches**, so it is not where the fault is either:

- allocation: `sceKernelAllocateDirectMemory(0, pool, 0x2000000, 0x200000,
  /*type*/ 3, &physical)` and `sceKernelMapDirectMemory(&mapped, 0x2000000,
  /*protection*/ 0x33, 0, physical, 0x200000)` - the same call, the same values;
- layout: two 16 MiB frames at offsets 0 and `frame_bytes`, registered from the
  mapping's base, exactly as `demo_renderer.cpp` builds its two `VideoBuffer`s;
- format: `sceVideoOutSetBufferAttribute2(&attr, 0x8000000022000000, 0, 1920, 1080,
  0, 0, 0)` and `RegisterBuffers2(handle, 0, 0, buffers, 2, &attr, 0, nullptr)`;
- colour encoding: `0xAARRGGBB` on both sides. The working demo's own table says
  so - its named colours are the giveaway that the order is B,G,R in bytes
  (`cyan = 0xffffff00`, `yellow = 0x00ffff`), and this port composes
  `0xff000000 | r<<16 | g<<8 | b`, which is the same;
- pixel addressing: the working demo's `put_pixel_unchecked` writes through
  `tiled_byte_offset(x, y)`, the same tiled layout this port's `Display::write`
  uses through `tiled_offset(x, y)`;
- and the arguments both sides pass to `sceVideoOutOpen(0xff, 0, 0, NULL)` and
  `sceVideoOutSetFlipRate(handle, 0)`, and both call
  `sceSystemServiceHideSplashScreen()` before opening the display.

**So the next step is a comparison, not a deduction.** Two ways, both cheap:

1. Read the first frame's buffer back on the CPU after the flip and compare it
   against the same frame drawn by `demo_renderer.cpp`'s own `Canvas`. If this
   port's buffer does not hold the bands, the write path is at fault and the
   difference is in `Display::write` or the surface it is handed; if it does hold
   them, then the memory being written is not the memory being displayed, and the
   difference is in the registration or the mapping.
2. Link `demo_renderer.cpp`'s `Canvas` code into this port unchanged, draw through
   it instead of through `Display`, and present with the probe's single flip. If
   that appears, the fault is in this port's own drawing; if it does not, the fault
   is in this port's display setup - and either way the working code is right there
   to bisect against.
