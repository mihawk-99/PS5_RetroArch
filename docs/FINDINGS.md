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
