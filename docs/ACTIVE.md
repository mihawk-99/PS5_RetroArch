# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-19_

## Now

**Two paths run; the CPU one is on screen and the GPU one is not yet.**

`video_ps5` still puts the RGUI menu on the television and is untouched by all of
this work. `video_vulkan` now initialises, presents ~45 frames a second, records
the menu texture, uploads it and draws the menu quad every frame - and the screen
is black. The owner has confirmed the black screen, and there is no refusal, no
failed call and no error line to explain it.

## The GPU path, as measured

`docs/GPU_PATH_CRITERIA.md` holds the acceptance criteria. A4 and B1 are met:

- **A4 - zero refusals.** The trace carries no `vulkan: ` line at all. Three
  named causes were cleared: every pipeline is created with a triangle list, every
  sampler asks clamp-to-edge, and no compute pipeline is compiled (libps5vk
  refuses any set-0 binding with stride 0, and this frontend's shared set declares
  a compute-only storage image at binding 3, so the unused upload shader was
  refused once per pipeline initialisation).
- **B1 - the driver is the Vulkan one.** The trace carries
  `video driver: video_vulkan init entered` and no `ps5_init entered`.

B2, B3 and B4 are **not** met. The screen is black.

## Seven faults, all silent, all fixed

Each one produced a clean trace and a black screen. In the order they were found:

1. **Widgets claimed the driver.** `vulkan_gfx_widgets_enabled` answered true, so
   the frontend took the widgets branch of its mutually exclusive `if/else` and
   never called `gfx_display_init_first_driver` - the only thing that sets
   `p_disp->dispctx`, without which `gfx_display_draw` returns immediately.
   (0042)
2. **Nothing enabled the menu texture.** The driver draws the menu only behind
   `VK_FLAG_MENU_ENABLE`, and the frontend's only `true` is in
   `display_menu_libretro` - a libretro concept, set when a core runs. This title
   launches straight into the menu with no content, so it was never set. (0046)
3. **The menu texture was never filled.** In `vulkan_set_texture_frame`, when the
   optimal texture already existed, the staging texture was flushed to the GPU and
   never copied into the image the draw samples; the copy lives only in
   `vulkan_frame`'s upload block and is the sole writer of that image. (0051)
4. **A stale `/app0/args.txt` ended every run.** A leftover capture file from an
   earlier probe made the run take a picture and then quit itself 90 frames in,
   which the console books as an application crash with a coredump and which looks
   from the sofa like a title that never appeared. The capture block no longer
   calls `CMD_EVENT_QUIT`, and `tools/run-title.sh` clears the file before every
   run. (0043)
5-7. `docs/FINDINGS.md` carries these with their traces.

## What is true at the moment of the draw

From the instrumented trace, once per frame:

    vulkan set_texture_frame: rgb32=0 320x240 frame=yes
    vulkan copy_staging_to_dynamic: dynamic 320x240 fmt=37 type=2, staging fmt=37 type=1, compute=0
    vulkan menu state: flag=1 idx=0 staging(img=0 buf=1) optimal(img=1 buf=0)
    vulkan draw_quad 0: texture=yes image=yes layout=5 320x240 pipe=yes

So: the menu hands its framebuffer over, the formats match, the copy to the
sampled image runs, and the quad is drawn with a valid pipeline, a valid image and
`VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`. The pixels are black anyway.

## Next

1. **The driver's own draw, in `../PS5_Vulkan`.** Everything on this side is now
   instrumented and correct, so the next question is what `ps5vk_CmdDraw` does
   with the menu quad: the AGC stream it writes, the viewport it programs, and
   whether the draw reaches the tiled framebuffer the swapchain image lives in.
   `ps5vk_draw.c` is the file. Read it, then instrument it the same way - probes
   on this side have been wrong three times about where the picture stops.
2. **Then B4**: ask the owner to look. That confirmation is the only witness for
   "on the screen" that this project accepts, and it has said "black" three times.
3. **Then C**: measure. C2 compares frame rate against the CPU path's number for
   the same window; C3 needs a core and is recorded as not measured without one.
4. **Then retire the probes.** Fifteen diagnostic marks are now permanent in the
   patch set (0046-0053). They are the reason this was findable and they are not
   shippable: gate the useful ones behind a flag and delete the rest.

## Working notes

- **`bash tools/run-title.sh` is the only way to run the title.** It owns the whole
  sequence and prints the title's own trace. Do not hand-deploy.
- **The probes have been wrong about where the picture stops, three times.** Each
  time the trace was clean and the conclusion drawn from it was false. Instrument
  the next step in the chain and read it, before concluding.
- **`gfx_display_vk_draw` is not the menu's draw.** It is the display-list path.
  This driver composites the menu itself in `vulkan_frame` through
  `vulkan_draw_quad`. A probe there reports nothing and proves nothing.
- **`/app0/...` is not an FTP path.** The title's files are at
  `/data/homebrew/PPSA99169/`.
- **The console's `/app0/args.txt` is read by the driver too**, and deploy never
  deletes anything, so a file left there changes later runs. `run-title.sh` clears
  it on every run; if a run behaves oddly, check it first.
- **`/app0/retroarch.log` does not exist to be read** (FTP answers "no such
  file"), so the trace is the only record a run leaves.
- **Assertions only speak because stderr is unbuffered.** If that line vanishes
  from the trace, check `src/main.cpp`'s `setvbuf` before anything else.
- **A probe is not gone until the objects are.** Reverting the probe set leaves the
  strings in `build/ra/obj`; `rm -rf build/ra/obj` is the fix.
- **An insert must not repeat its anchor.** `tests/test_frontend.py`'s `ProbeSet`
  checks every probe for that, for a marker in its own text, for a note and for a
  trailing newline.
- **Do not hand-edit the configured tree for real changes.** Diff it against
  `vendor/retroarch` if it is damaged; every port change belongs in
  `tools/apply-port-patches.py`.
- **The patch count is pinned** in `tests/test_frontend.py`. It is 64.
- **`vendor/` is not in the diff.** Anything a build needs from it must be
  regenerable or live in a tracked directory.
- **`src/` is linked before the archives**, so a definition there wins over the
  same symbol in the frontend.
- **Console capture landmines.** `retroarch.log` may be from any run; the cfg's
  `video_driver` is never parsed on a content-less run.
- **The flip-status marker reads 1 forever** on this console while the buffers
  rotate correctly, so it is not a usable instrument.
- **`../PS5_Vulkan` is never modified.** Its maintainer answers questions and has
  been accurate and decisive.
