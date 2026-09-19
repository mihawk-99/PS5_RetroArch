# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-19_

## Now

**The Vulkan driver initialises, creates the stock shader's pipeline, and runs the
runloop - the first frame's draw is refused by the driver.** `video_vulkan` is
selected, the console's `khr_display` WSI resolves, a device and a swapchain at
3840x2160 are created, four textures and a vertex buffer are built, both stock shaders
compile through the driver's own compiler, `vkCreateGraphicsPipelines` returns 0,
`vulkan_init` hands back a live pointer, the input driver opens the pad, and
`runloop_iterate` reaches `vulkan_frame` -> the backbuffer pass -> the chain's draw ->
`vkQueueSubmit`. Nothing is on screen.

**The two rounds spent on "a render pass is already open" were this port's own probe.**
A probe insert had duplicated the `vkCmdBeginRenderPass` call - the tool writes
`insert + anchor`, the insert ended with the anchor, and the statement appeared twice,
so the second call found the pass the first had begun. The duplicate detector (diff the
configured tree against `vendor/retroarch` for lines that appear more often) found five
such pairs; the file was restored from upstream and the port patches re-applied.
`tests/test_frontend.py`'s `ProbeSet` now checks that an insert does not contain its
anchor *at all*, which is the property that matters, and it caught one more while this
was written up.

**The real remaining failure**, from the trace (assert messages are readable because
stderr is unbuffered):

    probe REC: begin render pass
    probe DRAW: binding for the second triangle
    probe DRAW: the quad draw is recorded
    assertion failed: cmd_buffer->state == MESA_VK_COMMAND_BUFFER_STATE_INITIAL ||
      ... EXECUTABLE || ... PENDING  (vk_queue.c:362, vk_queue_submit_add_command_buffer)

That state is what a **recording refusal** leaves, so the draw is refused. The driver
refuses in three places, and two are silent here: `pipeline->draw_refusal` (a string
computed at pipeline creation) and `ps5vk_pipeline_prepare_shaders` - its AGC
`sceAgcCreateShader`/`sceAgcLinkShaders` step, which it runs at the **first draw**
rather than at pipeline creation. The viewport/scissor check passes (one of each).

**Its messages are compiled out**, which is why this cannot be read from here:
`src/vulkan/runtime/vk_log.c` drops every message unless `MESA_DEBUG` was set at build
time, or the instance has debug logging on, or a debug callback is installed - and
`enable_debug_logging` is never assigned anywhere in that tree.

## Next

1. **Make that driver speak, from this side**: patch the frontend's Vulkan instance
   creation to enable `VK_EXT_debug_utils` and install a messenger whose callback
   writes to stderr (which is the trace file). Then every refusal names its reason, and
   the remaining work is ordinary. If the extension is refused, the failure is
   immediate and visible.
2. **Or take the answer from `../PS5_Vulkan`**: what `sceAgcCreateShader` /
   `sceAgcLinkShaders` return for this pipeline, or a build whose `vk_log` is not
   compiled out. One value settles it.
3. **Then the first frame**, and with it the objective: frames in the trace, the menu
   on screen, and `tools/verify.sh` green.
4. **Then the config file.** Content loading still discards the title's `-c`; the fix
   (0006) is parked in `parked/config-path.patch.py`.

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
