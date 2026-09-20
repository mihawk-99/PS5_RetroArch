# PPSSPP implementation plan: software core first, Vulkan renderer second

This is the implementation plan for adding PPSSPP to this title. It is the
executable counterpart to [PPSSPP_Core_Plan.md](PPSSPP_Core_Plan.md), which holds
the platform analysis, the driver findings and the acceptance gates this plan
inherits. Read that document for *why* each constraint exists; this one says what
gets built, in what order, and what proves each step.

The plan is stable. It records no progress, no dates and no results: the current
step is in `docs/ACTIVE.md`, the run history is in `docs/PHASE_LOG.md`, and the
measurements behind each decision are in `docs/FINDINGS.md`.

## 1. The shape of the work

Two milestones, one core binary:

| Milestone | What runs | Graphics | Depends on PS5_Vulkan |
| --- | --- | --- | --- |
| **Track A** (this plan's first half) | Real PSP emulation: MIPS JIT, software GPU core, audio, input, saves | PPSSPP's CPU rasterizer, 480x272 XRGB8888 handed to the frontend | **No** |
| **Track C** (second half) | The same core on the Vulkan hardware renderer | PPSSPP's `DrawEngineVulkan` against the linked libps5vk | Yes: the driver subset in section 7 |

Track A is a prefix, not a detour. It proves the loader contract, the platform
adaptation, the build pipeline, the memory policy, audio, input and content
handling, and it produces a known-good PPSSPP to diff the Vulkan renderer
against. Its only throwaway decision is the first memory policy (section 5),
which is upgraded inside Track A anyway.

Do not describe a software-rendered run as hardware rendering. The plan's own
three-achievement rule applies: core/platform bring-up, software-rendered PPSSPP,
and PPSSPP Vulkan hardware rendering are separate claims with separate evidence.

### Why this order is available at all

`LibretroGraphicsContext::CreateGraphicsContext()` (`libretro/LibretroGraphicsContext.cpp`)
tries GL, GLES and Vulkan in turn and ends unconditionally at
`LibretroSoftwareContext`. The core option `ppsspp_backend = "none"` maps to
`RETRO_HW_CONTEXT_NONE` (`libretro/libretro.cpp`), which matches none of those
branches, so PPSSPP never calls `RETRO_ENVIRONMENT_SET_HW_RENDER` and never
creates a Vulkan device. `GetGPUCore()` then returns `GPUCORE_SOFTWARE` and
`retro_load_game` wires that into the emulator.

The frontend needs no change: the software frames arrive through the same
`video_cb` XRGB8888 path that the five shipped cores already prove, with
`video_driver = "vulkan"` presenting them. The one runtime consequence is a fixed
480x272 output (`NATIVEWIDTH`/`NATIVEHEIGHT`, `SOFT_BMP_SIZE` in
`libretro/LibretroGraphicsContext.h`), which is already a multiple of the
frontend's 256-byte row padding, so no padded-width work is involved.

## 2. Decisions already made, with their evidence

- **Pin `hrydgard/ppsspp` at `f293b10fb2d9dc0c2bc10281444ee3d3e932e6ad`.** It is
  the revision `PPSSPP_Core_Plan.md` and `handoff/PPSSPP_UPSTREAM_LIBRETRO_RESEARCH.md`
  were written against, so every finding in them is true of the pinned tree. It is
  an assessment reference, not an approved release: M0 of the core plan still has
  to confirm the final pin against the owner's older PS5 port when that source is
  located.
- **`thread_local` needs no patch.** The SDK toolchain compiles with
  `-femulated-tls`, so a `thread_local` variable becomes a `__emutls_v.*` object
  with no `.tdata`/`.tbss` and no `PT_TLS` segment. Verified by compiling and
  linking a TLS probe with `prospero-clang++` and `tooling/native/ps5-core.ld`:
  zero PT_TLS, zero TLS sections, `NEEDED` exactly `libkernel_web.sprx`,
  `libSceLibcInternal.sprx`, `libScePosixForWebKit.sprx`, and one undefined
  import, `__emutls_get_address`.
  **Consequence to protect:** the title must keep resolving `__emutls_get_address`
  at link time. It comes from clang's `libclang_rt.builtins-x86_64.a`, which
  `tools/build.sh` links only inside its `APP_VULKAN_ARCHIVES` branch. If a future
  change drops the Vulkan archives from the link, every core with a `thread_local`
  stops linking.
- **One core binary, two runtime configurations.** The backend is chosen per
  content load from a core option (`retro_check_backend()` in `libretro.cpp`), so
  the software and Vulkan paths are the same `.so`. There is no fork to maintain
  and no separate build.
- **The frontend does not change for Track A.** No new patch in `patches/series`,
  no change to `core_names` until the ABI gate passes, and `video_driver` stays
  `"vulkan"`.

## 3. Files this plan adds

| Path | Purpose |
| --- | --- |
| `tools/build-ppsspp.sh` | the reproducible build: pinned fetch, zero-fuzz patches, cross configure, build, ABI check, staging, `build.json` |
| `tooling/ppsspp/ps5-toolchain.cmake` | FreeBSD/x86-64 cross toolchain on `prospero-*`, no host library search |
| `tooling/ppsspp/CMakeLists.txt` | wrapper project: adds the pinned tree, forces the libretro target and the PS5 feature set |
| `patches/ppsspp/*.patch` | named source changes, one concern per file, applied with `--fuzz=0` |
| `build/cores/ppsspp/` | the fetched tree, its build directory and `abi.json`/`build.json` (ignored) |
| `build/cores/stage/{cores,info}/ppsspp_libretro.{so,info}` | the staging convention every other core uses |
| `evidence/ppsspp-native/` | committed sanitized evidence once console runs exist |

## 4. Step ladder

Each step states its acceptance before it is built, and one step is one commit.

### A1 — the platform branch

PPSSPP has no platform for this ABI: `ppsspp_config.h` is an `#elif` chain over
Windows, Apple, Switch, Android, Linux and OpenBSD with no `#else` and no
`#error`. Compiled as-is, no `PPSSPP_PLATFORM_*` macro is defined and the tree
silently takes generic paths — `MemoryMap_Setup` logs "Hit a wrong path" and
returns false, and `Common/CPUDetect.cpp` leaves its core count uninitialised.

Add the branch on the macro the toolchain actually defines (`__PROSPERO__`), then
fix what the compiler reports. The census is small: `PPSSPP_PLATFORM(LINUX)`
appears in only nine files and `PPSSPP_PLATFORM(OPENBSD)` in one, so PS5 is a
genuinely new platform rather than a Linux alias. Where a BSD-shaped path is
right, follow the OpenBSD precedent.

- Acceptance: the pinned tree configures and compiles past every
  `PPSSPP_PLATFORM`-guarded file with no undefined platform macro; the diff to
  upstream is one named patch file.

### A2 — the cross build

`tools/build-ppsspp.sh` follows `tools/build-mgba.sh`: pinned revision, fresh
extraction, patches with zero fuzz, `prospero-*` compilers named explicitly, and
a local destructor registry object compiled with `-fno-exceptions -fno-rtti`
(`tooling/native/core_cxx_runtime.cpp`). PPSSPP needs git submodules, so the
fetch is a pinned `git` clone plus `git submodule update --init --recursive`
rather than a codeload tarball; the script records the commit and every submodule
SHA in `build/cores/ppsspp/build.json` so the input set is identified even though
it is not a single digest.

The wrapper project must keep host discovery out of the build: `find_package`
restricted to the SDK root, `OPENGL_LIBRARIES` preset empty so
`find_package(OpenGL REQUIRED)` is never reached, `X11_LIBRARIES` empty, optional
dependencies (FFmpeg, Discord, miniUPnPc, SDL) off, and `VULKAN` left on since the
Vulkan path is compiled regardless. Linking matches every other core:
`-nostdlib -nodefaultlibs -Wl,-z,undefs -Wl,-T,tooling/native/ps5-core.ld` with
`-lkernel_web -lSceLibcInternal -lScePosixForWebKit`.

- Acceptance: `tools/check-core.py build/cores/ppsspp/ppsspp_libretro.so` passes —
  25 libretro exports, `libkernel_web.sprx` present, imports limited to the three
  allowed modules, 16 KiB load segments, no writable+executable segment, and no
  `PT_TLS`. `readelf -lW` and the ABI report are the evidence.

### A3 — the import surface

`tools/core-imports.py` derives the title's native binding table from the staged
cores' undefined symbols. Adding PPSSPP grows that table, and every new symbol
must be exported by a stub of a module the title already links, or the title link
fails with `no public SDK stub exports required symbol <name>`
(`tooling/native/sce_module_writer.cpp`).

Expected new imports, all previously confirmed present in `libkernel_web.so` or
`libSceLibcInternal.so`: `mprotect`, `pthread_*`, `sigaction`, `sigaltstack`,
`shm_open`, `__cxa_*`, `_Unwind_*`, `__emutls_get_address`.

- Acceptance: the title links with PPSSPP in `core_names`, the five existing cores
  still pass `check-core.py`, and the added symbol list is recorded as evidence.
  The staged core is **not** added to `core_names` before this point.

### A4 — first load on the console

Deploy the staged core beside the five shipped ones and load it with
`ppsspp_backend = "none"`. This is the first proof that the loader accepts a
20 MB C++ core with hundreds of thousands of dynamic symbols and that the title
stays alive when it does.

- Acceptance: the loader log shows the module loaded, initializers ran and the
  core reported its identity; the frontend remains navigable; `klog` captured.
- Failure handling: the loader can refuse a malformed or unsupported core, but it
  cannot isolate a fault inside core code. A crash is evidence to preserve, not a
  reason to relaunch.

### A5 — a game boots

Assets first: PPSSPP needs `system/PPSSPP/` (its own asset files, the `lang`
folder and `flash0` fonts) and `ppge_atlas.zim`; the firmware-derived parts come
from the owner and are never committed. Content starts with a redistributable PSP
homebrew, then an owner-provided game.

- Acceptance: a PSP title reaches gameplay with the software GPU core, audio and
  input working, and the run captured. Frame timing is recorded but not yet a
  gate; the software rasterizer is expected to be slow on 3D content.

### A6 — lifecycle and the memory policy

Repeated load/unload, content change, menu open/close, and a second game. Then
the memory upgrade (section 5). The `NO_MMAP` bring-up policy caps guest RAM near
32 MiB, so a 64 MiB title is the natural forcing case for the arena work.

- Acceptance: bounded memory growth across cycles, no leak visible in the
  title's allocation diagnostics, and a 64 MiB title running on the upgraded
  policy.

### A7 — Track A acceptance

The plan's M2 milestone, reached with the software renderer: bounded CPU
workloads, repeated reset/load/unload with stable accounting, the active CPU mode
proven by logs and execution evidence rather than by the saved option string, and
one deliberate interpreter run as a comparison.

## 5. Memory policy, in two steps

The console's numbers are fixed: a 448 MiB flexible pool that **every** anonymous
mapping draws on, including `PROT_NONE` reservations; a 12,288 MiB direct pool
reachable only through the `sceKernel*` APIs; a 16,384-entry page-table
allowance; execute granted by `mprotect` and never at mapping time; and `mprotect`
rounding a non-aligned address instead of refusing it.

PPSSPP's normal POSIX path cannot work in that environment.
`MemArena::Find4GBBase()` needs an 8 GiB `PROT_NONE` anonymous mapping to succeed
before it returns a 4 GiB-aligned base, and on failure it returns the hardcoded
`0x2300000000`; `MemArena::CreateView` then maps each guest view with `MAP_FIXED`
at `base + virtual_address`, which silently replaces whatever occupies that
address. The 8 GiB probe cannot succeed inside a 448 MiB pool, so the dangerous
fallback is the *expected* path, not an edge case.

**Step one, bring-up: `USE_NO_MMAP`.** CMake adds `-DNO_MMAP -DMASKED_PSP_MEMORY`;
`CreateView` returns the pointer without mapping anything, `Find4GBBase` becomes a
single 160 MiB allocation, and guest pointers are `base + (addr & 0x3FFFFFFF)`.
No shm object, no `MAP_FIXED`, no multi-GiB reservation. The guest RAM ceiling
this implies is a known limitation, not a surprise: it is the reason step two
exists.

**Step two, correctness: a PS5 arena adapter.** A small platform memory file that
reserves address space with `sceKernelReserveVirtualRange`, backs guest RAM,
VRAM and the JIT cache with `sceKernelAllocateDirectMemory` +
`sceKernelMapDirectMemory`, obtains execute with `sceKernelMprotect`, tracks each
region's reservation base, mapped base, direct offset, protection and lifetime,
and unwinds every failed stage. The exact prototypes are not in the SDK headers —
they are exported by the kernel stubs but undeclared — so they come from the
probe code in the sibling Eden repository, which already exercised them on this
console.

Both steps are backend-independent: they apply unchanged when the Vulkan renderer
is switched on.

## 6. The JIT and executable memory

The MIPS recompilers allocate a 16 MiB code space (`AllocCodeSpace(1024*1024*16)`
in each backend); `GetJitCacheSize` no longer exists upstream, so that constant is
the whole code-cache budget. `AllocateExecutableMemory` asks for
`PROT_READ|PROT_WRITE|PROT_EXEC` unless `PlatformIsWXExclusive()` is true, and a
direct RWX mapping is exactly the case this console refuses to execute.

So the PS5 platform must report `PlatformIsWXExclusive() == true`, which routes
allocation through RW and adds execute later with `mprotect`, with
`Common/CodeBlock.h` bracketing writes with protect/unprotect. The platform branch
in A1 owns that. The cost is measured and known: 108 microseconds per
map/write/RX/call/munmap cycle on this console against 3.5 on the development
host, so code-cache churn is expensive and worth keeping out of steady state.

PPSSPP's fast-memory handling installs a `SIGSEGV` handler with `SA_SIGINFO` and
`SA_ONSTACK`, reads the machine context and rewrites the faulting program counter
(`Common/ExceptionHandlerSetup.cpp`, `Core/MemFault.cpp`). The SDK exports
`sigaction`, `sigaltstack`, `getcontext`, `makecontext` and `swapcontext`, so the
primitives exist; the loader must not be covering those signals, and the SDK's
machine-context layout is the thing to verify with a focused probe before fast
memory is trusted.

## 7. The upgrade path to the Vulkan renderer

Nothing in Track A is discarded except the memory policy's first step. Switching
on hardware rendering means:

1. Set `ppsspp_backend = "vulkan"` (and keep `video_driver = "vulkan"`).
2. Land the driver subset PPSSPP actually requires, in this order: attachments at
   arbitrary extents; a combined depth/stencil format; cull mode and front face;
   triangle strips and fans; then colour write mask, stencil state, dynamic blend
   constants and meaningful pipeline barriers.
3. Prove the frontend's `retro_hw_render_interface_vulkan` bridge with a minimal
   hardware-core canary before PPSSPP is the thing being debugged, because
   PPSSPP's device creation asserts when the driver has no combined depth/stencil
   format and the bridge has otherwise never been exercised.
4. Keep `ppsspp_backend = "none"` as the documented recovery configuration. A
   driver regression then costs a core option, not a rebuild.

The driver details, including what **not** to build because PPSSPP never asks for
it — descriptor sets beyond zero, geometry and tessellation shaders, dynamic
rendering, logic operations, depth clamp, dual-source blend — are in section 9 of
`PPSSPP_Core_Plan.md` as amended by this plan's findings. The honest rule for
both sides: advertise a capability only when its semantics exist, because PPSSPP
takes the fast path whenever the feature bit is set and a silently dropped
feature is a correctness bug, not a fallback.

## 8. Evidence and gates

- `tools/verify.sh` gains nothing new. The PPSSPP build becomes part of the
  `build` gate only when step A3 lands, by adding `ppsspp` to `core_names` in
  `tools/build-title.sh`; before that the build script is run directly and its
  ABI report is the evidence.
- Every step's evidence is an artifact: the ABI report for A2, the title link and
  the import delta for A3, loader and frontend logs for A4, gameplay capture for
  A5, allocation accounting for A6.
- Raw captures stay in the ignored `klog/`; sanitized machine-readable results go
  to `evidence/ppsspp-native/`. Owner content, ROM filenames, console addresses
  and firmware-derived assets never enter the repository.
- The five shipped cores are regression-tested whenever the import surface, the
  loader or shared upload code changes. A PPSSPP step that breaks one of them is
  not landed.

## 9. Open items to close before or during A1-A3

| Item | Why it matters | Where it closes |
| --- | --- | --- |
| Final upstream pin | the assessment pin is not an approved release, and the owner's older PS5 port may carry reusable adaptations | before the first public build |
| The owner's older JIT port | may already solve arena, ABI and timing problems; must be located or declared lost | A1 |
| PSP content and `system/PPSSPP` assets | acceptance impossible without them; firmware-derived files come from the owner | A5 |
| SDK machine-context layout | fast memory rewrites the faulting PC from `ucontext_t` | A4, by probe |
| `__emutls_get_address` retention | every core with a `thread_local` depends on the title's clang builtins archive | A3, by the title link |

## 10. Non-goals

- Building or vendoring PSP firmware, games or PPSSPP's firmware-derived assets.
- A second graphics backend, a private GPU path, or driver code in this
  repository: the driver lives in PS5_Vulkan and is consumed as released
  archives.
- Claiming hardware rendering, full-speed gameplay or general PSP compatibility
  from a software-rendered run, a boot screen or a single title.
- Changing the shipped default away from the five accepted cores until PPSSPP
  passes its own acceptance steps.
