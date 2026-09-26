# LRPS2 in the PS5 title: requirements, gaps and plan

LRPS2 is libretro's PCSX2 core. The goal is a production-quality PS2 core in
this title (PPSA99169), rendering through ../PS5_Vulkan. It must run God of War
II (demo), Ratchet & Clank (demo) and Final Fantasy X (demo, Besaid) correctly
at 4K internal resolution (6x) with maximum settings, at full speed, and stay
stable over long sessions. This page is Phase 0: what the pinned core needs,
what the console and driver give it today, and the order of the work. Results
go to docs/PHASE_LOG.md as they come. The Vulkan half of the gap list and the
driver rounds are in ../PS5_Vulkan/docs/LRPS2_GAPS.md.

## Source and workflow

- **Pinned revision:** my fork ../PS5_LRPS2 (github.com/mihawk-99/PS5_LRPS2),
  master at `6d14775ead86932f48f0107b4f4d7034bfccf344` (2026-09-25). It is
  libretro's LRPS2 with an AArch64 port and a C89 x86 emitter, among other
  work; the x86-64 recompilers are upstream PCSX2's.
- **Every change to the core is made in the fork**, on a local `ps5-port`
  branch cut from the pinned revision (committed locally, never pushed).
  `patches/lrps2/ps5-port.patch` is that branch's diff against the pin, written
  back from the fork after each change. `tools/build-lrps2.sh` clones the pinned
  revision and applies it, as the Dolphin and PPSSPP builds do, so a clean
  checkout of this title builds the port without the fork's unpushed commits.
  `LRPS2_DEV=1` builds the fork's working tree directly while I edit it.
- The build follows the other native cores: `tooling/lrps2/ps5-toolchain.cmake`
  (FreeBSD x86-64 through the SDK's prospero-clang), libc shims where the core
  calls what the console lacks, `tools/check-core.py`'s ABI check,
  `tools/core-imports.py`'s import table, and the core stamp.

## What the core needs

### Rendering

- **Hardware renderer (Vulkan)**, the shipping path. It needs a Vulkan 1.1
  instance and device, `VK_KHR_sampler_mirror_clamp_to_edge` (enabled
  unconditionally), R16G16B16A16_UNORM as a colour target, and a sampled
  D32_SFLOAT_S8_UINT. It relies on barriers inside render passes for its
  feedback loops and uses a dozen optional extensions. The details and the
  driver rounds are in ../PS5_Vulkan/docs/LRPS2_GAPS.md.
- **paraLLEl-GS**: a Vulkan compute renderer needing most of the 1.2 feature
  set. It is used for Profile 5's compute stress only, after the rest.
- **Software renderer**: "Software (SW)" rasterises on the CPU and asks the
  frontend for no hardware context (the core's `libretro_select_hw_render`).
  Phase 2 boots with it, so the CPU side is proven apart from the driver. Its
  rasteriser is JIT-compiled with xbyak into the code area below.
- The GS thread is the frontend's own: `retro_run` drains the GS ring
  (`MTGS::MainLoop`) and every Vulkan call happens there.
- Shaders are GLSL, compiled at run time by the bundled glslang, with a
  pipeline and SPIR-V cache in `system/pcsx2/cache`.

### CPU emulation and the platform

| Area | What the pinned core does |
|---|---|
| Recompilers | x86-64: EE (R5900), IOP (R3000A), microVU0/1, the VIF unpack dynarec, and the software GS's xbyak JIT. The multi-ISA sources are built for SSE4.1, AVX and AVX2 and chosen at run time by CPUID; the console's Zen 2 has all three. |
| Addressing from JIT code | The C89 emitter encodes a global either RIP-relative or as a sign-extended 32-bit absolute, and aborts on anything else (`c89emit.h`, `E_MODRM_ABS`). The code area, the core's own data and guest main memory must therefore be within 2 GiB of each other. Upstream finds such a spot near the executable on Windows only; elsewhere it takes whatever `mmap` returns. |
| Main memory | 320 MiB (`HostMemoryMap::MainSize`): EE RAM, scratchpad and ROMs; IOP memory; VU memory; a bump allocator. It is a shared-memory object (`memshm_create`, i.e. `shm_open`) so the fastmem window can map its pages. |
| Code area | 305 MiB, read-write-execute (`CodeSize`): EE 64, IOP 32, VIF0/1 8 each, mVU0 64, mVU1 64, VIF unpack 1, software GS 64. |
| Fastmem | A 4 GiB window (`memshm_area_create`) into which guest pages of main memory are mapped at their guest virtual addresses, with `mprotect` on single pages. Faults are backpatched to slow paths. |
| Code protection | EE RAM pages holding recompiled code are made read-only (`mmap_MarkCountedRamPage`), and a write fault invalidates their blocks. |
| Page size | `__pagesize` is 4 KiB on x86 and 16 KiB on Apple AArch64. The vtlb coalesces four 4 KiB guest pages into one host page when the host page is 16 KiB (`vtlb.cpp`, the `__pagesize != VTLB_PAGE_SIZE` paths). |
| GS memory | 4 MiB of GS local memory through `memfd_create`/`shm_open`, mapped several times back to back so reads wrap (`GS.cpp`). |
| Faults | libretro-common's faulthandler on SIGSEGV (and SIGBUS on Apple and AArch64); FreeBSD x86-64 reads `mc_rip`. |
| Threads | The EE and IOP on the core's CPU thread (created at load, paused and resumed around `retro_run`, save states and option changes); MTVU (VU1) on its own thread by default; the GS on the frontend thread; the CDVD reader thread; software GS worker threads. |
| Audio | SPU2 produced on the CPU thread; batched to the frontend at the end of `retro_run`. |
| Files | libretro VFS. BIOS in `system/pcsx2/bios`. Memory cards in `system/pcsx2/memcards` (shared) or per content. Cache in `system/pcsx2/cache`. Discs as ISO, CHD, CSO, ZSO, GZ, CUE and ELF, with M3U lists. `block_extract` is set, so the frontend never unpacks an archive for it. |
| Bundled dependencies | Everything is in the tree: libretro-common (CHD, zstd, LZMA, deflate, LZ4, FLAC, PNG, YAML, rthreads, fastjmp, faulthandler, memmap), glslang, the Vulkan headers and xbyak. The built-in GameDB is generated at build time. OpenGL (glad), DEV9 networking and USB are not needed. |

## The console against those needs

| Need | The console, from the Dolphin and PPSSPP ports | Plan |
|---|---|---|
| Shared memory for main memory, fastmem and GS memory | Every view of a shared-memory object is charged to the title's flexible memory (about 450 MB for the whole title), which Wind Waker's mirrors exhausted. Direct memory is a separate pool of several GiB, and one allocation maps at any number of addresses (Dolphin's MemArena). | A PS5 backend for libretro-common's `memshm_*`, `memshm_area_*` and `memreserve*` in the fork: direct memory for the objects, and `sceKernelReserveVirtualRange` for the reservations. The fastmem window and GS memory both come from it. |
| Address layout | `0x2_0000_0000`-`0x2_FFFF_FFFF` is the driver's GPU window. An unhinted `mmap` lands in it. Dolphin's arena is at `0x10_0000_0000` and PPSSPP's range at `0x6_0000_0000`. The title's core loader maps a core wherever `mmap(nullptr)` puts it (Dolphin landed at `0x2_0199_8000`). | One 2 GiB neighbourhood above the GPU window for the core image, the 305 MiB code area and the 320 MiB main memory. That needs the loader to place a core at a chosen base, which is a general loader change, and hints in the core for the two areas. The 4 GiB fastmem window goes anywhere outside the GPU window (it is reached through a register). |
| Executable memory | A mapping asked for with `PROT_EXEC` does not execute; one mapped read-write and then `mprotect`ed executable does (Dolphin and PPSSPP). | The code area is mapped that way at its hint. |
| Flexible memory | About 450 MB for the whole title, with Dolphin running leaving roughly 238 MB free. Anonymous mappings are charged in full when made. | 305 MiB of code does not fit beside RetroArch. I will measure what a game really uses, then size the areas to fit (they are upstream's generous defaults) or place code in direct memory if the console executes it. Either way the result is in the log. |
| Page size | 16 KiB. | `__pagesize` 16 KiB for the PS5, using the vtlb's coalescing path. That path so far has run only on AArch64 Apple, so the x86 recompilers' page-protection users (`mmap_MarkCountedRamPage`, the mVU dispatcher's size) need checking against it. |
| Faults | A fault on a reserved, unmapped page can arrive as SIGBUS. The console's machine context has 48 bytes before `uc_mcontext` that FreeBSD's lacks (measured by the platform probe). | Take both signals. My SDK fork's `sys/_ucontext.h` declares the 48 bytes, so the FreeBSD branch's `uc_mcontext.mc_rip` is right as written. |
| Threads, timers, audio, input, FTP-visible files, 120 Hz pacing | Solved by the title for Dolphin and PPSSPP: `core_threads_ps5` (sampler registration and stack sizes), `audio_ps5`, `input_ps5`, `permissions_ps5`, and the pacer. | Reused as they are; the CPU thread and MTVU get the sampler hooks. |
| Content | The Ratchet & Clank demo is a `.7z`, which LRPS2 does not open and the frontend will not unpack for it. The FFX demo is the European release, so PAL at 50 Hz. God of War II's demo is an ISO. | I convert the 7z once, on the console's own storage, beside the original and leaving it untouched. Full speed for FFX is 50 fps, and its pacing on the 120 Hz output gets its own check. |

## Plan

1. **Phase 1 — build:** `tools/build-lrps2.sh`, the toolchain file, libc shims,
   the import table, ABI check and stamp; packaged into the title with its info
   file. Loading it on the console is the first test.
2. **Phase 2 — platform, on the software renderer:** memory map and the
   `memshm` backend, the loader's placement, JIT memory, the fault path,
   threads, audio and input; then the BIOS; then each game to a playable scene,
   with a save state there as the baseline (in a test directory; my own states
   and memory cards are never touched); then memory cards and save states.
3. **Driver rounds R81-R84** (see the Vulkan page), started in parallel with
   Phase 2, since they are what stands between the hardware renderer and its
   first frame.
4. **Phase 3 — hardware renderer:** first frames, then correctness against
   desktop LRPS2 at the same revision on the same states (GS dumps where a frame
   needs bisecting), then R85-R87 as the profiles make them matter.
5. **Phase 4 — Profiles 0-12**, with any missing option exposed cleanly. The
   upscale option listed 1x, 2x, 4x and 8x; the fork adds 3x, 5x and 6x.
   Sharpening, anti-aliasing and TV effects are the frontend's slang shaders:
   LRPS2 has none of its own.

## Status (2026-09-26)

Phase 1 is done: the core builds from the pinned revision and the committed
patch, passes the ABI check and is staged in the title. Phase 2's platform work
is done: the code area and guest memory come from my SDK fork's platform layer,
and the recompilers have upstream's 305 MiB again (docs/PHASE_LOG.md, the SDK
fork's adoption). The BIOS boots on both renderers.

Phase 3 has started. The hardware renderer draws on the driver's R81-R91 and
runs God of War II's demo, Final Fantasy X's demo and San Andreas at 6x
(2880x2160) at full speed, with MTVU on by default on the PS5
(docs/PHASE_LOG.md, "LRPS2's hardware renderer at 4K"). What is left of it:

- the comparison with desktop LRPS2 at the same revision and settings, which is
  where San Andreas' doubled radiosity haze at 6x is settled;
- FFX's pacing on the 120 Hz output: a PAL game presents at 50 Hz;
- the Ratchet & Clank demo, once its .7z is converted on the console's own
  storage;
- memory cards and save states in a test directory (Profile 10), then Phase 4's
  profiles.
