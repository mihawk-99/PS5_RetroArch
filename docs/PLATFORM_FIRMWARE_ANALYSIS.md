# The 10.01 system modules: what they tell us

I have an unpacked copy of a console's system modules on this machine,
outside every repository (a folder named for firmware 10.01). This page records
what comparing their export tables with our SDK and our shims established, and
whether the copy is worth using. It names exported functions and modules only.
Nothing from the copy is in any repository, and the reader I used lives in my
scratch space.

## What is there

332 files, all x86-64 ELF images with the FreeBSD ABI and readable dynamic
tables:

| Folder | Contents |
|---|---|
| `common/lib` | 277 modules: libkernel in three variants (`libkernel`, `libkernel_web`, `libkernel_sys`), `libSceLibcInternal`, and the Sce libraries (VideoOut, AudioOut, Pad, Agc, Sysmodule, ...) |
| `priv/lib` | 26 system-private modules |
| `sys` | system processes |
| `vsh` | system applications |

Each module's symbols are named by NID: an 11-character hash of the function
name, the same derivation our own ELF writer uses for our imports
(tooling/native/sce_module_writer.cpp). With the module's library table, an
export reads as "NID, library, module". I computed NIDs for every name in the
payload SDK's stub libraries, the SDK headers,
tooling/native/runtime/imports.txt and the functions our shims replace, and
looked each up.

**Versions.** The copy is 10.01. My console runs 12.09:
`sceKernelGetSystemSwVersion` reports `12.090.001` in our own capability probe
(../PS5_PayloadSDK, platform/evidence/probe-2026-09-25), and
`kern.sdk_version` reports
0x10010000. So everything below is a hint, and each behaviour that matters was
measured on the console.

## What it shows that the SDK does not

**The SDK's stubs are almost all real.** Every one of the 3,015 names in the
SDK's `libSceLibcInternal` stub is exported by the console's libc. Of the 1,111
names in the `libkernel_web` stub, all but `sceKernelGetPrefixVersion` and
`sceKernelIccGetThermalAlert` are exported (the same two are missing from the
other variants). The other stubs we could link are complete except for
system-only functions nobody here calls: six system-cursor functions in
VideoOut, 17 crash-report functions in SystemService, 40 settings getters in
UserService and two device-control functions in AudioOut.
`libScePosixForWebKit` and `libSceGLSlimVSH` are not in the copy, so their
stubs cannot be judged from it; the SDK routes `arc4random`,
`arc4random_buf`, `getaddrinfo`, `freeaddrinfo`, `opendir` and `readdir`
through the former, and our titles measured `arc4random` resolving to nothing.

**Why our libc gaps are gaps.** For the functions our shims replace
(src/libc_shims.c, src/ps5_directory.cpp, tooling/lrps2/ps5-libc-shims.cpp,
the cores' patches):

| Function | Exported by | So |
|---|---|---|
| `gmtime_r`, `localtime_r`, `utimensat`, `futimens`, `dirfd`, `faccessat`, `readlinkat`, `clock_nanosleep`, `if_nameindex`, `arc4random`, `arc4random_buf`, `arc4random_uniform` | no module in the copy | no stub fix is possible: shims |
| `umask`, `statfs`, `fstatfs`, `openat`, `unlinkat`, `fchmodat`, `fstatat`, `mkdirat`, `renameat`, `fchownat`, `linkat`, `symlinkat` | `libkernel_sys` only | our titles import `libkernel_web` by design (docs/PLAN.md), so these resolve to nothing; shims |
| `statvfs`, `fstatvfs` | libc | exported, but libc builds them on `statfs`, which only `libkernel_sys` carries; our titles measured `statvfs` faulting inside libc (2026-09-24): shim |
| `opendir`, `fdopendir`, `readdir`, `closedir`, `rewinddir` | libc | exported; our titles measured the console refusing `opendir`, so enumeration goes through `getdents`, which `libkernel_web` exports |
| `gmtime`, `posix_memalign`, `aligned_alloc` | libc | real exports |

So the question has a clear answer: no gap is a stub routing mistake that
a stub fix would replace. Each is either absent from the system, present only
in the variant our titles do not import, or exported but faulting or refused.
The shims stay, once, in the platform layer.

**The memory interface.** Everything the platform layer needs is exported by
all three libkernel variants and named in the SDK's libkernel stubs, but not
one `sceKernel` function is declared in the SDK's headers:

- direct memory: `sceKernelAllocateDirectMemory`,
  `sceKernelAllocateMainDirectMemory`, `sceKernelReleaseDirectMemory`,
  `sceKernelCheckedReleaseDirectMemory`, `sceKernelMapDirectMemory`,
  `sceKernelMapDirectMemory2`, `sceKernelMapNamedDirectMemory`,
  `sceKernelGetDirectMemorySize`, `sceKernelAvailableDirectMemorySize`,
  `sceKernelDirectMemoryQuery`, `sceKernelGetDirectMemoryType`;
- protection: `sceKernelMprotect`, `sceKernelMtypeprotect`,
  `sceKernelQueryMemoryProtection`, `sceKernelVirtualQuery`, and POSIX
  `mprotect`;
- virtual ranges: `sceKernelReserveVirtualRange`,
  `sceKernelSetVirtualRangeName`, `sceKernelMmap`, `sceKernelMunmap`,
  `sceKernelBatchMap`, `sceKernelBatchMap2`;
- memory pools: `sceKernelMemoryPoolExpand`, `...Reserve`, `...Commit`,
  `...Decommit`, `...Batch`, `...Move`, `...GetBlockStats`;
- flexible memory: `sceKernelMapFlexibleMemory`,
  `sceKernelMapNamedFlexibleMemory`, `sceKernelReleaseFlexibleMemory`,
  `sceKernelAvailableFlexibleMemorySize`,
  `sceKernelConfiguredFlexibleMemorySize`;
- shared memory for JIT code: `sceKernelJitCreateSharedMemory`,
  `sceKernelJitCreateAliasOfSharedMemory`, `sceKernelJitMapSharedMemory`,
  `sceKernelJitGetSharedMemoryInfo`. In `libkernel_web` these are exported
  only under the `libkernel_jvm`, `libkernel_ps2emu` and `libkernel_psmkit`
  libraries. Our probe measured `sceKernelJitCreateSharedMemory` refused to
  the title (0x80020001), so executable code does not go this way.

Across our repositories the kernel functions we call are all among these, and
nine files plus two core patches declared their own prototypes for them. The
machine-context offset was carried in five places: the crash reporter, the
sampler, and the Dolphin, PPSSPP and LRPS2 ports.

## Verdict

Useful, as an index, for three things: deciding which exported functions are
worth probing, explaining why an import resolves to nothing (which module and
variant exports it), and confirming that the SDK stubs are not the problem.
Not useful for behaviour: the copy is two firmware versions behind my console,
and an export says nothing about whether a title may use it --
`sceKernelJitCreateSharedMemory` is exported and refused, `opendir` is
exported and refused. Every behaviour the platform layer relies on is
established by our own probe instead (../PS5_PayloadSDK, platform/docs/PROBE.md).

## Where the platform layer lives

In my fork of the payload SDK, `../PS5_PayloadSDK`, which every project pins by
revision. The fork's `sys/_ucontext.h` declares the console's 48 extra bytes,
so `uc_mcontext.mc_rip` is the faulting instruction and no project carries an
offset. Its `platform/` builds `libps5platform.a` and installs
`include/ps5platform` with the SDK. That covers the kernel declarations, the
libc gaps above, the executable-code allocator and direct-memory shared memory,
with host tests. Its `platform/tools/setup-sdk.sh` installs the fork's revision
over the v0.42 release binaries.
