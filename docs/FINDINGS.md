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

## The bands are not reaching the screen, and the frame path taken is now in question

**Measured.** `present()` was reduced to the working sequence exactly (paint bands
into buffer 0, `clflush` + `mfence`, one `sceVideoOutSubmitFlip(handle, 0, 1, 1)`,
one `sceVideoOutWaitVblank`, then hold), and the screen is still black. That rules
out the three differences this port had from ../PS5_Vulkan's demo renderer -
rotation, the flip-status query, and re-flipping - because none of them are in that
build.

**Everything statically comparable matches**, and this is the list, checked one by
one against `../PS5_Vulkan/src/demo_renderer.cpp`: the direct-memory allocation
(`type 3`, alignment `0x200000`, `0x2000000` for two `0x1000000` frames), the map
protection `0x33`, the two-buffer registration from the mapping's base, the pixel
format `0x8000000022000000`, the colour encoding (`0xAARRGGBB`; the demo's own cyan
is literally `0xffffff00`), `sceVideoOutOpen(0xff, 0, 0, NULL)`,
`sceVideoOutSetFlipRate(handle, 0)`, `sceSystemServiceHideSplashScreen()` before
the display opens, and the tiled per-pixel addressing - the demo's
`put_pixel_unchecked` writes through `tiled_byte_offset(x, y)` and this port's
`Display::write` writes through `tiled_offset(x, y) / 4`, and the two functions
were compared numerically over 15 sample points and produce identical byte offsets.

**A readback was added and did not run.** It sits inside the `if (source == nullptr)`
branch of `ps5_frame`, reads the buffer back through the same tiled addressing that
wrote it, and logs the count of pixels that differ from what was intended. The
binary is deployed - the console's `eboot.bin` contains the `readback:` string - and
the line never appears in the trace, while the frame telemetry that follows it in
the same function does appear.

**The likely reason, to be confirmed.** `ps5_frame`'s trace tag says "no-menu-source"
when `have_menu_frame` is false, and that is set by `ps5_set_texture_frame`, not by
the `source == nullptr` test. Those are two different conditions. If the frontend is
handing this driver a real frame, the code takes the core-frame branch, never
touches `source == nullptr`, and the readback never runs - which is exactly what the
trace shows. The first thing the next round should do is log unconditionally at the
top of `ps5_frame` whether `frame` is null, with its width, height and pitch. That
one line says which branch the driver is actually taking, and the readback belongs
in whichever branch it is.

## The bands are provably in the buffer, the flip is accepted, and the screen is black

**Measured.** With the probe painting the frame itself and reading it back through
the same tiled addressing that wrote it:

    readback: base=200200000 1920x1080 wrong=0 of 2073600 (0.0% wrong)
    probe: flipped buffer 0 once, status=0 marker=1 (then holding)

Zero wrong pixels: the buffer holds exactly the three bands that were painted, in
the tiled layout the display is told about. The base is `0x200200000`. The flip of
buffer 0 is accepted and the display reports marker 1.

**And the screen stays black.**

**So the write half is proven and the display half is not.** Every value this port
passes to VideoOut matches `../PS5_Vulkan/src/demo_renderer.cpp` - the same
allocation, protection, alignment, two-buffer registration from the mapping base,
pixel format, `SetFlipRate(0)`, `HideSplashScreen` before open, and the same tiled
addressing, checked numerically - and the memory holds the right pixels. What
remains is the one thing no comparison of values can catch: whether the buffer that
was written is the buffer the display reads.

**A process lesson, and it cost two rounds.** The probe was left inside
`if (source == nullptr)` while the frontend hands this driver a real 4x4 frame on
every call, so the branch was never taken and none of the probe code ran. A
readback that never printed was read as a display fault rather than as code that
never ran. One unconditional log at the top of the frame callback - `frame=present
w=4 h=4 pitch=8 menu_frame=no` - settled it in a single run. Measure which path a
driver is on before building anything on top of it; do not infer it from a tag that
happens to be printed nearby.

**Also settled, and it explains a great deal.** The frontend asks this driver to
draw a **4x4** frame, not 1920x1080. That is why the image was never scaled to the
screen: `video_st->width/height` are zero because nothing ever called
`video_driver_set_size`, so RetroArch is presenting into a degenerate frame. The
probe sidesteps it by painting the display's own frame directly. Fixing the size is
the next thing for the real path, and `ps5_set_viewport` is where the driver can
report it.

## The menu was in the framebuffer all along, and the hand-over died on a struct size

**Measured, and the goal of this milestone.** `tools/run-title.sh --watch 20` runs
the title unattended, the console reports no fatal signal, and the console's owner
watched **RetroArch's RGUI menu on the television**. `/app0/trace.txt` from that run
(`evidence/ppsa-99169-rgui-menu-on-screen/`):

    ps5_init: own table ident=ps5 poke_interface=4015d0 set_viewport=401590 wrap_type_to_enum=0
    ps5_get_poke_interface entered
    ps5_set_texture_frame: rgb32=0 320x240 frame=present have=1 (was 0)
    ps5_frame 1: menu 320x240 pitch=640 present=1
    ps5_frame 60: sources so far: menu=yes core=yes
    display: flip 1200 of buffer 1, status=0 marker=1

**The fault was one line of the build, and it was invisible from every direction
this project had been looking.** `src/` was compiled with **no `-DHAVE_*` flags at
all**. The frontend archive is compiled with fifty of them
(`tools/retroarch-flags.sh`). RetroArch declares its driver interface as a struct
full of `#ifdef HAVE_OVERLAY` / `#ifdef HAVE_GFX_WIDGETS` members, so the two sides
disagreed about the struct: `video_ps5` was laid out **136 bytes** long while the
frontend read it as **144**. Everything after `overlay_interface` was read one
member late - `poke_interface` and `wrap_type_to_enum` both came back NULL, and
`alive()` was the right function only by luck.

**Why it looked like a display fault.** A NULL `poke_interface` is not an error
anywhere: `video_driver_init_internal` calls it only `if (...->poke_interface)`, so
nothing failed, nothing logged, and the driver opened the display and presented
1500 frames. RGUI rendered the menu into its 320x240 framebuffer on every frame and
handed it to `rgui_set_texture_frame`, which checks `video_st->poke` and returns
when it is NULL. The hand-over was dropped in silence, once per frame, for as long
as the title ran.

**Two instruments found it, and both are worth keeping.**

1. A probe at RGUI's own commit point printed what the frontend could see:
   `probe commit upscale=0 fb=320x240 data=1 poke=0 fn=0`. The framebuffer was
   allocated and the menu had been drawn into it; the poke was NULL.
2. `ps5_init` now logs its own table's members from inside the driver:
   `poke_interface=4015d0`. The two lines together say "this table has the function"
   and "the frontend cannot see it", which is a layout disagreement and nothing
   else.

**The check that keeps it fixed.** `tests/test_frontend.py` compiles RetroArch's
header with the frontend's own defines and compares `sizeof(video_driver_t)` against
the size of `video_ps5` in the object the title links, then reads the table's
relocations to confirm the member at the frontend's `poke_interface` offset is this
driver's hand-over function. Built without the defines it fails with
`video_ps5 is 136 bytes in the title's object and video_driver_t is 144 bytes in the
frontend` - verified by doing exactly that.

## The frame the display keeps is the frame it was given, and the runloop does not take it back

**Measured, and it corrects the lead this round started from.** The band probe was
made to hold its frame for 8 seconds inside `present()` instead of returning into
RetroArch's frame loop, with the flip status sampled every 500 ms. The console's
owner reported that the bands appeared **almost immediately** - not after the hold
began - and were **still on screen when the title closed**, twelve seconds after the
hold ended and `present()` had returned into the runloop.

So the behavioural difference between this port and `../PS5_Vulkan`'s renderer -
that the renderer flips once and then blocks forever while this port returns to
RetroArch's frame loop - is **not** what made the screen black. The runloop does not
undo a presented frame: it does not flip (the probe's `present()` returned early
after its first call), it does not clear the buffer, and the display stayed on
frame 1 (`marker=1`) for the whole 8-second hold with nothing else touching it.

**What was left, and it was the only other difference.** The port called
`sceAgcInit(8)` in `Display::open`, left over from the round that submitted flips
through an AGC command buffer. The renderer whose output has been seen on this
console contains no `sceAgc` call at all (`../PS5_Vulkan/src/demo_renderer.cpp`).
Removing it, together with the hold, is what produced the first pixels this port
ever put on the screen. Two changes went in together, so the attribution is not
separated by a clean A/B - what is ruled out is the runloop, by the timing above,
and what is left with no other candidate is the AGC initialisation. The clean test,
if it ever matters, is to restore `sceAgcInit` alone and watch for the bands.

**Two process notes, because both cost runs.** A probe left inside the configured
upstream tree does not disappear when you stop adding it: after deleting
`build/ra-conf` and reconfiguring, `tools/build-retroarch.sh` still reused the stale
`build/ra/obj/menu_drivers_rgui.c.o` from earlier in the day and shipped its probes
into `eboot.bin`. Deleting the object directory is what makes a probe really gone.
And a trace file that is appended to for every run stops being readable: this
session's `/app0/trace.txt` reached 72,904 lines because every run appends to it.

## The pad input driver is written and registered, and two faults upstream of it block it

**Measured.** A complete `input_driver_t` for this console lives in
`src/input_ps5.cpp`: it reads the DualSense through `scePadInit`/`scePadOpen`/
`scePadRead` with the 120-byte sample layout `../ProsperoLight` verified on
hardware, maps the pad's button words onto RetroArch's own numbering, and reports
sticks and triggers as axes. It is registered in `input_drivers[]`, its button map
is pinned by a host test, and `config/retroarch.cfg` names it. What is **not** true
is that the pad works: the title never reaches `ps5_input_init`, and the two
reasons are both upstream of the driver.

**Fault one, found by disassembly and fixed: RetroArch's built-in test input driver
was on.** `HAVE_TEST_DRIVERS` defaults to `yes` in `qb/config.params.sh`, and while
it is on `video_driver_init_input` opens with

    if (*input)
       if (strcmp(settings->arrays.input_driver, "test") != 0)
          return true;

so the whole input-driver initialisation is skipped whenever the configured driver
is not `"test"`. With it on, `input_drivers[]` holds the test driver, the null
driver and this project's driver, and the frontend's first entry is the test one:
`probe init_input entered: *input=ba4dc0 configured="null" joypad="null"` and then
`return true`. No input driver is ever initialised, so `current_data` stays NULL and
every button read is a call that answers 0. `--disable-test_drivers` is now in
`tools/retroarch-sources.sh`; it is a development driver for RetroArch's own test
suite and has no place in a shipping title.

**Fault two, found by trace and fixed: the title's `-c` was thrown away.** The entry
point passes `-f -c /app0/retroarch.cfg --verbose --menu`, and content loading
rebuilds a fresh argv from the frontend's environment
(`content_load_init_wrap`), which offers no `-c`: the pair RetroArch actually parsed
was `["retroarch", "--menu"]`. `probe config_parse_file: path="(null)"` is that
measurement, and `probe config_load: after parse input="null" video="ext"` shows the
consequence - **the title has been running on compiled defaults all along**, config
file ignored, `--verbose` dropped. The display worked only because the video
driver's *default* happened to be ps5 already. The fix is one guarded insertion in
`tasks/task_content.c` (`patches/series`, 0006) that restores the title's own config
path when the frontend environment does not name one.

**And that fix is parked, because it exposes a crash that is not yet understood.**
With the config actually read, the title dies on launch:

    # signal: 11 (SIGSEGV)
    # reason: page fault (user read instruction, page not present)
    # fault address: 0000000000000000
    # rip: 0000000000000000

`rip: 0` is a call through a null function pointer. It is not the input driver:
with `input_driver = "ps5"` the trace stops after `ps5_get_poke_interface entered`
and `ps5_input_init` is never entered, and with `input_driver = "null"` the same
crash happens once the config is read. Markers through `drivers_init` and
`video_driver_init_internal` show the whole video path completing - overlay
unload/init, context reset, display server, mouse cursor, audio init, core info all
print - so the crash is after that, in `rarch_main`'s runloop or the content task.
The parked change is `parked/config-path.patch.py`; the config keeps
`input_driver = "null"` so the title runs.

**What this means for the next round.** The order is: fix the config-path crash,
then the driver that is already written and tested should initialise and the pad
should work. The crash is a null call in the runloop after driver initialisation,
and the fastest instrument is a marker on `runloop_iterate` and the content task,
not more of the driver.

## The config-path crash, narrowed: it is reading the config, not any setting in it

**Measured, by elimination.** Four console runs, each differing in exactly one
thing, and the title's own trace says which variant ran because the entry point
marked it. All four used the same binary apart from that one change.

| argv | config read | result |
| --- | --- | --- |
| `-f -c /app0/retroarch.cfg --verbose --menu` | yes | SIGSEGV, `rip=0` |
| `-f -c /app0/retroarch.cfg --menu` (no `--verbose`) | yes | SIGSEGV, `rip=0` |
| `-c /app0/retroarch.cfg --menu` (no `-f`, no `--verbose`) | yes | SIGSEGV, `rip=0` |
| `-f --verbose --menu` (no `-c`) | no | runs, menu on screen, no signal |

So it is not `--verbose` finally taking effect, and not `video_fullscreen` finally
taking effect: dropping each of those leaves the crash. **It is the config file
being read at all.** That is a much narrower statement than the previous entry could
make, and it rules out the two things that change *behaviour* rather than
*settings*.

**And the config itself parses fine.** With `config_load` marked before and after:

    probe C:before-config-load
    probe C:after-config-load

so `config_load` -> `config_set_defaults` -> `config_parse_file` -> `config_load_file`
all complete. The crash is **after** the config is read and **before** the first
frame: no `ps5_frame 0` line ever appears, and neither `runloop_iterate` call site
in `rarch_main` is reached. It is inside `retroarch_main_init`, in the stretch
between the config load and the first frame - which is where the driver lookups and
`drivers_init` live.

**What is still ruled out, from the earlier marker run.** `drivers_init` and
`video_driver_init_internal` both complete: overlay unload, overlay init, context
reset, display server init, mouse cursor check, audio init and core info all print
their markers. And `ps5_input_init` is never entered, so the input driver is not
involved.

**The suspicion, stated as a suspicion.** With the config read, the frontend's
driver *selections* are no longer the compiled defaults: those lookups
(`audio_driver_find_driver`, `video_driver_find_driver`, `input_driver_find_driver`,
`camera_driver_find_driver`, `menu_driver_find_driver`) are the next thing after the
config load that behaves differently, and each of them indexes a driver table whose
contents this build has stripped to almost nothing. A default that names a driver
this build does not carry is the obvious candidate for a null call. That is where
the next instrument goes - and it must be placed with the editor, not a script:
reading a marker into the middle of a multi-line call is what produced
`undefined symbol: rarch_main` in this round's last attempt.

## The config-path crash is a null call in `command_event`, and the file logger cannot see it

**Where it is now, by markers that printed.** With the config-path fix applied, the
title's own trace ends at exactly this sequence:

    probe M:drivers-init-all        (before drivers_init(DRIVERS_CMD_ALL))
    probe M:input-init-command
    probe M:controller-init         (before command_event(CMD_EVENT_CONTROLLER_INIT))
    -> SIGSEGV, rip = 0

and three markers placed *after* that point never print: the first instruction of
`command_event_init_controllers`, the `case CMD_EVENT_CONTROLLER_INIT:` label, and
the statement after `command_event(...)` returns. So the crash is inside
`command_event()` before its switch reaches the controller case - one frame deeper
than the call site. The register dump is identical in every config-loading run
(`rip: 0`, `rdi`/`rsi`/`r13` pointing into one 0x138-byte frame), so it is
deterministic, not a race.

**A guard that is real but is not the fix.** `patches/series` 0007 adds the missing
null check before `core_set_controller_port_device`'s call to
`current_core.retro_set_controller_port_device`. Upstream calls that core callback
unguarded, and `dynamic_dummy.c` only survives because it defines an empty stub. It
is now verified in the object (`test %rax,%rax; je` before `call *%rax`) and the
title still dies with a byte-identical register dump, so it did not fix this crash.
It is kept because the missing check is genuine.

**The file logger cannot see this crash, and that is structural.**
`retroarch_main_init` calls `rarch_log_file_init(...)` *after*
`retroarch_parse_input_and_config(...)` returns, and `command_event` is reached
later still - but a crash during init happens before the logger opens its file. So
`log_to_file = true`, `log_to_file_timestamp = false` and `log_dir = "/app0"` are
now in `config/retroarch.cfg` and are **inert until the config path is fixed**; they
are there so the log appears the day it is. The logging settings themselves need no
build flag: `rarch_log_file_init` is compiled unconditionally.

**Two process failures in this round, both worth more than the finding.**
1. Parking `patches/series` 0006 was done by slicing the script by text, and the
   slice captured the wrong block: the parked file was truncated to zero bytes and
   the 0006 entry was deleted from the script while `build/ra-conf` still had it
   applied - so the next build rebuilt the crash *and* the patch record no longer
   matched the tree. Both files were recovered with `git checkout`.
2. Inserting a probe line after each of `command_event`'s getter statements split a
   multi-line `#if` block and produced `undefined symbol: char_list_new_special` at
   link time. Markers must be placed at statements, and the result must be compiled
   before it is trusted; two attempts this round were lost to this.

## The input driver is selected but never initialised, and that is one early return

**Measured.** With the compiled default changed to `"ps5"` (`patches/series` 0008),
a probe inside our own video driver - which does run - reports what the frontend
believes its drivers are:

    probe drivers: input="ps5" joypad="null" video="ext"

So the selection works and the name reaches the input subsystem. And
`ps5_input_init` still never runs: no `input:` line appears in the trace, and its
first statement is a trace call.

**Why, read out of the object.** `input_drivers[]` in this build holds exactly two
entries, and ours is first:

    R_X86_64_64   input_ps5
    R_X86_64_64   .data.input_null

`HAVE_TEST_DRIVERS` is `#undef`, so the test-driver branch is gone. What remains in
`video_driver_init_input` is:

    if (*input)
       return true;          /* <- taken */
    ...
    if (!(new_data = input_driver_init_wrap(...)))   /* never reached */

`input_driver_st.current_driver` is already non-NULL when that function is called,
because the pre-initialisation pass in `retroarch_main_init` runs
`input_driver_find_driver`, which *selects* a driver and deliberately does not
initialise it. So the configured driver is named, never wrapped, `current_data`
stays NULL, and the pad is a driver with no state.

**This corrects the earlier entry in this file.** Disabling `HAVE_TEST_DRIVERS` was
necessary but not sufficient: that flag removed one early return from that function,
and a second one - the `if (*input)` guard that upstream intends for the case where
a *video* driver pre-initialised input - is taken in this build for a different
reason, because pre-init already selected the driver. The fix has to make the input
driver actually initialise on this path, not just be selected.

**A process note that cost a run.** `strings` defaults to a four-character minimum,
so it reports nothing for a three-character literal like `"ps5"`; that made a
correctly patched and rebuilt object look unpatched. `grep -a` is the right check
for short literals.

## Selection is not initialisation: `ps5_input_init` has never run

**Measured, from inside our own driver.** The frontend calls a video driver's `init`
with two pointers whose documented purpose is "the video driver may pre-initialise
an input driver" (`gfx/video_driver.h`). Printing them from `ps5_init` gives:

    probe handed: input=be3ed0 input_data=be3ed8 *input=ba41e0 *input_data=0

So when our video driver starts, an input driver is already **selected**
(`*input` non-NULL) and its state is **zero** (`*input_data` NULL). `ps5_input_init`
has never been called - not on this run, not on any run: the trace holds no `input:`
line at all, and that function's first statement is a trace call.

**The path that should initialise it.** `input_driver_find_driver` runs during driver
pre-initialisation and only *selects* (`current_driver = input_drivers[i]`).
Initialisation is `input_driver_init_wrap`, whose only call on this path is the tail
of `video_driver_init_input` - and that function returns early when
`current_driver` is already set:

    if (*input)
       return true;                 <- taken, because pre-init selected
    ...
    input_driver_init_wrap(...)     <- never reached

Upstream intends that early return for a *video* driver that pre-initialised an
input driver itself. This build's video driver does not - it ignores the pointers on
purpose - so the early return is taken for a reason upstream never designed for, and
the wrap below is dead code.

**A fix that is compiled in and was not sufficient.** `patches/series` 0009 clears
the stale selection when `tmp` is NULL, so the code below re-selects and then wraps.
The object shows it is really there:

    test %rdi,%rdi          ; tmp == NULL?
    je   0x3e               ; -> store tmp (the video driver's own input driver)
    cmpq $0x0, ...+0x19b    ; current_driver
    jne  0xe6               ; -> still the early return
    movl $0xffffffff, ...+0x19c
    mov  %r14, ...+0x19c    ; store tmp

and it did **not** make the driver initialise: `ps5_input_init` still does not run.
So either that function is not reached at all on the second
`video_driver_init_internal`, or the re-selection inside it does not find `"ps5"`.
That is the one measurement left, and it needs a probe inside
`video_driver_init_input` itself - the last two rounds' probes were placed outside it
and could not see this.

**Why the earlier checks in this round were worthless.** Twice I grepped a compiled
object for a patch's *comment* text and concluded the patch was missing. Comments do
not survive compilation, and `strings` additionally has a four-character minimum, so
it reports nothing for a three-character literal like `"ps5"`. Both checks were
incapable of the answer they were asked for. The instruction stream is the only
reliable evidence that a code change was compiled.

## The pad works, the picture is live, and the fault was one comparison

**Measured, on the shipping build.** `bash tools/run-title.sh --watch 20`, with the
console owner working the pad, and the title's own trace:

    input: pad opened, user=515310723 handle=51119872
    input: press, pad=0x00000040 retropad=0x00000020     DOWN
    input: press, pad=0x00000010 retropad=0x00000010     UP
    input: press, pad=0x00004000 retropad=0x00000001     CROSS -> RetroPad B
    input: press, pad=0x00002000 retropad=0x00000100     CIRCLE -> RetroPad A
    input: press, pad=0x00000020 retropad=0x00000080     RIGHT
    input: press, pad=0x00000080 retropad=0x00000040     LEFT
    menu: framebuffer commit 2 is a new picture (2 of 2 changed so far)
    ps5_frame 600: menu commits=76 changes=14 presented=yes

Every mapping is the one intended, including the pairing that matters for a
PlayStation player: CIRCLE is RetroPad `A` (0x100) and CROSS is `B` (0x1). And the
menu is **not one frozen frame**: 76 commits of its framebuffer, 14 of them a
different picture, where every earlier run showed 1 and 1. Input is what made it
redraw, which is the answer to the frozen-frame question - the picture was never
frozen, the menu simply had nothing to redraw for.

**The fault, and it is one comparison.** `ps5_input_init` had never run. It is
reached through `input_driver_init_wrap`, whose only call on this path is the tail of
`video_driver_init_input`, and that function opens with

    if (*input)
       return true;              /* upstream: "keep the selected driver" */

Upstream intends this for a *video* driver that pre-initialised an input driver of
its own, and the tell is `tmp`. `video_driver_init_internal` sets

    tmp = input_state_get_ptr()->current_driver;

*before* calling `video_driver_find_driver`, so once the pre-initialisation pass has
selected a driver, `tmp` **is** that selection - not something a video driver
supplied. Measured:

    probe INV: entered tmp=ba41e0 *input=ba41e0 configured="ps5"
    probe INV: after-clear *input=ba41e0        (with the first, wrong fix)

The early return therefore fired for a reason upstream never designed for, the wrap
was dead code, `current_data` stayed NULL, and every button read answered 0. The
first fix - clearing the selection when `tmp == NULL` - never fired, because `tmp`
is not NULL. The fix that works is `patches/series` 0009:

    if (*input != NULL && *input == tmp)
       *input = NULL;

which says exactly what it means: no video driver supplied a *different* driver, so
the stale selection is dropped and the code below re-selects from the settings and
then initialises it.

**Two corrections to earlier entries in this file.** Disabling `HAVE_TEST_DRIVERS`
was necessary but not the whole story, and neither was `0008`: the driver was named
and the naming was never the problem. Both earlier rounds concluded "selection is not
initialisation" and both then assumed the remaining gate was somewhere they had not
looked; it was one pointer comparison in the function they had already read twice.
The probes that found it were inside `video_driver_init_input` - the three previous
rounds placed probes *around* it and could not see it.

## The probes are kept, because a rebuild destroys them

Every probe in this project is written into the configured copy of RetroArch, and
`tools/retroarch-sources.sh` rebuilds that copy from upstream - so the set that
found the input fault was lost the moment the tree was reconfigured, mid-round.
`tools/apply-runtime-probes.py` now holds them: version-controlled, applied with one
command, reverted with `--revert`, listed with `--list`, and never part of the
shipping build because `tools/build-title.sh` does not call it.

They are the landmarks that were expensive to find: what the input-driver
initialisation is handed (`tmp`, `*input`, the configured name), whether the
selection survived to the wrap decision, what the wrap returned, the line before any
input driver's own init, the call into `video_driver_init_input`, and the two
landmarks in `retroarch_main_init` that separated "the crash is in a driver" from
"the crash is after every driver".

## Reading the config makes the input driver work, and then the title dies on a refused syscall

**Measured, and it corrects the shape of this whole problem.** Re-applying the parked
config-path fix (0006) on top of everything since - 0007, 0008, 0009 - the config is
finally read, and the effect is immediate:

    ps5_init entered (video=present width=0 height=0)
    ps5_init: own table ident=ps5 ...
    ps5_get_poke_interface entered
    input: pad opened, user=515310723 handle=51250944

`input_joypad_driver = ""` was reaching the frontend at last, the input driver was
initialised, and the pad opened. Then the title died, and the console named something
new:

    # signal: 12 (SIGSYS)
    # rax: 0  r8: 0x80  r9: 0x80
    # process pid=223, coredump.elf calls exit() exit_value=0.

SIGSYS is a syscall the console refuses - not a null pointer, not a data fault. A
second signal follows it (`SIGSEGV`, `rip: 0`) as the process tears down, which is
the fault earlier rounds were reading, and why they kept looking for a null call.
The config being read is what makes the title reach a blocked syscall; nothing in the
config file is malformed.

**So the config does not need to be read for the pad to work.** `patches/series` 0008
names this project's driver as the compiled default, and with the config path parked
the pad opens and responds anyway - `menu commits=5 changes=2` on a 15-second run
with input live. That also means the config route is now purely about
configurability and the file logger, not about whether the injector or the pad
functions.

**A self-inflicted fault that masqueraded as this one, and cost most of a round.**
`tools/apply-port-patches.py` in the working tree held **14** patch blocks where the
committed script holds **10**: the controller-port guard (0007) had been added twice,
so `runloop.c` was patched twice in one build. Every run built from that tree died
with `rip: 0` and an 8-line trace, and I misread it as progress on the config fault.
`git checkout -- tools/apply-port-patches.py` restored the verified 10-block script,
and the title immediately ran again. Twice now this session a patch-script edit made
by slicing text has cost a round; the lesson is to count the blocks
(`grep -c '^    ($'`) after every edit to that file.

## With the config read, everything works up to the joypad step - which is where it dies

**Measured, in order, on one run with the config-path fix applied.** Probes at each
landmark give a complete sequence with no gap until the very end:

    probe CFG: load-file-enter
    probe CFG: load-file-body-start
    probe CFG: before-read
    probe CFG: after-read
    input: pad opened, user=515310723 handle=51578624
    probe X: wrap-before-init
    probe X: before-joypads
    -> dead

Read it as the phases it names. The config file **is opened and parsed** -
`after-read` prints, so `config_file_new_from_path_to_string` returns a real
`conf`, and nothing about the config file's contents or the reading of it is at
fault. The input driver **is** initialised, and this time its own init runs to
completion: `pad opened` is printed by `ps5_input_init` itself, and
`wrap-before-init` immediately precedes the call to it. Then `before-joypads` prints,
which is the first statement after that init returns, and nothing after it.

**So the remaining fault is in `input_driver_init_joypads()` or the moment after
it** - the last step of `input_driver_init_wrap`:

    if ((ret = input->init(name)))      /* returns: pad opened */
    {
       input_driver_init_joypads();      /* before-joypads prints, then nothing */
       return ret;
    }

That function calls `input_joypad_init_driver(settings->arrays.input_joypad_driver,
input_driver_st.current_data)`. This build compiles exactly one joypad driver,
`null_joypad`, and its `init` is a real function, so this is not the empty-table case
the earlier entries suspected. What is left is the `null_joypad` init itself or the
syscall behind it - and that is where SIGSYS (signal 12, `rax: 0`) points, not at the
config.

**This is a much better position than "the config crashes".** The config mechanism
is proven to work on this console: the file opens, parses, and its settings are
applied far enough to change driver behaviour (`input_driver = "ps5"` reaches the
frontend and the pad opens, which is only true when the config is read). The fault
is one call in the input subsystem, on a path that only exists because the config now
names this project's driver. It is also specific to reading the config: with the
config path parked, the input driver still initialises through the compiled default
and the pad works, because that route does not reach this call with the same
`current_data`.

**A note on the earlier SIGSYS attribution.** The previous entry said reading the
config "walks the title into a blocked syscall" and implied the file path was at
fault. The probes above show the file path completing; the refused syscall is downstream,
in the joypad step. The correction matters because it changes the fix: nothing about
`retroarch.cfg`, `fopen`, `stat` or the working directory needs to change.

## RetroArch now keeps its own log on the console, and it is the instrument the Vulkan path needs

**Measured.** With `--log-file=/app0/retroarch.log` added to the title's argument
list, a run leaves a real frontend log beside the title - 24 lines on this build,
alongside the ten-line `/app0/trace.txt` this project writes by hand:

    [INFO] Version: 1.22.2
    [ERROR] Couldn't find any audio driver named "ext"
    [INFO] Available audio drivers are:
    [INFO]   null
    [WARN] Going to default to first audio driver...
    [INFO] [Input] Found input driver: "ps5".
    [INFO] [Video] Graphics driver did not initialize an input driver. Attempting to pick a suitable driver.
    [INFO] [Video] Found display server: "null".
    [ERROR] Failed to initialize audio driver. Will continue without audio.
    [Core] Geometry: 320x240, Aspect: 1.333, FPS: 60.00

**Why the flag and not the config.** RetroArch initialises the file logger after it
parses its config, so the config's `log_to_file` settings are inert while the config
path is parked - and a crash during startup happens before that point anyway. The
`--log-file` argument is handled in *argument parsing*, which calls
`rarch_log_file_init` before the config is read
(`retroarch.c`: the "Enable logging to file if verbosity and log-file arguments were
passed" block, which precedes `config_load`). So it records startup, which is exactly
where the frontend has been silent.

**This closes the one gap that made the Vulkan path unworkable.** The recorded reason
Vulkan is off is that the title "exits 1 within a second of EXEC, with no signal and
no message from RetroArch". A failure that says nothing cannot be diagnosed from a
console run; now the frontend's own words land in a file after every run, Vulkan
included. Nothing else about the Vulkan path has changed - it still needs
`--enable-vulkan`, and the ICD still has to reach the title - but the next failure
will be readable instead of silent.

**Two facts the log already settles.** The audio driver the frontend looks for is
literally `"ext"` and the only one compiled is `null`, so audio is expected to fail
and the frontend continues - that is not a fault to chase. And the input driver is
found as `"ps5"` and the display server is `"null"`, which is correct for a build with
no graphics backend: the "display server" is a separate slot from the video driver.

## Vulkan is on, the delivered driver is staged, and the EXEC-1 is now a named step

**The blocker recorded for three sessions was not the ICD.** With `--enable-vulkan`
on, the frontend compiled **266 of 276** sources and **archived a title anyway**.
The ten that failed were all one cause:

    deps/SPIRV-Cross/spirv_cfg.cpp: cannot use 'throw' with exceptions disabled
    gfx/drivers_shader/shader_vulkan.cpp: cannot use 'throw' with exceptions disabled
    gfx/drivers_shader/slang_process.cpp: cannot use 'throw' with exceptions disabled
    ... seven more

`tools/build-retroarch.sh` compiled the frontend's C++ with
`-fno-exceptions -fno-rtti`, and SPIRV-Cross and RetroArch's Vulkan shader path need
exceptions. The archive was then built from what did compile, so the Vulkan shader
path was silently half-present - and because the build *succeeds*, nothing about the
run said so. With `-fexceptions` (RTTI still off) the frontend compiles **276 of
276**. The lesson generalises: `build-retroarch.sh` prints
`==> [ra] N sources did not compile: ...` and that line must be read on every build,
because a partial archive still links.

**What is in place now.** `--enable-vulkan`; the video driver's compiled default is
`"vulkan"` (`patches/series` 0010, the same mechanism 0009 uses for input, because
the config path is parked and the frontend runs on compiled defaults); the driver
`../PS5_Vulkan` delivers is staged beside the title by `tools/build-title.sh` from
`$PS5_VULKAN_ICD` or the sibling's `build/driver/ps5/libvulkan.so.1`, into both the
title root and `sce_module/`; and `patches/series` 0011 adds `/app0` and
`/app0/sce_module` fallbacks to RetroArch's `dylib_load` chain plus a `RARCH_ERR` on
total failure, because upstream's `dylib_load` logs nothing and that silence is what
made this path undiagnosable.

**The console re-signs a shared object on write.** Measured: the file staged here is
16,695,760 bytes with sha256 `25b32922...`, and the console serves 16,706,040 bytes
with sha256 `eb23125d...` - byte-for-byte the sibling's own `libvulkan.so.1.signed`.
So `tools/deploy-title.py` now checks a shared object the way it checks `eboot.bin`:
a byte string taken from the middle of the local library must appear in what the
console serves. A digest comparison there answers the wrong question and was
rejecting a correct upload.

**Where it still stops, and what that rules out.** The title reaches
`[Input] Found input driver: "ps5"`, then `rarch_main returned = 1`. The trace shows
**no `ps5_init`** at all, so with `video_driver = "vulkan"` the frontend fails
inside `video_driver_init_internal` before our driver is called - and the new
`RARCH_ERR` never printed, which places the failure *before* the `dylib_load` chain.
The candidates are therefore the graphics-context step and the `[Video] Found video
driver` lookup, not the ICD, not the dlopen path, and not the delivered object. That
is a much smaller space than "exits 1 with no message".

## A PS5 title cannot dlopen a driver, so Vulkan has to be linked - and the link is now the only gap

**Measured by ../PS5_Vulkan, on the console, with their e2-module runner.** This
overturns the delivery route this port has been built around for three sessions:

    sceKernelLoadStartModule("/app0/libvulkan.so.1")        -> 0x80020008 (ENOEXEC)
    the same for the FSELF-wrapped, soname-as-path and control variants
    "libvulkan.so.1" bare                                   -> 0x80020002 (ENOENT)
    dlopen answered NULL for all twelve candidates, including modules the process
    already holds, with dlerror() NULL every time
    sceKernelDlsym -> ESRCH for every name, on modules that do load

The discriminator is the module shape: a PS5 module image carries SCE module
parameters and an export table, which a linker-produced `.so` does not. So no name,
path or container fixes this, and RetroArch's `dylib_load("libvulkan.so.1")` can
never succeed here. The route that is proven on this console is their runner title's:
**link `libps5vk.ps5.a` into `eboot.bin` and call `vkGetInstanceProcAddr` as an
ordinary symbol.**

**Two corrections this forces, both recorded rather than quietly dropped.**

1. The "the console re-signs on write" observation in the previous entry is wrong.
   The byte difference is this project's own FTP tooling: `RETR` decrypts a SELF on
   read (the driver 16,695,760 -> 16,706,040, and `libc.prx` 1,284,674 ->
   1,335,962). Nothing re-signs on write, and the title sees the staged containers
   at their staged sizes. The `.so` marker verification added on the strength of
   that claim is harmless but was justified by the wrong reason.
2. `/app0` and `/app0/sce_module` are the only paths a title can see; `/data/homebrew/`
   and `/temp0` do not exist inside a title. The `/app0` dlopen fallbacks are
   therefore moot - the load is refused for every path, not just the bare name.

**What is in place now, and it is most of the work.** `patches/series` 0012 replaces
RetroArch's `dylib_load`/`dylib_proc` pair with a direct reference to the linked
`vkGetInstanceProcAddr` (with a local prototype, because RetroArch's Vulkan headers
declare the `PFN_` type but not the function). The frontend compiles **276 of 276**
with Vulkan on and no longer contains a dynamic load. `tools/build-title.sh` names
the sibling's four released archives - `libps5vk.ps5.a`, Mesa's
`libvk_runtime.ps5.a`, `libpsbc_driver.ps5.a`, `libpsbc_support.ps5.a` - and
`PS5_VULKAN_DIR` overrides their root; `tools/build.sh` links them whole
(`--whole-archive`) with the sibling's `--no-dynamic-linker
-z nodynamic-undefined-weak`, and adds `libc++.a`, `libc++abi.a`, `libunwind.a` and
the compiler builtins in a `--start-group`, because the shader compiler and Mesa's
runtime are C++.

**What the link still wants, and it is a short list.** With all of the above the
linker reports `__eh_frame_start`, `__eh_frame_end`, `__eh_frame_hdr_start`,
`__eh_frame_hdr_end`, `u_thread_create` and `util_barrier_init`. The first four come
from ../PS5_Vulkan's own linker script, `tooling/psbc/ps5-pie-unwind.ld`, which its
`tools/psbc-link.sh` passes with `-T`; the last two are Mesa's threading util, which
that script satisfies from its psbc runtime group. So the remaining work is to adopt
that link recipe rather than to discover anything: archives, the unwind script, and
the group ordering, all named in their `tools/psbc-link.sh`.

## The Vulkan path is the one running, and its refusals are now readable

**Measured.** A fresh run's `/app0/trace.txt` (952 KB) opens with the driver's own
refusals, which are readable only because `src/main.cpp` points `stderr` at the
trace file unbuffered. They are, by name and count:

- `only triangle lists without primitive restart are supported (VK_ERROR_UNKNOWN)`
  - 21 times, raised at `driver/ps5vk_pipeline.c:903`;
- `set 0 binding 3: descriptor type 3 has no proven table entry (VK_ERROR_UNKNOWN)`
  - 3 times, raised at `driver/ps5vk_pipeline.c:137`;
- `sampler address modes N, N and N are not the clamp-to-edge ...` - 16 times,
  raised at `driver/ps5vk_image.c:804`.

So the frontend is running RetroArch's Vulkan video driver against
`../PS5_Vulkan`'s libps5vk, not this port's hand-written `video_ps5`.

**A contradiction to resolve, and it is the next thing to measure.** The title's
config says `video_driver = "ps5"` (both `config/retroarch.cfg` and the deployed
copy), yet the trace's refusals come from the Vulkan driver. And
`/app0/retroarch.log` is 1,200 bytes with no timestamps and was byte-identical
across two runs, which is what a stale file looks like - so it may have been
written by an earlier build and may not describe the run just made. Its contents
are at least consistent with an older `video_ps5` run: `[Input] Found input driver:
"ps5"`, `[Core] Geometry: 320x240`, `[Environ] SET_PIXEL_FORMAT: RGB565`,
`[Video] Found display server: "null"`, and no mention of Vulkan at all.

**Why the refusals matter more than they look.** `driver/ps5vk_private.h`
documents `ps5vk_cmd_buffer_refuse`: a refusal *"records that cmd_buffer cannot
encode a command: logs why, and recording ends with result at vkEndCommandBuffer"*.
A command buffer that ends in error is not submitted, so a refused command means
the draws never reached the GPU - and the flip can still succeed, which is exactly
the shape of "644 presents accepted, black screen". RetroArch calls
`vkEndCommandBuffer` at `gfx/drivers/vulkan.c:4212` and **discards the result**, so
the frontend cannot see this on its own. The driver's refusals in the trace are the
only witness.

**Consequence.** Everything this port learned about its own `video_ps5` display
path - the correct buffer, the accepted flip, the marker, the splash call - is
about a driver that is not the one running. The display work is not wasted (it is
the fallback path and it is proven to the buffer), but it is not the current
blocker. The current blocker is that the Vulkan driver refuses commands the
frontend issues.

## The frontend runs the linked libps5vk, and the driver refuses its draws

**Measured, and this corrects the entry above it.** The stale-artefact reading was
wrong: after truncating `/app0/trace.txt` to zero bytes and running once, the fresh
trace contains the *same* refusals. They are produced by the run, not left over.

**The build is the Vulkan build.** `build/ra-conf/config.h` has `HAVE_VULKAN 1` and
`HAVE_VULKAN_DISPLAY 1`; `gfx_drivers_vulkan.c.o` is built (177,104 bytes); the
image is 33,788,426 bytes, not the 8 MB of the hand-written driver. Two earlier
measurements of mine were against a different, smaller build and misled me - the
"zero video_vulkan strings" count and the 8,030,348-byte size.

**The driver table is `video_vulkan` then `video_ps5`** (`video_drivers[]` in the
configured `gfx/video_driver.c`, `&video_ps5` at line 354). Vulkan is registered
first, and the trace's refusals quote `../PS5_Vulkan`'s own source
(`driver/ps5vk_pipeline.c:903`, `:137`, `driver/ps5vk_image.c:804`), so the running
video driver is the linked libps5vk and not this port's `video_ps5`.

**`/app0/retroarch.log` is not stale and does not contradict this.** Its
`[Input] Found input driver: "ps5"` is the *input* driver, which this port does
supply (`src/` has a pad driver); `[Video] Found display server: "null"` is the
display server, a different slot again. Neither names the video driver, so the file
never said what I first thought it said. The config's `video_driver = "ps5"` names
a driver that is registered second, so it is not the one used either.

**The blocker.** The frontend's draws are refused by the linked driver, and by
`driver/ps5vk_private.h`'s own rule a refusal ends recording with an error at
`vkEndCommandBuffer`, so those command buffers are not submitted - which is why
frames are accepted and the screen is black. RetroArch discards
`vkEndCommandBuffer`'s result (`gfx/drivers/vulkan.c:4212`), so the refusals in
this trace are the only witness the port has that its draws are going nowhere.

Of the three refusal sources, the maintainer has already classified all three as
expected gaps rather than bugs: triangle strips (`driver/ps5vk_pipeline.c:896-904`),
storage images (`driver/ps5vk_descriptor_set_layout.c:20-38`, which would need a
libpsbc shader-compiler change), and sampler address modes
(`driver/ps5vk_image.c:800-806`, C4 scope). Two are avoidable from this side - the
descriptor type and, in principle, the topology - but the sampler address modes are
a driver-side gap.

**What this means for the work so far.** The `video_ps5` display investigation
stands on its own and is proven to the buffer, but it is a second, unused path
while Vulkan is registered first. The live problem is the driver refusing the
frontend's commands.

## The video driver is chosen by a compiled default, and the config cannot override it

**Measured.** `tools/apply-port-patches.py` patches `configuration.c` so that
`case VIDEO_NULL:` returns a name instead of falling through - the compiled default
video driver, used when no config file is read. Its own comment says why the config
cannot help: *"There is no config file at runtime yet: content loading rebuilds argv
and drops the title's `-c`, and the fix for that is parked because reading the
config still crashes the launch."* So `/app0/retroarch.cfg`'s
`video_driver = "ps5"` is inert, even though the file is present, correct, and read
for its logging settings.

**The ICD file is a red herring.** `/data/homebrew/PPSA99169/libvulkan.so.1`
(16,706,040 bytes) is beside the title, and renaming it away changed nothing: the
same refusals appeared. The Vulkan driver in use is the one linked statically from
`libps5vk.ps5.a`, not something loaded by name at runtime.

**Changing the compiled default to `"ps5"` did not take.** The patch was rewritten
so `case VIDEO_NULL:` returns `"ps5"`, the configured tree was reset and the patches
re-applied, the change was confirmed in `build/ra-conf/configuration.c`, and the
title rebuilt (33,790,346 bytes). The run produced the same 21 topology, 3
descriptor-type and 16 sampler address-mode refusals. Something other than that
switch still selects Vulkan, and it has not been found. `video_ps5` remains
registered in `video_drivers[]` at index 3 and unreachable in practice.

**The blocker, stated once.** The frontend's draws are refused by the linked
libps5vk. Of the three refusal sources the maintainer has classified all three as
expected gaps, and the sampler address-mode restriction
(`driver/ps5vk_image.c:800-806`, C4 scope) has no workaround from this side: it is a
driver-side change in a tree this project does not modify. Until it is lifted - or
until the frontend is shown how to avoid it - the menu's draws never reach the GPU,
because a refusal ends recording with an error at `vkEndCommandBuffer` and
RetroArch discards that result.

## The compiled default now names this project's own driver

**Changed.** `tools/apply-port-patches.py`'s `configuration.c` patch returns `"ps5"`
instead of `"vulkan"`. That switch is the whole of the video-driver choice, because
its own comment records that the config cannot make it: content loading rebuilds
argv and drops the title's `-c`, so `/app0/retroarch.cfg`'s `video_driver` is never
read. Naming this project's driver there makes `video_ps5` the driver that runs, and
`video_ps5`'s display path is the one proven as far as the buffer.

**Two traps hit while making it, both worth remembering.**

1. **A build artifact lied about which source it came from.** After patching, the
   object still behaved like the old one. The cause was that the port's build
   applies the patch and then compiles, so the *order* of `build-retroarch.sh`'s
   steps decides whether a patch reaches the object - and an object whose mtime is
   newer than the already-patched source is kept. Deleting
   `build/ra/obj/configuration.c.o` and rebuilding was the only way to be sure. The
   lesson is the same one this project learned with `retroarch.c`: after changing a
   patch, remove the object it belongs to rather than trusting the build to notice.
2. **A `strings` check on the driver name was meaningless.** Counting `^ps5$` in an
   object cannot distinguish the two builds, because the linker merges string
   suffixes and a short name may not survive as its own entry. It reported `ps5=0
   vulkan=1` for a binary that did contain the change. The driver name is the wrong
   thing to grep for.

**Not yet measured: whether this switch actually selects `video_ps5` on the
console.** The runs made with it in place still produced libps5vk's refusals, which
is why it is recorded as unverified rather than as working. The next run with the
console free should be judged on the trace alone: `video_ps5`'s marks
(`ps5_init entered`, `ps5_frame ...`) appear if it was selected, and the `vulkan:`
refusals appear if it was not.

## The compiled default is `ps5` and the console still runs Vulkan

**Measured, and this closes the route.** `build/ra-conf/configuration.c` line 1152's
`case VIDEO_NULL:` returns `"ps5"` - confirmed by reading the configured tree, not
by grepping a binary. The title was rebuilt from it (33,793,610 bytes) and the
console still produced the linked libps5vk's refusals:

    vulkan: only triangle lists without primitive restart are supported (VK_ERROR_UNKNOWN)  x21
    vulkan: set 0 binding 3: descriptor type 3 has no proven table entry (VK_ERROR_UNKNOWN)  x3
    vulkan: sampler address modes ... (VK_ERROR_UNKNOWN)  x16

So `config_get_default_video()` is not what selects the video driver on this build,
and changing it has no effect. The frontend's own log names no video driver either -
it reports the *input* driver (`ps5`) and the display server (`null`), never the
video driver - so the frontend cannot say which driver it chose and the refusals in
the trace are the only witness.

Two candidates remain, and both are outside this project: `frontend_driver_get_video_driver()`
(`gfx/video_driver.c` returns early if the frontend context supplies a driver,
before any name lookup) and RetroArch's own runner/profile mechanism. Neither was
determined.

**The blocker, stated once and for the last time in this document.** The frontend's
draws are refused by the linked libps5vk. A refusal ends recording with an error at
`vkEndCommandBuffer` (`driver/ps5vk_private.h`), so those command buffers are never
submitted, which is why frames are accepted and the screen is black. Of the three
refusal sources the maintainer classified all three as expected gaps, and the
sampler address-mode restriction (`driver/ps5vk_image.c:800-806`, C4 scope) cannot
be worked around from this side. Until it is lifted, or until the frontend is
changed to request only clamp-to-edge samplers, the menu's draws do not reach the
GPU. Both are changes in `../PS5_Vulkan`, which this project does not modify.

## The RetroArch menu is on the console's screen, drawn by this project's own driver

**Measured, by the console's owner.** `PPSA99169` runs this port's `video_ps5`
driver, receives RGUI's 320x240 framebuffer, presents it, and the RetroArch menu is
visible on the television. That is the objective this project was built for.

**The evidence, in one unattended `tools/run-title.sh` run** (capture
`klog/run-PPSA99169-080439.log`: one EXEC, **zero fatal-signal lines**; the script
launched it, watched 20 s, found it still running and closed it itself):

    ps5_init entered (video=present width=960 height=720)
    display: virtual=200200000 physical=0x200000 bytes=0x2000000 two buffers at +0
             and +0x1000000 registered from 0 set 0, 1920x1080, format=0x8000000022000000
    ps5_init: told the frontend the display is 1920x1080
    input: pad opened, user=515310723 handle=51447552
    display: flip 1 of buffer 0, status=0 marker=1
    ps5_set_texture_frame: rgb32=0 320x240 frame=present have=1 (was 0)
    menu: framebuffer commit 1 is a new picture (1 of 1 changed so far)
    ps5_frame 1: menu 320x240 pitch=640 present=1
    ps5_frame 60: sources so far: menu=yes core=yes
    ps5_frame 600: menu commits=3 changes=2 presented=yes
    ps5_frame 1200: menu commits=3 changes=2 presented=yes

Three things in that trace are worth naming, because each was a fault this document
previously recorded as open:

- **`menu 320x240`** - RGUI's framebuffer reaches the driver. The commit that fixed
  `width=0, height=0` is why: the driver now tells the frontend the display is
  1920x1080, so the menu renders into a real frame instead of a degenerate one.
- **`rgb32=0 ... 320x240 frame=present`** - the menu's RGB565 buffer arrives, and
  `ps5_frame`'s conversion path handles the 16-bit source rather than dropping it.
- **`menu commits=3 changes=2`** - the framebuffer is not a static image. It changed
  twice, once on a pad press (`input: press, pad=0x00000040 retropad=0x00000020`
  appears immediately before a new commit), so the pad reaches the menu and the menu
  responds by redrawing.

**What actually unblocked it, and it was one anchoring mistake twice over.** The
video driver is chosen by `config_get_default_video()`, whose switch is over
`VIDEO_DEFAULT_DRIVER`; in this build that resolves to `VIDEO_VULKAN`, so the
function returns at `case VIDEO_VULKAN:` and never reaches the `case VIDEO_NULL:`
arm patch 0010 was editing. Changing that arm had no effect for exactly that reason.
Two corrections, both from ../PS5_Vulkan's maintainer and both verified here:

1. `tools/apply-port-patches.py` now anchors on `case VIDEO_VULKAN:` and returns
   `"ps5"` there - the arm that fires.
2. The driver-table edit inserted `&video_ps5` *after* the `#ifdef HAVE_VULKAN`
   block, leaving `&video_vulkan` at index 0, and index 0 is what RetroArch falls
   back to when a name is empty or unfindable. It is now inserted **before** the
   block, so this project's driver is first as well as named.

**One thing not explained.** The flip status marker reads 1 at flips 1, 300, 600,
900 and 1200 - it never advances, while the buffers rotate correctly (0, 1, 0, 1) and
the frames are visibly on screen. So the marker is not a usable instrument for
"has the display taken a newer frame" on this console, whatever it reports. The
display work was read through it for several rounds; the pictures on the screen are
the evidence that matters.

## A1 is one choke point, not many sites - and the static buffers are exactly a quad

**Measured by reading the code.** The strip topology the driver refuses is not chosen
in one place: `gfx_display.c:538`, `:678`, `:937`, `gfx_thumbnail.c:1005`,
`gfx_widgets.c:645` and `materialui.c:2583` all set
`draw.prim_type = GFX_DISPLAY_PRIM_TRIANGLESTRIP`, and the Vulkan driver derives the
pipeline from it - `disp_pipeline = ((draw->prim_type == GFX_DISPLAY_PRIM_TRIANGLESTRIP) << 1) | blend`
(`gfx/drivers/vulkan.c:1470`). So topology and geometry are chosen together, and
patching the pipeline alone would draw wrong triangles rather than refuse - a silent
wrong picture, which is worse than a refusal. Patching every call site is the wrong
shape for a port whose changes are meant to be a short named list.

**The choke point is `gfx_display_vk_draw`** (`gfx/drivers/vulkan.c:1379`). Every menu
draw passes through it, and it already re-bakes the caller's separate vertex,
tex-coord and colour arrays into an interleaved VBO. A strip can be expanded to a
triangle list inside that loop, and the only other change is to report
`GFX_DISPLAY_PRIM_TRIANGLESTRIP` as the list pipeline, so the menu's own vertex data
does not have to change at all.

**The trap that makes it delicate.** For four strip vertices the loop reads eight
floats from `vertex` - and when the caller supplies no coordinates, `vertex` is
`&vk_vertexes[0]`, a **static array of exactly 8 floats** (`:1246`), with
`vk_tex_coords[8]` (`:1253`) the same. A conversion that reads six interleaved
vertices instead of four therefore reads past the end of a static array. The
expansion has to be done as an index mapping - emitting `(N-2)*3` list vertices while
reading source index `s(i)` from the original arrays - not by duplicating the
interleaved values after the fact.

**Not started.** The change is small but lands in the middle of the frontend's draw
path, next to the shader pipeline ids (`VIDEO_SHADER_MENU` .. `_5`, which have their
own `to_menu_pipeline` mapping at `:1453`). It wants a fresh round with the console
free, and it is the first of the three refusals rather than the whole job.

## A2's format match fails on one remap, and three refusals are all that remain

**Measured on the console, Vulkan running.** After the corrected A1 (all four
pipeline creation sites build triangle lists) the refusal count fell from 24 to
**3**, and every `only triangle lists without primitive restart` line is gone. The
three that remain are all one cause:

    vulkan: set 0 binding 3: descriptor type 3 has no proven table entry (VK_ERROR_UNKNOWN)

That is the storage-image descriptor written by `vulkan_copy_staging_to_dynamic`'s
compute branch, which is taken when `dynamic->format != staging->format`.

**Why they still differ, and it is one line.**
`vulkan_create_texture` remaps a format for the *optimal* texture but not for the
staging one:

    #define VK_REMAP_TO_TEXFMT(fmt) ((fmt == VK_FORMAT_R5G6B6... ) ? R8G8B8A8 : fmt)
    ...
    if (remap_tex_fmt != format) {
       if (type == VULKAN_TEXTURE_STREAMED) ...      /* keeps the original */
       else if (type == VULKAN_TEXTURE_DYNAMIC) format = remap_tex_fmt;
    }

The macro only rewrites RGB565; every other format passes through unchanged. Patch
0027 forces `fmt = VK_FORMAT_R8G8B8A8_UNORM`, which is *not* RGB565, so nothing is
remapped: the dynamic texture is created as R8G8B8A8 and the staging texture - the
same `fmt` argument - as R8G8B8A8 too. On that reading they should match, and the
compute branch should not be taken. It is taken, so something in the chain still
diverges, and where was not determined in this round.

**The candidate worth checking first.** The comment in `vulkan_create_texture` shows
a probe that *undoes* a remap when the device can sample the remapped format
(`remap_probe.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT` sets
`remap_tex_fmt = format`). If that probe behaves differently for the dynamic and
staging textures - one created before the other, or with different `type` - the two
end up with different formats from the same input. A one-line trace of
`dynamic->format` and `staging->format` in `vulkan_copy_staging_to_dynamic` settles
it in a single run, and that is the next step rather than another inference.

**Also settled in this round.** An earlier round had already changed
`configuration.c` to return "vulkan", so the compiled default is Vulkan and the
objective's "CPU path stays selectable" is satisfied by `video_ps5` remaining
registered, not by it being the default. A check of the deployed image for the
`patches/series, 0027` marker returned 0 and proved nothing: the marker is inside a
comment, and comments do not survive compilation.

## Why the GPU path was black: seven faults that all failed silently (2026-09-19)

Every fault below produced the same three symptoms - a clean trace with no refusal,
a driver presenting ~45 frames a second, and a black screen - which is why five
successive readings of the source produced five wrong answers and the faults were
finally found by instrumenting the console and reading one frame in order.

The measurements, from the run that has all seven fixed, in trace order:

    menu iterate 0: state=17 ret=0
    rgui render 0: blit-state reached
    rgui set_texture: rgui=yes dirty=1 data=yes
    vulkan set_texture_frame: rgb32=0 320x240 frame=yes
    vulkan create_texture: asked=1 type=1 320x240 fmt=37 image=no buffer=yes
    vulkan create_texture: asked=2 type=2 320x240 fmt=37 image=yes buffer=no
    vulkan menu state: flag=1 idx=0 staging(img=0 buf=1) optimal(img=1 buf=0)
    vulkan copy_staging_to_dynamic: dynamic 320x240 fmt=37 type=2, staging fmt=37 type=1, compute=0
    vulkan draw_quad 0: texture=yes image=yes layout=5 320x240 pipe=yes

1. **The last refusal was a compute pipeline, not an upload.** libps5vk's
   `ps5vk_descriptor_options` walks every set-0 binding whose stage flags match the
   stage being compiled and refuses any with stride 0; a storage image's stride is
   zero by design (`ps5vk_descriptor_stride`), and this frontend's shared set
   declares a compute-only storage image at binding 3. Exactly one pipeline is
   compiled with COMPUTE against that layout - `rgb565_to_rgba8888`, the upload
   shader - so the refusal fired once per `vulkan_init_pipelines` call: at init and
   again on every swapchain recreation, three times in the recorded run, every line
   identical. The pipeline is unnecessary here; 0027's single-format change had
   already made its branch unreachable, and the branch would fail its own
   `retro_assert`. Not compiling it ends the refusals (0036).

2. **Widgets answered true for a driver whose menu widgets do not draw.** RetroArch
   chooses between two mutually exclusive branches: `gfx_widgets_init` when the
   driver reports `gfx_widgets_enabled`, else `gfx_display_init_first_driver`. The
   latter is the only thing that sets `p_disp->dispctx`, and `gfx_display_draw`
   returns immediately without it (`gfx_display.c:643`). Answering true therefore
   cost the menu its display context and every draw returned in silence (0042).

3. **Nothing enabled the menu texture.** The driver draws the menu only inside
   `if (vk->flags & VK_FLAG_MENU_ENABLE)`, and the only place in the whole frontend
   that sets it true is `display_menu_libretro` - whose own comment says "Display
   the libretro core's framebuffer onscreen". It is a libretro concept and it is set
   when a core runs. This title launches into the menu with no content, so it was
   never set. `video_ps5` never noticed because it ignores the flag and draws
   whatever `set_texture_frame` handed it (0046).

4. **The sampled image was created and never filled.** `vulkan_set_texture_frame`
   fills `vk->menu.textures[]` (staging) and the draw samples
   `vk->menu.textures_optimal[]`. The optimal image is written by exactly one thing,
   `vulkan_copy_staging_to_dynamic`, called from `vulkan_frame`'s upload block. That
   block is gated on `vk->menu.dirty[]`, which this function sets at its end for
   both cases - but the function itself fills the optimal image only when the
   staging texture was just created. From the second handover on it took the `else`
   branch, which flushed the staging texture to the GPU and never copied it, so the
   menu was drawn every frame from an untouched allocation: a valid image, a valid
   pipeline, a valid draw, black pixels (0051).

5. **A leftover file on the console ended every run.** `/app0/args.txt` is read by
   the driver as well as by `src/main.cpp`, deploy never deletes anything, and a
   capture file left by an earlier probe made each run capture a frame at 90, write
   it, and then call `command_event(CMD_EVENT_QUIT, NULL)`. The title therefore
   disappeared about 1.5 seconds after it appeared, and the console booked it as an
   application crash with a coredump - a symptom that reads as "it crashes and there
   is no picture". The quit is gone (0039) and `tools/run-title.sh` clears the file
   before every run (0043).

6. **The draw was instrumented in the wrong place.** `gfx_display_vk_draw` is the
   display-list path used by GL-style drivers. This driver composites the menu
   itself, inside `vulkan_frame`, through `vulkan_draw_quad`. A probe there reported
   "no draws" for three rounds, which was true of the path being watched and false
   about the driver (0053).

7. **A probe ended the run it was measuring.** The capture block's
   `CMD_EVENT_QUIT` is the mechanism in fault 5; recorded separately because the
   general rule is the one worth keeping: a probe must not change the run.

Left open, precisely: with the menu texture filled, the quad drawn every frame with
`texture=yes image=yes layout=5 320x240 pipe=yes`, and no refusal anywhere, the
screen is still black. The next question is what the driver does with that draw -
`../PS5_Vulkan`'s `ps5vk_draw.c`, the AGC stream it writes for the quad and the
viewport it programs - not any further gate on this side.
