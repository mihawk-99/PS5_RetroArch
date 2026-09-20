# PPSSPP native Vulkan core: technical implementation and handoff plan

## Table of contents

1. [Decision and completion objective](#1-decision-and-completion-objective)
2. [Scope, permissions and branch handoff](#2-scope-permissions-and-branch-handoff)
3. [Source identities and evidence discipline](#3-source-identities-and-evidence-discipline)
4. [Existing RetroArch foundation](#4-existing-retroarch-foundation)
5. [Platform capability matrix](#5-platform-capability-matrix)
6. [CPU, JIT and memory integration](#6-cpu-jit-and-memory-integration)
7. [Native core build and loader contract](#7-native-core-build-and-loader-contract)
8. [Libretro Vulkan interface and ownership](#8-libretro-vulkan-interface-and-ownership)
9. [Confirmed current driver blockers](#9-confirmed-current-driver-blockers)
10. [Additional graphics risks requiring investigation](#10-additional-graphics-risks-requiring-investigation)
11. [Driver artifact selection and concurrent development](#11-driver-artifact-selection-and-concurrent-development)
12. [Implementation milestones and acceptance gates](#12-implementation-milestones-and-acceptance-gates)
13. [Diagnostics, measurements and failure recovery](#13-diagnostics-measurements-and-failure-recovery)
14. [Console test matrix](#14-console-test-matrix)
15. [Files, commands and proposed deliverables](#15-files-commands-and-proposed-deliverables)
16. [Open questions and first-session checklist](#16-open-questions-and-first-session-checklist)
17. [References and limits of this assessment](#17-references-and-limits-of-this-assessment)

## 1. Decision and completion objective

**The environment is promising enough to start PPSSPP integration. The currently
inspected PS5_Vulkan implementation cannot run the normal upstream PPSSPP Vulkan
renderer unchanged.** There are known rendering requirements that it explicitly
refuses, including a combined depth/stencil attachment needed during device
initialization. This is more specific than saying the workload is untested.

The user requests a native PPSSPP libretro core using the existing PS5 RetroArch
pipeline and their Vulkan driver. This document is a technical handoff, not a
claim that a port has been built or tested. No PPSSPP build, console launch or
driver modification was performed while preparing it.

The implementation objective is:

> A reproducibly built, ABI-checked native `ppsspp_libretro.so` with matching
> metadata and required assets, loaded by the existing native core loader, using
> verified x86-64 JIT execution and PPSSPP's Vulkan hardware renderer against the
> exact statically linked PS5_Vulkan artifact identified in the report; selected
> PSP workloads render correct gameplay with working audio/input, no unexpected
> GPU refusals or command-buffer failures, and return to XMB and another core
> without corruption, deadlock or a crash. The console owner confirms the result.

A bounded initial gameplay milestone is acceptable; document the exact tested
content and unsupported cases. Do not label the core generally compatible or
full-speed from a boot screen or one simple workload.

Distinguish three achievements in all reports:

- **Core/platform bring-up:** native loading, dependencies, memory, CPU execution.
- **Software-rendered PPSSPP with Vulkan presentation:** potentially useful as an
  explicitly labelled diagnostic, but does not satisfy hardware-rendered PSP.
- **PPSSPP Vulkan hardware rendering:** GPU commands originate in PPSSPP's renderer
  and produce PSP graphics; RetroArch presents/composites the resulting image.

Finishing an entire Vulkan 1.0 milestone is not a necessary scheduling dependency
if the PPSSPP-specific subset can be implemented and verified sooner. Conversely,
a command-count audit or a milestone label is not proof of PPSSPP compatibility.
Vulkan 1.3 is not a prerequisite established by this investigation.

## 2. Scope, permissions and branch handoff

Read `AGENTS.md`, `docs/PLAN.md`, then `docs/ACTIVE.md` in this repository before
implementation. The active file's previous thumbnail/input work is context, not
an instruction to resume that unrelated work. This handoff concerns PPSSPP.

The next implementation agent should create a RetroArch branch **before code
changes**, preferably `codex/ppsspp-native-vulkan`. Inspect the working tree and
existing branches first; do not overwrite changes, reset history or assume the
branch does not exist. Example, only after inspection:

```bash
git status --short --branch
git branch --list codex/ppsspp-native-vulkan
git switch -c codex/ppsspp-native-vulkan
```

The planning task itself does not create that implementation branch. Preserve
this document if it is still uncommitted when switching branches.

The user previously authorized driver fixes provided PS5_Vulkan documentation
is updated and changes are committed in that repository. They also explicitly
warned that they are actively modifying PS5_Vulkan. Therefore:

- Inspect its working tree before any driver work; coordinate overlapping files.
- Do not absorb the user's unrelated modifications into an agent commit.
- Prefer an isolated driver worktree when implementing an independent fix, after
  deciding which source state must be included. An ordinary worktree from HEAD
  does **not** contain the user's uncommitted changes.
- Driver changes and their evidence belong in PS5_Vulkan; frontend/core changes
  belong here. Track the dependency by exact commit and artifact hashes.
- Never silently replace the known-good frontend driver snapshot with whichever
  archive happens to be newest.
- Do not push unless the user asks.

Current deployment policy: uploads are authorized, but check that the console is
idle and preserve user data. A new launch requires the user's intervention or a
new explicit authorization. Historical permissions for earlier tests are not an
instruction to launch now. Never terminate another title. Start capture before
an authorized launch and preserve logs before restarting after a failure.

Keep XMB as the default, RGUI selectable, and `video_ps5` registered/selectable as
the CPU fallback. Do not claim a completed Vulkan step by silently selecting a
software renderer or suppressing a driver refusal. Preserve the existing logging
policy: useful logs, no periodic synchronous logging that reintroduces hitches.

## 3. Source identities and evidence discipline

Assessment snapshot, recorded during the September 2026 handoff:

| Component | Inspected identity |
| --- | --- |
| PS5_RetroArch HEAD | `b1dced92108a70ff56c20d0e97f5cfe87797901a` |
| PS5_Vulkan HEAD | `9d3a025f68ad8c0b608b6814bec844aa252f6710` |
| Driver working tree | Modified `driver/ps5vk_image.c`, `src/diagnostics.cpp`, `tools/check-mip-layout.sh`; untracked `jobs/v0-wide2/` |
| Upstream PPSSPP source reviewed | `f293b10fb2d9dc0c2bc10281444ee3d3e932e6ad` in `hrydgard/ppsspp` |
| Local temporary upstream excerpts | `/tmp/ppsspp-current-review/`, with `REVISION`; disposable, not a build input or committed evidence |

The PPSSPP revision above is an **assessment reference**, not an approved release
pin. Confirm which upstream repository/revision will be used for the actual
libretro build. Compare it with the user's older PS5 PPSSPP port before choosing.
Pin sources, submodules, metadata and assets reproducibly; record digests.

Driver restrictions below are working-tree source observations. Recheck them
because development is concurrent. Local source presence, archive contents and
console execution are three different facts. Do not substitute one for another.

Use these evidence classifications:

- **Measured:** named console probe or accepted run, with its scope and artifact.
- **Source-confirmed:** explicit code path or rejection in the inspected revision.
- **Inference:** likely integration consequence that still needs a reproducer.
- **Unverified:** no relevant workload result exists yet.

The Eden probe documents contain earlier failed approaches followed by newer
successful direct-memory results. Read the dated amendments and current evidence;
do not repeat the old anonymous-memory ceiling as the total available RAM.
Some project plan text also predates the accepted Vulkan frontend. Consult current
source and evidence rather than turning stale prose into a new implementation.

## 4. Existing RetroArch foundation

Already accepted by the console owner, within the tested workloads:

- Native frontend title, XMB and selectable RGUI.
- RetroArch Vulkan presentation through statically linked PS5_Vulkan.
- Native stereo audio, controller input, analog menu navigation and remapping.
- Filesystem browsing, configuration loading/saving and writable title folders.
- Native dynamic core loading, metadata discovery and failed-load recovery.
- FCEUmm, mGBA, Snes9x, FBNeo and Genesis Plus GX gameplay in recorded tests.
- Corrected software-frame colour handling and clean close-content transitions.
- Fixed XMB large-list allocation handling and removed periodic logging hitches.
- Latest accepted thumbnail RGBA handling and controller recovery after defaults.

This establishes a reusable frontend, **not** hardware-core Vulkan compatibility.
All currently accepted emulator cores produce software-rendered frames.

Relevant implementation files:

| File | Purpose |
| --- | --- |
| `tools/build-snes9x.sh` | C++ native core build example, source/info pinning, local destructor registry, ABI report |
| `tools/build-mgba.sh`, `tools/build-fbneo.sh`, `tools/build-genesis-plus-gx.sh` | Other native port/build precedents |
| `tools/build-title.sh` | Builds current cores, generates import surface, links driver archives and stages title |
| `tools/check-core.py` | ELF ABI, imports, exports and segment checks |
| `tools/core-imports.py` | Core import aggregation for the title |
| `src/core_loader_ps5.cpp` | Native shared-object mapping, relocation, constructors, teardown |
| `src/core_imports_ps5.cpp` | Runtime import resolution surface |
| `src/core_frame_ps5.cpp` | Existing software core-frame integration; inspect before changing format handling |
| `tooling/native/ps5-core.ld` | Native core linker layout |
| `tooling/native/core_cxx_runtime.cpp` | Core-local destructor handling |
| `tooling/native/app-symbols.map` | Application symbol export policy |
| `patches/series` | Ordered RetroArch frontend patches |

Do not blindly carry existing XRGB8888 software-output conversion patches into
PPSSPP's hardware-image path. Native Vulkan images have their own format, layout
and synchronization contract.

## 5. Platform capability matrix

Evidence resides in the sibling Eden repository under `evidence/` and is indexed
by `docs/PROBING.md`, `docs/CAPABILITY_PROBE.md` and `docs/ACTIVE.md` there.

| Capability | Observed result | Consequence / limit |
| --- | --- | --- |
| Host CPU ABI | Native x86-64 code runs | Suitable architecture for PPSSPP's AMD64 recompiler; no speed guarantee |
| Page size | 16,384 bytes | Use actual page size for allocation/protection boundaries |
| Anonymous RW then RX | Generated function executes | W^X-style JIT path exists |
| Anonymous RW then `mprotect` RWX | Generated function executes | Writable/executable mapping can be obtained through protection change |
| Direct `mmap(RWX)` | Mapping returned, execution faults | Success return does not prove executable permissions |
| JIT patch cycles | 64 protection/write/execute cycles pass | Basic rewriting works; not a PPSSPP self-modifying-code test |
| Executable region | 16 MiB, 1,024 generated functions pass | More than a one-instruction-page demonstration |
| Multithreaded generated code | Four worker threads pass | Basic concurrent execution works; not proof of PPSSPP cache synchronization |
| Signals | SIGSEGV/SIGBUS, alternate stack, `si_addr`, `siglongjmp` exercised | Fault delivery/recovery primitives work; instruction-context backpatching remains untested |
| Flexible memory | 448 MiB configured, 442 MiB available in one budget run | Ordinary anonymous/heap allocations remain constrained |
| Anonymous allocation walk | About 432 MiB before ENOMEM in earlier runs | Process footprint and allocation route matter |
| Direct-memory pool | 12,288 MiB reported | Separate pool accessible through native APIs |
| Direct-memory allocation | Up to 4 GiB mapped, written, released | Large backing allocations possible |
| Simultaneous layout | 4 GiB guest + 512 MiB code + 1 GiB table = 5,632 MiB | Large multi-region allocation and executable cache coexist in that probe |
| Executable direct memory | Map as buffer, then `sceKernelMprotect(0x07)` works | Do not pass execute to initial direct mapping and assume equivalent behavior |
| Direct mapping with execute | Refused with `0x80020016` in layout probe | Allocation/protection order is material |
| Virtual reservation | `sceKernelReserveVirtualRange`: 4 GiB succeeds; 64 GiB fails `0x8002000C` | Intermediate sizes/alignment guarantees, including PPSSPP's 8 GiB request, not established |
| Page-table accounting | Allowance of 16,384 entries reported | Respect accounting; do not equate it uncritically with one generic CPU PTE per page |

The layout result does not prove every byte was stress-tested for hours, that the
entire reported pool is always available, or that graphics/CPU contention is
acceptable. Consult the actual probe implementation before making stronger claims.

The user reports an older PS5 RetroArch+PPSSPP build with working JIT but no GPU
support. Its source/build artifacts were not located or reviewed during this
assessment. Obtain or locate them: they may already solve critical allocator,
ABI, timing, thread or emulator-memory problems. Do not assume its SDK or runtime
objects can be linked unchanged into this project.

## 6. CPU, JIT and memory integration

### 6.1 Upstream entry points to audit

At the reviewed revision:

- `Common/MemoryUtil.cpp`: `AllocateExecutableMemory`, `AllocateMemoryPages`,
  `ProtectMemoryPages`, `FreeExecutableMemory`; page rounding and platform policy.
- `Common/MemArenaPosix.cpp`: backing allocation, `CreateView`, `ReleaseView`,
  `Find4GBBase`; `shm_open`, `ftruncate`, `mmap`, `MAP_FIXED`, shared views.
- `Core/MemMap.cpp`: guest memory views and arena initialization.
- The selected AMD64 JIT and fault-handler code: follow actual callers of the
  functions above in the chosen revision, including code-cache resets/patches.

**Confirmed mismatch:** the normal POSIX executable allocator starts with
`PROT_READ | PROT_WRITE | PROT_EXEC`, unless `PlatformIsWXExclusive()` changes that
policy. Direct RWX mapping is specifically the console path that failed to
execute. Audit the complete allocate/write/protect/execute lifetime; simply
changing one platform boolean is not sufficient without proving all callers.

**Confirmed address-space risk:** `MemArena::Find4GBBase()` on the ordinary 64-bit
POSIX path requests an **8 GiB PROT_NONE anonymous mapping** to find a 4 GiB-aligned
base. If it fails, the inspected source falls back to `0x2300000000`. Anonymous
PROT_NONE reservations consume the constrained pool in the probes. Do not permit
that hardcoded-address fallback to overwrite unknown mappings in the title.

### 6.2 Proposed PS5 memory adapter

Keep platform-specific allocation/protection code small and testable. Reuse the
older working port where compatible; otherwise adapt the appropriate upstream
memory abstraction rather than globally intercepting all `mmap` behavior.

Native APIs worth evaluating, using the installed SDK declarations and Eden
probe calls as the source of exact signatures/flags:

- `sceKernelGetDirectMemorySize`
- `sceKernelAllocateDirectMemory`
- `sceKernelMapDirectMemory`
- `sceKernelMprotect`
- `sceKernelReleaseDirectMemory`
- `sceKernelMunmap` or the correctly matched SDK unmap interface
- `sceKernelReserveVirtualRange`
- `sceKernelVirtualQuery`
- `sceKernelGetPageTableStats`
- `sceKernelConfiguredFlexibleMemorySize`
- `sceKernelAvailableFlexibleMemorySize`
- POSIX `mmap`, `mprotect`, `munmap`, `sysconf`/`getpagesize` where proven appropriate

Names above are an investigation checklist, not a ready-to-compile signature list.
Confirm import availability in this title's SDK and loader. Do not copy Eden's
numeric protection values without preserving their allocation context. The
observed buffer mapping uses `0x33`; adding CPU RWX uses `0x07` through the native
protection call. Derive production constants from the actual headers/probes.

Track each owned region's reservation base/size, mapped base/size, direct-memory
offset/size, current protection, purpose and lifetime. Check size addition and
alignment overflow. Ensure every failed stage unwinds earlier successful stages
and that load/unload cycles return accounting to a stable baseline.

Plan memory by purpose:

| Allocation class | Initial approach | Verification |
| --- | --- | --- |
| Small host objects / runtime stacks | Existing native runtime | Aggregate frontend+core flexible-memory peak |
| Guest RAM / aliases | PPSSPP arena adapter, direct backing if required | Address constraints, alias coherence, release behavior |
| JIT cache | Explicit supported protection sequence | Generated calls, patch cycles, near-call reachability, protection error paths |
| Large CPU caches | Direct memory when justified by measurements | Alignment, ownership and API compatibility |
| Vulkan resources | Existing driver allocation APIs | Do not bypass Vulkan object ownership or confuse CPU direct memory with valid GPU resources |

PSP physical RAM is much smaller than Eden's guest RAM, but PPSSPP's **host
virtual address layout** is a separate constraint. Verify aliases required by
`Core/MemMap.cpp`; several distinct allocations containing copied data are not
substitutes for views of the same backing. An 8 GiB reservation is not 8 GiB of
PSP RAM. Investigate aligned native reservation or a supported alternate arena
path; do not allocate multi-GiB physical memory merely to satisfy an address hint.

The reviewed `NO_MMAP` path uses a different allocation strategy, but enabling it
is only a candidate: validate pointer translation, JIT assumptions, mirrors,
performance and cleanup before selecting it. Do not assume it is a free fallback.

### 6.3 JIT correctness and threading

- Use 16 KiB page boundaries, with explicit checked rounding. Avoid changing the
  permissions of unrelated allocations sharing a page.
- x86-64 generally has coherent instruction/data caches, but code patching still
  needs correct publication and synchronization. Keep upstream synchronization;
  never race a running thread against patching/freeing its instructions.
- Check relative branch/displacement range assumptions between generated code,
  dispatch stubs, helper functions and mapped core text. Allocation success alone
  does not validate a JIT's placement requirements.
- Verify POSIX threading, condition variables, mutexes, thread-local state,
  atomics and the monotonic/high-resolution timer actually used by the core.
- The probe's signal handler resumes through `siglongjmp`; it does not establish
  that PPSSPP's fault handler can inspect/update every required machine-context
  field for fast-memory backpatching. Treat that as a separate integration gate.
- Do not install a broad signal handler that hides native loader/driver faults.
  Define handler chaining/restoration and which address ranges the core owns.
- Exercise repeated cache invalidation, reset, content unload and a second game.
  Threads must stop before executable mappings or core text are unmapped.

CPU-only or software-renderer diagnostics may isolate these problems, but report
them as intermediate results. Preserve the accepted frontend GPU path.

## 7. Native core build and loader contract

### 7.1 Reproducible build shape

Proposed deliverables are `tools/build-ppsspp.sh`, `patches/ppsspp/` and narrowly
scoped platform support under `tooling/ppsspp/` if needed. Follow existing scripts'
logic, not another PS5 project's binary pipeline or flags copied wholesale.

Use `.deps/native/ps5-payload-sdk/bin/prospero-*` wrappers explicitly for C, C++,
archive creation and linking. Existing scripts set `PS5_PAYLOAD_SDK`, `PS5_CLANG`,
`CC`, `CXX`, `AR` and `LD`, then repeat compiler selections in upstream build
arguments to prevent a hardcoded host compiler from winning.

Audit the selected PPSSPP build system for a libretro shared target and its
platform probes. Cross-compilation must not execute PS5 test binaries on Linux.
Separate any host code-generation tools from target libraries. Record versions,
source/submodule pins and generated inputs. Avoid network fetches hidden in build
steps after the dependency set has been established.

Inventory the actual enabled dependency graph: C++ runtime, compression/archive
libraries, image/audio codecs, glslang/SPIR-V components, Vulkan headers and any
thread/atomic libraries. Do not assume the desktop executable's entire dependency
set is needed, or that headers-only Vulkan dispatch removes shader compilation
requirements. PPSSPP's reviewed libretro Vulkan callback explicitly initializes
glslang. All target dependencies must be built for this SDK/ABI.

### 7.2 ELF/loader restrictions already in force

`tools/check-core.py` checks:

- ELF64, little-endian, x86-64 ET_DYN, FreeBSD/PS5 ABI identification.
- The required libretro API exports in the dynamic symbol table, including
  initialization, content lifecycle, callbacks, serialization and memory APIs.
- `libkernel_web.sprx` required; allowed dependencies currently limited to it,
  `libSceLibcInternal.sprx` and `libScePosixForWebKit.sprx`.
- No `libkernel_sys.sprx`, host Linux libc/libstdc++ or accidental host objects.
- 16 KiB-compatible load segments; no writable+executable ELF load segment.

The last restriction concerns the **ELF image**, not an intentionally allocated
runtime JIT cache. Do not weaken ELF validation to enable runtime JIT.

`src/core_loader_ps5.cpp` currently rejects PT_TLS and PT_INTERP; supports a
limited relocation set including `R_X86_64_NONE`, `RELATIVE`, `64`, `GLOB_DAT` and
`JUMP_SLOT`; supports init/fini arrays but rejects legacy initialization paths,
text relocations and unsupported preinitialization. General C++ unwind
registration is absent. Recheck exact implementation before changing it.

Therefore PPSSPP-specific load risks include:

- TLS emitted by PPSSPP, shader compiler libraries or the C++ runtime.
- Exceptions/unwinding, RTTI assumptions and thread-local destructors.
- Unsupported relocation forms, weak imports and symbol visibility.
- Static constructors/destructors that require unavailable imports or outlive
  the core after unload.
- Duplicate runtimes or globally interposed C++/Vulkan symbols.

Existing Snes9x builds a core-local destructor registry and uses
`-fno-exceptions -fno-rtti` for that support object. This is a precedent, **not**
proof PPSSPP and all dependencies can globally disable either feature. Inspect
uses first. If loader features are added, write focused malformed-ELF and
lifetime tests; do not turn rejection into silent acceptance.

An ABI-check pass does not resolve every symbol or prove the dynamic loader
supports every section. Inspect `readelf -lW`, `-dW`, `-rW`, `--dyn-syms -W`,
undefined-symbol reports and actual load results.

### 7.3 Packaging, metadata and assets

Proposed core staging matches existing conventions:

```text
build/cores/stage/cores/ppsspp_libretro.so
build/cores/stage/info/ppsspp_libretro.info
dist/PPSA99169/cores/ppsspp_libretro.so
dist/PPSA99169/info/ppsspp_libretro.info
```

Current title staging additionally places a metadata copy beside each core for
older configurations with an empty info-directory setting. Preserve that behavior
when adding PPSSPP to `core_names`, import generation and runtime identity inputs.
Do not add the unfinished core unconditionally to the shipped build before its
build/load gate is ready.

Pin matching metadata from libretro-core-info. Validate display name, supported
extensions and archive handling against the chosen core; do not copy another
core's `.info` or treat discovery as successful loading. PPSSPP typically needs a
support asset tree under the system directory (commonly `system/PPSSPP/`);
verify exact path, capitalization, contents and licenses for the selected revision.
A PSP BIOS dump is not a substitute for that support tree.

In-title `/app0` corresponds to FTP `/data/homebrew/PPSA99169/`. Preserve content,
configuration, saves and existing assets during deployment. Start with a known
supported uncompressed content format for isolation, then test archive handling
explicitly; never dismiss an archive regression by silently changing the user's
content workflow. Keep copyrighted games and private paths out of committed logs.

## 8. Libretro Vulkan interface and ownership

### 8.1 Reviewed upstream flow

`libretro/LibretroGraphicsContext.cpp` chooses a `LibretroVulkanContext` when
requested and negotiates `RETRO_ENVIRONMENT_SET_HW_RENDER`.

`libretro/LibretroVulkanContext.cpp` at the reviewed revision:

1. Advertises `RETRO_HW_CONTEXT_VULKAN` (the constructor names Vulkan 1.0.18).
2. Provides `RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE` with
   `RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN` and its interface version.
3. In `create_device`, calls `VulkanLoadFromGetInstanceProcAddr` with the frontend's
   function pointer and adopts the frontend instance via `CreateInstanceExternal`.
4. Selects the physical device and calls `VulkanContext::CreateDevice` with the
   frontend's required extensions/features.
5. Marks the created device externally owned: the frontend eventually destroys it.
6. Chooses a graphics queue without owning a WSI surface.
7. Obtains `RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE` during context reset.

The context code was fetched at the reference commit; associated presentation
implementation must still be read in full before porting. Do not follow an old
web result for `libretro_vulkan.cpp`: the reviewed revision uses the context and
presentation classes named here, and upstream architecture changes over time.

### 8.2 Required integration audit

The core should consume the frontend-provided dispatch path. The frontend already
links libps5vk statically. Avoid a second independently initialized driver/runtime
inside the core and avoid desktop `dlopen("libvulkan.so.1")` assumptions.

Verify:

- All Vulkan handles passed between core and frontend belong to the same driver
  and compatible device/instance ownership model.
- `vkGetInstanceProcAddr` / device dispatch return every actually required entry
  point; advertised optional extensions do not conceal NULL function pointers.
- The frontend implements the negotiated interface version and required features.
- Context reset/destroy, device loss and failed creation have balanced ownership.
- The core-created logical device is handed back according to the interface;
  no double destroy and no use after frontend teardown.
- Presentation uses the libretro image handoff, expected layout and queue family,
  frontend synchronization callbacks and semaphores/fences as defined by the
  exact `libretro_vulkan.h` version. Audit `set_image`, queue locking and sync
  callbacks where present; do not invent a separate swapchain in the core.
- Any negotiated device replacement preserves RetroArch's own menu/presentation
  resources or correctly recreates them.
- PPSSPP workers cannot submit concurrently outside the interface's queue contract.
- Core unload drains work and destroys dependent objects before mappings or
  dispatch storage disappear.

Do not assume existing software-core load/recovery tests cover any of this.
A small native hardware-core canary may be worthwhile: negotiate the interface,
render a known offscreen pattern, present it, open/close the menu, unload, reload.
That isolates frontend bridge problems from emulation and shader complexity.

## 9. Confirmed current driver blockers

The following were directly inspected in the current local source. Line numbers
are navigation aids and may move; search the named symbols/refusal strings.

### 9.1 Combined depth/stencil: startup blocker

`Common/GPU/Vulkan/VulkanContext.cpp`, `CreateDevice`, around line 655, selects the
first attachment-capable format from:

```cpp
VK_FORMAT_D24_UNORM_S8_UINT
VK_FORMAT_D32_SFLOAT_S8_UINT
VK_FORMAT_D16_UNORM_S8_UINT
```

It asserts that a usable format was found. The libretro device callback actually
calls this function, so this is not merely a standalone desktop path.

`PS5_Vulkan/driver/ps5vk_image.c` advertises depth attachment capability for
`VK_FORMAT_D32_SFLOAT`, but none of those combined formats in the inspected table.
`driver/ps5vk_draw.c` also explicitly refuses stencil attachments and accepts
only the supported D32 depth-target shape.

Required work: implement at least one compatible combined format end-to-end:
allocation/layout, views/aspects, attachment state, clear/load/store, depth and
stencil testing/writing, transfers needed by the selected renderer and truthful
feature reporting. Advertising a format or substituting depth-only D32 does not
implement stencil and must not be used to bypass startup.

### 9.2 Fixed 4K attachments: fundamental framebuffer blocker

`driver/ps5vk_draw.c` defines target width 3840 and height 2160. Colour and depth
attachment checks reject other extents; the render area must cover the full
target. Related inherited/secondary rendering checks enforce the same shape.

PPSSPP uses offscreen PSP framebuffers and intermediate targets. Native PSP output
is 480x272, but internal framebuffer stride/extent and effect targets vary; do
not special-case only 480x272. A 4K frontend swapchain does not change this need.

Required work: parameterize target layout, pitch, allocation size, surface register
encoding, viewport/scissor and render area handling. Validate multiple small,
non-power-of-two and differently sized targets. Reuse the driver's AddrLib/oracle
checks and existing pixel-readback probes. Audit primary, secondary, legacy
render-pass and dynamic-rendering paths, not only one size check.

### 9.3 Culling, stencil and dynamic state

`driver/ps5vk_pipeline.c`, `ps5vk_draw_refusal`, around lines 800-840:

- Accepts dynamic viewport/scissor and selected depth states only.
- Rejects non-NONE face culling, depth clamp/bias, rasterizer discard and non-fill
  polygons in this path.
- Rejects enabled stencil or depth-bounds tests.

PPSSPP's `GPU/Vulkan/PipelineManagerVulkan.cpp` constructs PSP-dependent cull mode,
stencil operations and dynamic `STENCIL_WRITE_MASK`, `STENCIL_COMPARE_MASK` and
`STENCIL_REFERENCE`. These are genuine renderer paths, though individual games
may exercise different subsets. Implement state encoding and reset between
pipelines/draws, with tests that deliberately alternate contrasting states.

Do not infer all rejected optional features are unconditional PPSSPP requirements.
For example, inspect advertised-feature fallback before declaring depth clamp or
logic operations a universal startup blocker.

### 9.4 Blend constants and selective colour writes

The same pipeline function rejects dynamic `BLEND_CONSTANTS` and any colour-write
mask other than RGBA (`0xf`) or none (`0`). PPSSPP pipelines use a PSP-derived write
mask and enable dynamic blend constants when their blend factors require them.

The driver has separately verified constant blend-factor functionality; that is
not equivalent to accepting Vulkan's dynamic blend-constant command/state path.
Implement actual dynamic updates and per-channel masks with correct format
component ordering. Test alpha-only, RGB-only and individual-channel writes over
a known destination, including alternating draw states and menu transitions.

### 9.5 Descriptor sets beyond set zero: conditional limitation

`ps5vk_draw_refusal` rejects pipeline layouts containing descriptor sets beyond
set 0. PPSSPP's draw engine uses combined image samplers and dynamic uniform
buffers; inspect the chosen renderer's complete pipeline layouts, tessellation
and optional paths to establish whether set >0 is actually needed.

This is a **confirmed driver restriction, not a confirmed universal PPSSPP
blocker**. Do not spend time broadening it before recording an actual required
layout or selecting an unavoidable source path.

## 10. Additional graphics risks requiring investigation

These need focused inspection/tests, not unsupported declarations of failure:

| Area | Audit / probe |
| --- | --- |
| Shader generation and compiler | PPSSPP-generated vertex/fragment SPIR-V, UBO accesses, varying locations, integer operations, loops, discard, texture operations |
| Repeated compilation | Driver ACTIVE reports ACO SIGFPE/abort after particular shader sequences; reproduce in one process, including alternating shader families |
| Descriptor churn | Dynamic UBO offsets/alignment/range, sampler counts, updates, pool rollover and lifetime under in-flight work |
| Image formats | Actual texture/render-target/depth format feature combinations, component mapping, packed PSP formats and conversion paths |
| Mipmaps | Multiple levels, non-square extents, view ranges, level selection and filtering; current driver changes touch format/layout work |
| Render-to-texture | Render, transition, sample and render again; PSP framebuffer feedback/copy paths |
| Depth/stencil lifecycle | Independent clears, stencil preservation, write masks and transitions between passes |
| Transfers and readback | Row pitch, offsets, subregions, buffer/image copies, resolves/blits and CPU-visible completion |
| Synchronization | Barriers, visibility, queue submission order, semaphores/fences, upload-buffer reuse and framebuffer reuse |
| Render-pass compatibility | Attachment formats/samples, load/store, secondary command inheritance and render areas |
| Vulkan memory allocator | Memory types/heaps, requirements, alignment, mapped ranges, coherent/noncoherent handling and allocation churn |
| Cache persistence | Pipeline/shader caches, invalidation across driver builds, malformed or incompatible cache recovery |
| Threading | Parallel shader compilation and submission safety; transient workaround must be explicit and measured |

The driver's ACTIVE notes describe an unsigned texture shader that leaves ACO
unable to compile again in that process; running that case last in a separate
probe battery avoids the sequence in diagnostics, not in a real emulator.
PPSSPP compiles many pipelines during play. A stable one-shader demonstration is
insufficient. The precise failing sequence may or may not occur in PPSSPP; capture
real shaders before assuming equivalence.

Shader/frontend compilation and PS5 driver compilation are distinct stages:
PPSSPP generates shader source/SPIR-V using its dependencies; PS5_Vulkan lowers
that shader to GPU code through its compiler. A successful glslang result says
nothing about the latter stage's coverage.

Optional unsupported features must be reported accurately so PPSSPP can choose
its own fallbacks. Do not set feature bits to make negotiation succeed before
semantics exist. Likewise, an audit that accounts for all entry points does not
prove that every valid command argument/state combination is implemented.

## 11. Driver artifact selection and concurrent development

RetroArch uses prebuilt static archives. Rebuilding the sibling driver alone does
not update an already linked title, and a source revision alone does not identify
the executable driver inside that title.

Current `tools/build-title.sh` consumes:

```text
build/driver/ps5/libps5vk.ps5.a
.deps/native/vulkan-runtime/lib/libvk_runtime.ps5.a
build/driver/ps5/libpsbc_driver.ps5.a
.deps/native/psbc/lib/libpsbc_support.ps5.a
```

It also links Mesa utility objects through `tools/build-mesa-util.sh`. Record their
identity and the relevant headers/toolchain. The accepted recent frontend build
retained its normal XMB driver inputs; do not assume it was linked to today's
sibling working tree. Read the corresponding `driver-archives.json` evidence.

The title build has a diagnostic archive snapshot path that verifies source hash
before copying, copied hash, and source hash after copying. Use an equally explicit
stable-input policy for PPSSPP development rather than copying archives during a
concurrent link. Do not enable costly memory diagnostics just to obtain snapshots.

For a driver change: build driver archives in its repository using its documented
procedure; run its relevant gates; record commit plus dirty-diff status; snapshot
all dependent artifacts; relink RetroArch; record frontend/core/driver hashes;
then deploy and read back identity. Do not silently link older archives after a
new driver build fails. Keep the known-good package available for recovery.

## 12. Implementation milestones and acceptance gates

Each milestone is a bounded step, not permission to skip required gates or console
authorization. Record failed attempts as carefully as successful ones. Commit
only what the evidence supports; unverified runtime work stays explicitly pending.

### M0 — Baseline, source choice and branch

- Create the implementation branch, inventory both repositories and accepted
  artifacts, locate the old JIT port, choose a source/submodule/asset pin.
- Produce a dependency/ABI inventory and map old platform adaptations to this SDK.
- Acceptance: reproducible inputs and written delta; no claim of a working core.

### M1 — Native shared core build and safe lifecycle

- Add the build script, metadata and required runtime imports/platform adaptations.
- Validate ELF segments, relocations, undefined imports, constructors and teardown.
- Test missing assets, rejected imports and failed initialization without corrupting
  the frontend. A test configuration may defer hardware initialization explicitly.
- Acceptance: native load/init/deinit and repeated lifecycle capture. If the chosen
  upstream path initializes Vulkan immediately, record that dependency rather than
  artificially claiming this step can run before driver startup support exists.

### M2 — PPSSPP memory/JIT integration

- Implement the minimum arena/executable-memory adaptation and verify aliasing,
  placement, patching, worker synchronization and failure cleanup.
- Prove the active CPU mode through logs and execution evidence, not the saved
  option string alone. Disable JIT intentionally once as a comparison if useful.
- Acceptance: bounded CPU workload and repeated reset/load/unload with stable memory
  accounting. Software-rendered diagnostics do not close the final GPU objective.

### M3 — Frontend hardware interface canary

- Exercise the actual libretro Vulkan negotiation, device/queue ownership and image
  presentation lifecycle using a minimal offscreen render where needed.
- Acceptance: known pixels, zero unexpected refusals, correct menu overlay,
  unload/reload and restored software-core presentation.

### M4 — Minimum driver subset for PPSSPP

- Implement variable target sizes, a combined depth/stencil format, required culling,
  stencil/dynamic state and colour-write behavior in independently tested steps.
- Order implementation by the first unavoidable failure, with small driver probes.
- Acceptance: exact pixel/readback comparisons and alternating-state tests; all
  claimed advertised capabilities match real behavior. Track remaining gaps.

### M5 — PPSSPP Vulkan first content

- Boot a redistributable PSP homebrew/test workload, then an owner-provided game at
  native rendering resolution with conservative settings.
- Capture shaders, first failing Vulkan call/state and graphics timing summaries.
- Acceptance: actual PPSSPP Vulkan GPU work, correct visible content, audio/input,
  no command-buffer-ending errors or unexpected driver refusals.

### M6 — Representative gameplay and transitions

- Add workloads exercising 2D/textures, 3D depth/culling, stencil/blend effects,
  framebuffer feedback/readback and repeated shader creation.
- Test Quick Menu, Close Content, another PSP game, a different existing core and
  native Quit; test cold/warm caches and asset/config failure recovery.
- Acceptance: owner confirms correct colours/effects and clean transitions, with
  matching log evidence and bounded memory growth.

### M7 — Performance and release decision

- Measure CPU emulation, render preparation, shader compilation, submission and
  presentation separately. Record game target rate, emulation speed, frame-time
  percentiles, audio underruns and memory use.
- Do not confuse a 30 FPS PSP game with half-speed emulation or RetroArch's rolling
  display refresh estimate with instantaneous throughput.
- Only then consider resolution scaling and optional features. Full-speed/4K is
  not an initial acceptance promise.
- Ship only the verified configuration, with limitations and recoverable defaults.

## 13. Diagnostics, measurements and failure recovery

Keep `RetroArch.log`, driver trace and kernel logs useful. Capture these identities
once at startup: frontend hash, core hash/source revision, driver archive hashes,
SDK/build identifiers, CPU mode, renderer, negotiated interface and memory policy.

Use aggregate counters and first-failure records instead of per-draw/texture logs:

- Allocations/frees, current/peak bytes by owner and allocation route.
- Guest/JIT/CPU cache/driver/frontend budgets and allocation-failure request size.
- Pipeline/shader count, compile duration and repeated-compile failures.
- Descriptor pools/sets and transient upload memory high-water marks.
- Frames, CPU emulation time, GPU preparation time, submit/present/wait duration.
- First unsupported Vulkan format/state/command and its relevant parameters.
- Content/menu/context lifecycle transitions with a monotonic timestamp.

On first failure capture the error code, allocation owner, requested bytes,
current/peak totals, frame number, shader/pipeline identifiers, content lifecycle
state and queue/fence context. Preserve the first causal error before secondary
cleanup errors overwrite it. Diagnostics must avoid allocation-heavy error paths
or synchronous periodic flushes during navigation/gameplay.

Native core loading can reject unsupported objects safely; it cannot isolate an
arbitrary fault in a core sharing the frontend process. Do not call that process
isolation. Preserve crash logs, ask whether an unexplained title exit was manual,
and do not automatically restart over evidence.

Raw captures stay in ignored `klog/`; credentials/console addresses stay in ignored
`.env`. Commit sanitized machine-readable evidence and expectations under an
appropriate `evidence/ppsspp-*` directory. Never publish owner ROM filenames,
private paths or console identifiers inadvertently.

## 14. Console test matrix

| Test | Evidence needed | Pass condition |
| --- | --- | --- |
| Core discovery | Metadata, core hash, log | Correct core identity; no false success from `.info` alone |
| Native load | ELF report + live loader log | All imports/relocations resolve; constructors complete |
| Missing asset/content | Failure log + continued navigation | Useful error; frontend remains usable |
| JIT microtest | Generated function results, protection transitions | Correct execution/patching, explicit errors on unsupported paths |
| Guest memory | Alias/address tests, resource accounting | Correct mirrors, alignment and matched cleanup |
| Vulkan negotiation | Device/interface/dispatch log | Shared driver and balanced ownership |
| Small framebuffer | Known pattern/readback | Correct extents, pitch, clipping and colours |
| Depth/stencil | Alternating clear/test/write cases | Expected depth rejection and stencil effects |
| Blend/masks | Known destination and component comparisons | Correct constant blending and selective writes |
| Shader churn | Many sequential/repeated compiles | No ACO crash, stale shader or leaked compilation state |
| Gameplay | Owner observation + logs | Correct visuals, audio/input and identified renderer/CPU mode |
| Menu overlay | Open/close repeatedly | Correct colours, synchronization and no persistent corruption |
| Close Content | Menu then another game/core | Clean state restoration and bounded resource recovery |
| Quit | Exit log + klog | Deliberate clean shutdown, no late worker submissions |
| Long run | Timings, accounting and owner report | No progressive memory growth/stalls; scope/duration recorded |

Retain existing FCEUmm/mGBA/Snes9x/FBNeo/Genesis regressions when changing shared
upload, loader or driver code. Existing menu colour fixes and screenshot/thumbnail
fixes are distinct paths: test both hardware gameplay and saved thumbnails.

## 15. Files, commands and proposed deliverables

Existing verification entry point: `tools/verify.sh`, in the established order
`format`, `unit`, `build`, `integration`, `evidence`. Its build gate currently
builds the shipped cores/title; PPSSPP must be explicitly integrated before that
gate can prove anything about it. Keep command definitions in that script and
its existing subordinate tooling, rather than inventing a second gate runner.

Useful read-only baseline commands:

```bash
git status --short --branch
git log -5 --oneline
git -C ../PS5_Vulkan status --short --branch
git -C ../PS5_Vulkan rev-parse HEAD
bash tools/verify.sh --list
rg -n 'ps5vk_draw_refusal|stencil attachments|PS5VK_TARGET_WIDTH' ../PS5_Vulkan/driver
```

After implementation, proposed commands (the PPSSPP build script does not yet
exist at the time of this handoff):

```bash
bash tools/build-ppsspp.sh
python3 tools/check-core.py build/cores/stage/cores/ppsspp_libretro.so --report build/cores/ppsspp/abi.json
readelf -lW build/cores/stage/cores/ppsspp_libretro.so
readelf -dW build/cores/stage/cores/ppsspp_libretro.so
readelf -rW build/cores/stage/cores/ppsspp_libretro.so
bash tools/verify.sh
```

Consult `docs/DEPLOYMENT.md` and the current `tools/run-title.sh` help before an
authorized console run. Do not paste a guessed launch command from this plan.

Proposed evidence files, adaptable to the existing evidence schema:

```text
evidence/ppsspp-native/abi.json
evidence/ppsspp-native/build.json
evidence/ppsspp-native/loader.json
evidence/ppsspp-native/driver-archives.json
evidence/ppsspp-native/capture.json
evidence/ppsspp-native/expectation.json
```

Add focused host tests for new loader/memory ownership behavior and meaningful
console probes for graphics semantics. Do not count a test that only repeats a
constant from its implementation as evidence of runtime correctness.

## 16. Open questions and first-session checklist

Open questions, in priority order:

1. Where is the user's previous working PS5 PPSSPP JIT source/build, and which
   memory/ABI adaptations can be reused without importing its old pipeline?
2. Which PPSSPP/libretro revision is the port target, and how does its hardware
   interface differ from the assessed current upstream reference?
3. Does its dependency graph emit TLS or require unwind features the loader lacks?
4. What exact guest-memory aliasing/alignment and JIT placement does that build
   require? Can the native reservation API meet it without hardcoded addresses?
5. Which PS5_Vulkan changes have landed since this snapshot? Which source state
   and tested archive set will the frontend consume?
6. Which combined depth/stencil format is the best supported hardware/layout path?
7. Does the current RetroArch hardware-core interface work with this driver's
   dispatch/device lifecycle, independently of PPSSPP?
8. Which renderer paths require additional descriptor sets, transfers or optional
   features beyond the four confirmed graphics blockers?
9. Which redistributable workload and owner-provided games define first acceptance?
10. When will the owner authorize a launch after the first reviewed build is ready?

First session sequence:

- Read the required repository context and this plan; inspect both working trees.
- Create/reuse the implementation branch without disturbing pending changes.
- Locate the old port and record a source comparison before writing new adapters.
- Inventory the chosen build's dependencies, ELF/loader requirements and memory
  assumptions. Read the actual libretro presentation implementation and headers.
- Recheck current driver blockers, then select one bounded implementation step.
- State its proof before building; run appropriate gates; record evidence and
  update `docs/ACTIVE.md` plus an append-only phase-log entry when it lands.

Do not begin by advertising unsupported driver features, globally disabling error
checks, forcing a 4K PSP framebuffer, replacing the frontend renderer, or trying
random compiler flags until the binary links. Preserve the causal error and fix
the smallest demonstrated requirement.

## 17. References and limits of this assessment

### Local project references

- [Current RetroArch state](docs/ACTIVE.md)
- [Project gates and invariants](docs/PLAN.md)
- [Testing](docs/TESTING.md)
- [Deployment](docs/DEPLOYMENT.md)
- [GPU-path acceptance criteria](docs/GPU_PATH_CRITERIA.md)
- [Native core build example](tools/build-snes9x.sh)
- [Native loader](src/core_loader_ps5.cpp)
- [Core ABI checker](tools/check-core.py)

Sibling paths below assume the existing `PS5_Homebrews` layout. They are local
research references, not vendored dependencies or portable public documentation:

- `../PS5_Eden/docs/PROBING.md`
- `../PS5_Eden/docs/CAPABILITY_PROBE.md`
- `../PS5_Eden/docs/ACTIVE.md`
- `../PS5_Eden/evidence/capability-probe/`
- `../PS5_Eden/evidence/capability-probe-memory/`
- `../PS5_Eden/evidence/capability-probe-budget/`
- `../PS5_Eden/evidence/capability-probe-layout/`
- `../PS5_Vulkan/docs/VULKAN_PROBE_ACTIVE.md`
- `../PS5_Vulkan/driver/ps5vk_image.c`
- `../PS5_Vulkan/driver/ps5vk_pipeline.c`
- `../PS5_Vulkan/driver/ps5vk_draw.c`

### Public primary sources

- [PPSSPP upstream](https://github.com/hrydgard/ppsspp)
- [Reviewed libretro Vulkan context](https://github.com/hrydgard/ppsspp/blob/f293b10fb2d9dc0c2bc10281444ee3d3e932e6ad/libretro/LibretroVulkanContext.cpp)
- [Reviewed Vulkan device initialization](https://github.com/hrydgard/ppsspp/blob/f293b10fb2d9dc0c2bc10281444ee3d3e932e6ad/Common/GPU/Vulkan/VulkanContext.cpp)
- [Reviewed pipeline states](https://github.com/hrydgard/ppsspp/blob/f293b10fb2d9dc0c2bc10281444ee3d3e932e6ad/GPU/Vulkan/PipelineManagerVulkan.cpp)
- [Reviewed executable-memory utilities](https://github.com/hrydgard/ppsspp/blob/f293b10fb2d9dc0c2bc10281444ee3d3e932e6ad/Common/MemoryUtil.cpp)
- [Reviewed POSIX memory arena](https://github.com/hrydgard/ppsspp/blob/f293b10fb2d9dc0c2bc10281444ee3d3e932e6ad/Common/MemArenaPosix.cpp)
- [Reviewed guest memory mapping](https://github.com/hrydgard/ppsspp/blob/f293b10fb2d9dc0c2bc10281444ee3d3e932e6ad/Core/MemMap.cpp)
- [PS5_Vulkan public repository](https://github.com/mihawk-99/PS5_Vulkan)
- [libretro core metadata](https://github.com/libretro/libretro-core-info)

No PPSSPP console compatibility, sustained performance, exact dependency list,
complete shader feature coverage or full hardware-interface lifecycle is proven
by this plan. Those are explicitly the implementation and verification work it
hands off. The strong platform probes justify beginning that work; the source
restrictions identify why a core build alone is insufficient.
