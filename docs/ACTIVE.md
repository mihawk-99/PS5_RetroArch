# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-19_

## Now

**The RetroArch menu is on the console's screen.** `PPSA99169` runs this project's
own `video_ps5` driver, receives RGUI's 320x240 framebuffer, presents it, and the
console owner confirms the menu is visible on the television. That is the objective
the project was built for; `docs/FINDINGS.md` carries the trace and the captures.

One unattended `bash tools/run-title.sh --watch 20` run is the evidence: one EXEC,
zero fatal-signal lines, the script launching, watching and closing it itself.
`/app0/trace.txt` from that run:

    ps5_init: told the frontend the display is 1920x1080
    input: pad opened, user=515310723 handle=51447552
    display: flip 1 of buffer 0, status=0 marker=1
    ps5_set_texture_frame: rgb32=0 320x240 frame=present have=1 (was 0)
    menu: framebuffer commit 1 is a new picture (1 of 1 changed so far)
    ps5_frame 1: menu 320x240 pitch=640 present=1
    ps5_frame 600: menu commits=3 changes=2 presented=yes

The framebuffer changed twice, once immediately after a pad press, so the pad
reaches the menu and the menu redraws.

**What unblocked it was one anchoring mistake, twice.** The video driver is chosen
by `config_get_default_video()`, whose switch is over `VIDEO_DEFAULT_DRIVER`; this
build has `HAVE_VULKAN`, so it returns at `case VIDEO_VULKAN:` and never reaches the
`case VIDEO_NULL:` arm patch 0010 was editing. And the driver-table edit inserted
`&video_ps5` *after* the `#ifdef HAVE_VULKAN` block, leaving `&video_vulkan` at
index 0 - which is RetroArch's fallback when a name is empty or unfindable. Both are
fixed: the patch anchors on `case VIDEO_VULKAN:` and returns `"ps5"`, and
`&video_ps5` is inserted before the block.

**Left open, and recorded.** The flip status marker reads 1 at flips 1, 300, 600,
900 and 1200 - never advancing - while the buffers rotate correctly and the frames
are on screen. The marker is not a usable instrument on this console.

## Next

1. **Run the capture - it is built, waiting, and now ends by itself.** The port now reads the frame with the
   driver's own `vulkan_read_viewport` and writes a PPM itself (patches 0031/0032,
   committed as `949fdb9`; the frontend builds 276 of 276 and the path is in the image).
   It has **not** been run: the console stopped answering ("No route to host") before the
   run, and the run that was attempted failed at its deploy step for that reason. Arm
   `/app0/args.txt` with `--ps5-capture=90` and `--ps5-capture-path=/app0/shot.ppm`, run
   `tools/run-title.sh --no-build --watch 18`, fetch `/app0/shot.ppm` and look at it. The
   trace line says `viewport WxH, read_viewport -> ok|failed`; patch 0033 then asks
   the frontend to quit, which should finally write `/app0/retroarch.log` (still the
   same 1200 bytes, because the title has always been killed rather than exiting).
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
