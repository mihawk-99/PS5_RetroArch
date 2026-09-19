# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-19_

## Now

**The Vulkan driver runs on the console, and the first frame stops one call short.**
A `tools/run-title.sh` run shows RetroArch selecting `video_vulkan`, resolving the
console's `khr_display` WSI, creating a device, a swapchain at 3840x2160, its
textures (4x4 blank, two 512x512 fonts, 1x1 default), a vertex buffer, reflecting the
stock shader's SPIR-V and compiling both of its stages into shader modules - and then
`vkCreateGraphicsPipelines` answering `VK_ERROR_UNKNOWN`. `vulkan_init` returns NULL,
the title exits 1. Nothing renders yet; everything up to that call is new.

**Where the refusal is.** Not in the frontend's pipeline state, which was dumped and
compared against the driver's own checks (R32G32_SFLOAT attributes, TRIANGLE_STRIP,
blend off, one sample, two descriptor bindings at the strides that driver assigns),
and not in the shader compiler: both stock shaders compile through the *host* build of
`../PS5_Vulkan`'s compiler with the driver's own options. What is left is the step no
host test can reach - `sceAgcCreateShader`/`sceAgcLinkShaders` on the console inside
that driver's `ps5vk_graphics_pipeline_create`, whose failure is the
`AGC shader creation or linking failed: 0x%08x` that returns the error. That project's
own log is compiled out (`#if !MESA_DEBUG` in its `src/vulkan/runtime/vk_log.c`), so
the code it holds has to be read from their side: it is the one question this port
cannot answer for itself.

**Five frontend faults were fixed to get this far**, each a request that driver
refuses (patches/series 0013-0015, all in `gfx/`): the swapchain's usage bits masked
with the surface's `supportedUsageFlags`; an image's usage masked to its format's
advertised features, with a format that cannot be sampled replaced by
`R8G8B8A8_UNORM`; and `vulkan_format_to_bpp()` taught that format, without which the
staging buffer it sized came out zero bytes long and the driver aborted in
`vk_buffer_init`.

**And one file had to go: `src/video_filters_stub.cpp`.** It defined the whole Vulkan
filter chain as NULL-returning stubs, from when this port had video filters disabled.
`src/` is linked before the archive, so the stubs won and the driver read "no chain" -
no pipeline was ever created, and neither the log nor the trace could say so. Removing
it brought in glslang and SPIRV-Cross, and with them FreeBSD's xlocale interface:
`src/locale_shims.c` provides the thirty-four `_l` functions the console SDK does not
export, each the C-locale answer its unsuffixed counterpart gives.

## Next

1. **Ask `../PS5_Vulkan` for the `sceAgcCreateShader`/`sceAgcLinkShaders` result code**
   for this pipeline, or for a build whose log is not compiled out. That single value
   is the difference between a shader package the console rejects and an AGC call made
   in the wrong state, and this port has no way to see it.
2. **Then the first frame.** The swapchain, the textures and the pipeline are the last
   three things between here and a menu on screen; `vulkan_init` returning non-NULL is
   the milestone to watch for in `/app0/trace.txt`.
3. **Then the config file.** Content loading still discards the title's `-c`, and the
   fix (0006) is parked in `parked/config-path.patch.py`.

## Working notes

- **`bash tools/run-title.sh` is the only way to run the title.** It owns the whole
  sequence, including starting the klog listener before the launch and closing the
  title itself, and it prints the title's own trace. Do not hand-deploy.
- **Read a console crash with `python3 tools/symbolize-crash.py <capture>`.** A
  backtrace is bare addresses and `--exclude-libs` leaves the driver, the compiler and
  the SDK with no symbol table; `build/title.map`, written by every link, is what turns
  them into names - it is how `0x41a7c4` became `ps5vk_CreateSwapchainKHR +0x634`.
- **A console round is only worth its minutes when the mark is placed after the
  assignments.** A dump inserted before a struct is filled prints garbage; one inserted
  inside a declaration list does not compile; one inside a function body cannot include
  a header that opens an `extern "C"` block. All three happened.
- **`vendor/` is not in the diff.** Anything a build needs from it must be regenerable
  or live in a tracked directory; `tooling/ps5-stubs/` and `src/` are the second case.
- **Runtime probes are kept: `tools/apply-runtime-probes.py`.** A rebuild wipes probes
  written into `build/ra-conf`, so they live in that script - applied with one command,
  `--revert` to remove, `--list` to see what each is for. They are not in the shipping
  build; revert them before the gate run, and check `strings build/llvm-pie.elf` says 0.
- **`/app0/...` is not an FTP path.** The title's files are at
  `/data/homebrew/PPSA99169/`; `RETR /app0/trace.txt` answers 550.
- **`src/` is linked before the archives**, so a function defined there silently
  overrides the same symbol in `libretroarch.a` or in the driver's archives - that is
  what the filter-chain stub was doing. It is also what `src/locale_shims.c` relies on.
- **`src/` must be compiled with the frontend's `-D` flags**, which
  `tools/build-title.sh` does by asking `tools/retroarch-flags.sh`.
- **Compile from `build/ra-conf`, never from `vendor/retroarch`**; upstream is never
  edited and `patches/series` is the only record of this port's changes.
- **`-DHAVE_MAIN` must never be defined**: it compiles the frontend's main loop out.
- The console's address and credentials come from the ignored `.env`; console captures
  stay in the ignored `klog/` tree.

## Last verified

| Check | Result |
| --- | --- |
| `bash tools/run-title.sh` (probed build) | **Partial**: `selected="vulkan"`, `context driver=khr_display`, 4 textures, `BUF bound`, `REFL entered`, `CHAIN both modules created`, then `vkCreateGraphicsPipelines -> -13` |
| Host compile of both stock shaders | PASS: `build/host/opengnm-psbc-probe` accepts the extracted SPIR-V with the driver's options (vertex `--ngg` and `--raw`, fragment with UBO stride 16 + sampler binding 2 stride 48) |
| `bash tools/verify.sh` (all gates) | PASS (format unit build integration evidence), probes reverted |
| `strings build/llvm-pie.elf \| grep -c probe` | PASS: 0 - the shipping image carries no probes |
| `python3 -m unittest discover -s tests` | PASS: 18 tests |
| Config path (0006) | **PARKED**: reading the config still crashes in `command_event`; see `parked/config-path.patch.py` |

## Open findings

- **The pipeline refusal is on `../PS5_Vulkan`'s side of the line**: its
  `ps5vk_graphics_pipeline_create` creates the stages through `sceAgcCreateShader` and
  links them with `sceAgcLinkShaders`, and its error message is compiled out of its own
  build. The result code it holds is the missing fact.
- `src/locale_shims.c` exists because glslang and SPIRV-Cross are written against
  FreeBSD's xlocale interface and the console SDK exports only the unsuffixed
  functions. If the SDK ever ships the `_l` set, that file goes.
- The console's FTP will not replace `sce_module/libc.prx`; the title runs against the
  console's copy. A release has to solve this.
- Pad input cannot be verified unattended yet: nothing in the pipeline presses a
  button.


The first link failed on **24 undefined symbols**, and they were three different
problems rather than one:

1. **Four `__eh_frame_*` boundaries.** The shader compiler is C++ and links the SDK's
   libunwind, which finds unwind tables through those symbols, not through
   `dl_iterate_phdr`. `tooling/native/ps5-pie.ld` now provides them, the same four
   `../PS5_Vulkan/tooling/psbc/ps5-pie-unwind.ld` defines.
2. **Eight Mesa utility symbols** (`u_thread_create`, `u_thread_setname`,
   `util_barrier_init/destroy/wait`, `util_thread_get_time_nano`,
   `os_create_anonymous_file`, `os_read_file`). Their PS5 object list filters
   `src/util/{u_thread,anon_file,os_file}.c` out of the compiler archive, and its own
   `libvulkan.so.1` only links because a *shared* object may leave symbols undefined.
   An executable may not. `tools/build-mesa-util.sh` compiles those three sources with
   their PS5 configuration, because `u_thread.c`'s `util_barrier` layout has to match
   the `u_queue.ps5.o` already inside the archive.
3. **Twelve `sceAgc*` entry points.** The console provides libSceAgc and
   libSceAgcDriver and the SDK stubs neither, so they are declared in
   `tooling/ps5-stubs/` for the converter to record as imports.

**The AGC stubs moved out of `vendor/`.** `vendor/` is not in the diff, so the
declarations the link needs would have been lost with the next checkout. They are
tracked under `tooling/ps5-stubs/` now, which is also where `tools/build.sh` reads
them.

**Not yet working on the console.** `bash tools/run-title.sh --watch 20` on this build:
the title launches, the console reports it running, and every one of its eight attempts
ends in `rarch_main returned = 1` with nothing in between - no `ps5_init`, no frame, no
menu. `ps5_init` is expected to be absent, because the compiled video default is now
`vulkan` and the config that names `ps5` is the one content loading discards (0006), but
*something* returns 1, and the frontend's own log (`/app0/retroarch.log`, fetched over
FTP) stops after the audio fallback with no fatal-error line: the setjmp handler in
`rarch_main` logs `Fatal error received in: "<what>"`, and that line is not there, so
the return is one of the silent paths after `drivers_init` - the leading candidate is
`task_push_load_content_from_cli` returning false (`retroarch.c:6036`).

**Read the log, not the trace, for this one.** `/app0/retroarch.log` is fetched with
`RETR /data/homebrew/<title>/retroarch.log`; `/app0/...` is not an FTP path. It proves
the frontend's own startup ran: input driver `ps5` found, audio fell back to null,
display server `null`, core geometry 320x240.
