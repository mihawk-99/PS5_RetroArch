# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-19_

## Now

**The Vulkan driver initialises on the console, builds the stock shader's pipeline,
and runs the runloop - the first frame dies on a render pass left open.** A
`tools/run-title.sh` run now shows: `selected="vulkan"`, the console's `khr_display`
WSI resolved, a device and a swapchain at 3840x2160, four textures, a vertex buffer,
the stock shader's SPIR-V reflected, **both shader modules compiled and
`vkCreateGraphicsPipelines` returning 0**, `vulkan_init` handing back a live pointer,
the input driver opening the pad, and `runloop_iterate` reaching
`vulkan_frame` -> `vkQueueSubmit`. Nothing has reached the screen yet.

**The exact remaining failure**, read from the trace because assertions now say what
they are:

    probe SPAN: offscreen passes
    probe SPAN: menu upload
    probe SPAN: about to begin the backbuffer pass
    probe REC: begin render pass
    assertion failed: cmd_buffer->render_pass == NULL
      (.../vulkan-runtime/src/vulkan/runtime/vk_render_pass.c:2648,
       vk_common_CmdBeginRenderPass2)

The frame's command buffer already has a render pass open when the backbuffer pass
begins, and the marks bracket the span to the first-frame history/feedback clear
(`vulkan_filter_chain_build_offscreen_passes`) and the menu texture upload.

**Five things were fixed to get here**, four of them in this repository:

- **0016 - the topology.** That driver's pipeline check accepts only
  `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST`, and its draw path refuses a non-indexed draw
  whose first vertex is not zero; RetroArch drew its quads as a four-vertex *strip*.
  The quads are now six-vertex lists, the second triangle drawn through the binding's
  own offset. This is what turned `VK_ERROR_UNKNOWN` into a created pipeline.
- **0017 - the samplers.** The driver refuses any sampler that is not clamp-to-edge
  and leaves the output handle untouched, and `CommonResources` destroys every handle
  that is not `VK_NULL_HANDLE`: sixteen refused samplers were destroyed as if they were
  real, and the driver asserted. The array is cleared before it is filled.
- **0018 - the quit.** The display context reported `quit` from the frontend's signal
  handler state, so the runloop ended on its first iteration and `rarch_main` returned
  0 with no frame. A console title has no terminal and no SIGTERM sender, so this
  context no longer treats that state as a quit request.
- **The BSS is cleared by this port's CRT.** `_start` went from `_init_env` straight to
  the static constructors, and the image's writable segment is 0x104b4 bytes in the
  file against 0xc7040 in memory. `tooling/native/ps5-pie.ld` now marks
  `__bss_start`/`__bss_end` and `_start` clears the range; every run prints
  `bss check=0 (must be 0), data check=7 (must be 7)`. Whether the loader also zeroed
  it was never measured - the invariant is what this asserts, and it is cheap.
- **Assertions are readable.** The driver's `__assert` prints through stderr and then
  aborts, and the console's libc buffers stderr, so every assertion used to arrive as a
  bare `abort is called(system)`. `src/main.cpp` sends stderr to the trace **and makes
  it unbuffered**, which is what produced the assert text above.

## Next

1. **Mark the span between the offscreen-passes call and the backbuffer pass**: the
   first-frame `clear_history_and_feedback` and the menu texture upload. Whichever one
   opens a render pass is the bug; `vulkan_framebuffer_clear` and
   `vulkan_copy_staging_to_dynamic` are the two candidates, and the driver's own
   `ps5vk_CmdClearColorImage` is CPU work at a submission split point, so the copy path
   is the likelier one.
2. **Then the first frame**, and with it the objective: `probe FRAME` lines in
   `/app0/trace.txt` every thirty frames, the menu on screen, and the driver's own
   present path.
3. **Then the config file.** Content loading still discards the title's `-c`, and the
   fix (0006) is parked in `parked/config-path.patch.py`.

## Working notes

- **`bash tools/run-title.sh` is the only way to run the title.** It owns the whole
  sequence and prints the title's own trace. Do not hand-deploy.
- **`python3 tools/symbolize-crash.py <capture>` reads a console backtrace**, but only
  approximately: the addresses are the *converted* image's and `build/title.map` is the
  linked one, so a frame can land one function off. The trace's `assertion failed:`
  line is exact - prefer it.
- **Assertions only speak because stderr is unbuffered.** If that line vanishes from
  the trace, check `src/main.cpp`'s `setvbuf` before anything else.
- **A probe is not gone until the objects are.** Reverting the probe set leaves the
  strings in `build/ra/obj`; `strings build/llvm-pie.elf | grep 'probe '` is the check
  and `rm -rf build/ra/obj` is the fix. Two stale probe strings survived a revert this
  round and needed a clean rebuild.
- **An insert must not repeat its anchor.** The probe tool writes `insert + anchor`, so
  an insert ending with its anchor duplicates that line - which broke the build three
  times in one round. `tests/test_frontend.py`'s `ProbeSet` now checks every probe for
  that, for a marker in its own text, for a note and for a trailing newline.
- **Do not hand-edit the configured tree.** Slicing a probe out of `build/ra-conf` by
  text removed a declaration line and a brace's partner this round, and the file only
  went back together by diffing it against `vendor/retroarch`. Revert probes with the
  tool; if the tree is damaged, diff it against its vendor copy (it carries no port
  patches beyond the ones the patcher names).
- **`vendor/` is not in the diff.** Anything a build needs from it must be regenerable
  or live in a tracked directory.
- **`/app0/...` is not an FTP path.** The title's files are at
  `/data/homebrew/PPSA99169/`.
- **`src/` is linked before the archives**, so a definition there wins over the same
  symbol in `libretroarch.a` or the driver's archives - that is how
  `src/locale_shims.c` supplies the `_l` locale functions, and why a libc symbol cannot
  be overridden (the native converter refuses to publish an application export).
- The console's address and credentials come from the ignored `.env`; console captures
  stay in the ignored `klog/` tree.

## Last verified

| Check | Result |
| --- | --- |
| `bash tools/run-title.sh` (probed build) | **Partial**: `selected="vulkan"`, `khr_display`, 4 textures, `vkCreateGraphicsPipelines -> 0`, `vulkan_init` live, pad opened, `vulkan_frame` -> `vkQueueSubmit`, then `assertion failed: cmd_buffer->render_pass == NULL` |
| `bash tools/verify.sh` (all gates) | PASS (format unit build integration evidence) |
| `python3 -m unittest discover -s tests` | PASS: 20 tests, including the probe-set checks and the patch count |
| `strings build/llvm-pie.elf \| grep 'probe '` | Only the driver's own messages; no probe text (after a clean rebuild) |
| Config path (0006) | **PARKED**: reading the config still crashes in `command_event`; see `parked/config-path.patch.py` |

## Open findings

- **The first frame leaves a render pass open** before the backbuffer pass; the marks
  narrow it to the offscreen-passes call and the menu upload, and the next measurement
  splits those two.
- **The frontend's own log never reaches disk.** `--log-file=/app0/retroarch.log` is in
  argv and the file exists, but it is a stale 1200 bytes: RetroArch never closes the
  logger on this path, so its buffer is lost. The trace is the instrument that works.
- `src/locale_shims.c` exists because glslang and SPIRV-Cross are written against
  FreeBSD's xlocale interface and the console SDK exports only the unsuffixed
  functions. If the SDK ever ships the `_l` set, that file goes.
- The console's FTP will not replace `sce_module/libc.prx`; the title runs against the
  console's copy. A release has to solve this.
- Pad input cannot be verified unattended yet: nothing in the pipeline presses a
  button.
