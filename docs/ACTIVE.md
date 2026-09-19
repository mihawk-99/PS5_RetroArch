# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-19_

## Now

**Frames run.** An unattended `tools/run-title.sh --no-build --watch 25` records **644
frames** and **643 menu draws** and is still alive when the script closes it: *"VERDICT:
it ran for 25s and this script closed it"* (`klog/run-PPSA99169-022103.log`,
`klog/trace-r3o.txt`). The probe-free shipping image behaves the same way on its own run
(`klog/run-shipping-r3.log`, `klog/run-PPSA99169-023553.log`: *"it ran for 20s and this
script closed it"*, no fatal signal, `grep -c 'fatal signal'` = 0). `video_vulkan` is
selected, the device and the 3840x2160 `khr_display` swapchain are created, the stock
shaders compile through the driver's own compiler, and `vulkan_frame` -> chain draw ->
`vkQueueSubmit` repeats for the whole run.

**The driver's own messages were what made this ordinary.** The messenger patch (0019)
put every refusal in the trace, and each one named its reason:

| Refusal | What it was | Fix |
| --- | --- | --- |
| `set 0 binding 1: a combined image sampler write names no sampler` | all four display samplers were refused at creation (`borderColor` opaque white; the driver creates only transparent black), so the menu quad wrote a NULL sampler | 0024 (the colour the driver names), 0025 (a refused sampler falls back to the nearest one) |
| `set 0 binding 2 is not bound or holds no write` | the display layout's **second** sampler binding: the driver requires a write for every binding its stage metadata names | 0026 (write the same image at binding 2, as the HDR path already does) |
| `samples a view whose component mapping is not the identity` | the menu texture used B4G4R4A4 with a B/R view swizzle | 0027 (32-bit, swizzle-free path), 0028 (the CPU conversion's channels in R8G8B8A8 order) |

**What is still refused, all at init and none of them fatal:** seven triangle-strip
clears and one storage-image compute upload of the frontend's blank texture, plus the
filter chain's non-clamp sampler modes (the chain's sampler table is repaired by 0023).
They cost the frontend work, not the frame.

**Nothing proves pixels yet, and the capture path is now half-built.** The port has no
screenshot: `--max-frames`/`--max-frames-ss` were compiled out (`#ifdef HAVE_SCREENSHOTS`),
and `tools/build-retroarch.sh` now defines it, so the frontend builds and links with the
options present. They still do not fire: `runloop.h`'s `RUNLOOP_TIME_TO_EXIT` compares
`max_frames` against the **core's** `frame_count`, and this title loads no content, so the
counter never advances - three runs with `--max-frames=200` ran the full watch window and
wrote no `shot.png`. `src/main.cpp` reads `/app0/args.txt` (one argument per line) so a run
can be given those options without the launcher, verified by the mark
`argv extras from /app0/args.txt = 3`.

## Next

1. **Hook the path the menu actually takes.** The `--max-frames-ss` block is not reached
   on a content-less run: it compares `video_st->frame_count`, which only
   `video_driver_frame` increments, and the menu is drawn through
   `video_driver_cached_frame` (patch 0029 tried a runloop-side counter and had no effect
   on the console, so it was removed). Put the counter and the `take_screenshot` call
   (`tasks/task_screenshot.c`) in `video_driver_cached_frame`, take the shot, read it back
   over FTP and look at it (`klog/shot-*.png`). The readback is the Vulkan driver's own
   `vulkan_readback`/`VK_FLAG_READBACK_PENDING` path, so it tests one more driver entry
   point too.
2. **Then the init refusals**: triangle strips at init (the topology patch 0016 applies to
   the chain's quad) and the blank texture's compute upload (its staging texture could
   match its destination, as 0027 does for the menu texture).
3. **`/app0/retroarch.log` is still the stale 1200 bytes** from an earlier round: the
   frontend's file logger is set up (`--log-file=/app0/retroarch.log`) but never flushes,
   and a run that exits by itself would flush it - which is the same exit the screenshot
   step needs.
4. **Config loading is still parked** (`parked/config-path.patch.py`, step 0006): the
   command line passes `-c /app0/retroarch.cfg` and the port does not yet prove the file
   is read.
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
