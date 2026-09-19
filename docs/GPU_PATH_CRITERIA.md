# The GPU path: acceptance criteria

Scoped before any code is written, because the previous path taught this project
what "done" has to mean before the work starts rather than after. Every criterion
names how it is verified, and the verification is a command or an artifact - not an
opinion.

## What this replaces, and what it must not break

The title renders today through `video_ps5` (`src/video_ps5.cpp` +
`src/display.cpp`): RGUI's 320x240 RGB565 framebuffer is converted and scaled on the
CPU, one pixel at a time, into a tiled 1920x1080 framebuffer in direct memory, and
handed to the display with `sceVideoOutSubmitFlip`. The menu is visible on the
console. **That path stays working throughout.** Every criterion below is additive:
if the GPU path is not yet better, `video_ps5` is still the driver that runs, and
the menu is still on screen.

## A - the 40 refusals, cleared

The linked libps5vk refuses this frontend's commands, and a refusal ends recording
with an error at `vkEndCommandBuffer`, so the command buffers never submit and the
screen stays black while every call returns success. The maintainer identified all
three sources and the frontend-side fix for each.

| # | Criterion | Where | How it is verified |
| --- | --- | --- | --- |
| A1 | No pipeline is created with `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP` | `vendor/retroarch/gfx/drivers/vulkan.c:2620`, `:2649`, `:2765` - the three sites that select a strip topology | `grep -c TRIANGLE_STRIP` over the configured `gfx/drivers/vulkan.c` is 0 in any `input_assembly.topology` assignment, and the trace has **zero** `only triangle lists without primitive restart` lines |
| A2 | The RGUI upload does not use a storage image | `vulkan_copy_staging_to_dynamic` chooses compute when `dynamic->format != staging->format` (`:1099-1104`); the compute path writes a `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE` at binding 3 (`:1150-1156`) | the trace has **zero** `descriptor type 3 has no proven table entry` lines |
| A3 | Every sampler the frontend creates uses clamp-to-edge on all three axes | `vendor/retroarch/gfx/drivers_shader/shader_vulkan.cpp:2034-2036` (hardcoded REPEAT) and `:2087-2108` (preset-driven, mapping to repeat / mirrored-repeat / clamp-to-border / mirror-clamp) | the trace has **zero** `sampler address modes` lines |
| A4 | The refusal count is zero, not merely lower | all of the above | `grep -c '^vulkan: ' /app0/trace.txt` is **0** over a full run |

A2 is a decision, not only a patch, and **the first decision was wrong**. It was
settled as "request RGB8888"; reading the menu's format chooser shows that cannot
work. Corrected as follows.

**Why RGB8888 cannot work.** RGUI picks its output pixel format from the *video
driver's identity*, not from any request:
`rgui_set_pixel_format_function` (`menu/drivers/rgui.c:1359-1394`) switches on
`video_driver_get_ident()` and falls through to `argb32_to_rgba4444` for any driver
it does not name - and it names ps2, gx, psp1, rsx, d3d10/11/12, sdl_dingux, sdl_rs90
and xvideo, not "ps5". So this port's menu is produced as **RGBA4444, 16 bits per
pixel**, and there is no RGB8888 path in RGUI at all. Requesting RGB8888 would leave
the menu's buffer 16-bit, the dynamic and staging formats would still differ, and the
compute path would still fire.

**The fix that follows.** Make the two texture formats match by using the format the
menu actually sends. `vulkan_set_texture_frame` already computes it - `fmt` becomes
`VK_FORMAT_B4G4R4A4_UNORM_PACK16` for a non-rgb32 frame (`gfx/drivers/vulkan.c`, the
`if (!rgb32)` branch). The corrected change is in the same function: allocate the
staging texture in that same remapped format, so `dynamic->format == staging->format`
and `vulkan_copy_staging_to_dynamic` takes its `vkCmdCopyBufferToImage` branch.

**The quality objection to the earlier decision does not apply.** I rejected the
matching-format route because it "stores the menu in B4G4R4A4, four bits per
channel". That is what RGUI *already produces* - `argb32_to_rgba4444` - and what the
optimal texture already holds. The staging texture being 32-bit was the odd one out,
not the thing to preserve. Matching it is not a quality loss; it is the removal of a
conversion nothing needed.

**Corrected A2 criterion, first decision kept for the record: request RGB8888.**

The menu is sent as RGB565 (`rgb32 == false`), and `vulkan_set_texture_frame`
answers that by remapping `fmt` to `VK_FORMAT_B4G4R4A4_UNORM_PACK16` while the
staging texture is allocated in the 32-bit `fmt` it started as - so the two formats
differ and `vulkan_copy_staging_to_dynamic` (`gfx/drivers/vulkan.c:1098`) takes the
compute path, which writes the storage image at binding 3 that libps5vk refuses.

The alternative - allocate the staging texture in the remapped format so the formats
match - would also clear the refusal, but it stores the menu in **B4G4R4A4: four
bits per channel**, and RGUI's palette and antialiased text would visibly degrade.
Requesting RGB8888 makes the menu 32-bit end to end, so the formats match, the plain
`vkCmdCopyBufferToImage` path runs, and the picture is full 8-bit colour.

The risk to watch is that `SET_PIXEL_FORMAT: RGB8888` is a frontend-wide setting; if
it has side effects, the remapped-staging fallback is the alternative and the
quality tradeoff gets recorded here rather than hidden.

## B - the menu reaches the screen through the GPU

| # | Criterion | How it is verified |
| --- | --- | --- |
| B1 | The title runs on `video_vulkan`, not `video_ps5` | `/app0/trace.txt` shows RetroArch's Vulkan driver initialising, and `ps5_init entered` (this project's own driver) is **absent**, because only one video driver runs |
| B2 | The frame is presented through libps5vk's swapchain | `vkQueuePresentKHR` returns `VK_SUCCESS` and the driver's flip marker is reached; a presentation count appears in the trace |
| B3 | No command buffer ends in error | the trace carries no refusal line (A4) **and** no `vkEndCommandBuffer` failure is reported; this is the specific fault that produced a black screen with successful presents before |
| B4 | The console owner confirms RGUI is visible, rendered through the GPU | the owner's confirmation, which is the only witness for "on the screen" this project accepts |

## C - it is actually better, measured

The reason for doing this at all. A GPU path that renders the menu no faster than
the CPU path is not worth the swap, so it has to be shown to be faster.

| # | Criterion | How it is verified |
| --- | --- | --- |
| C1 | The per-frame CPU cost of presentation is gone | the per-pixel loop in `src/video_ps5.cpp`'s `ps5_frame` does not run under Vulkan; evidenced by the absence of the driver's frame marks and by the frontend's own frame timing |
| C2 | Frame rate at the menu is at least as good as the CPU path | measured frame count over a fixed `--watch` window, compared against the CPU path's number for the same window |
| C3 | A real core runs at its target rate | load a libretro core, run it for a fixed window, and record frames presented versus frames expected. This is the number the whole exercise is for |
| C4 | The result is reversible | `video_ps5` remains registered and selectable, so a regression in the GPU path can be undone by one line in `tools/apply-port-patches.py` |

C3 needs a core in the title folder. If no core is available when the rest is done,
C3 is recorded as **not measured** rather than inferred - the previous rounds
established what happens when an unverified assumption is treated as a result.

## D - the invariants hold

| # | Criterion |
| --- | --- |
| D1 | `../PS5_Vulkan` is not modified. Every change is in this tree. |
| D2 | `tools/verify.sh` passes: format, unit, build, integration, evidence. |
| D3 | The port's changes to upstream remain named edits in `patches/series`, applied by `tools/apply-port-patches.py` to `build/ra-conf` only; `vendor/retroarch` is never edited. |
| D4 | Every step is verified by a command or an artifact, committed with its evidence, and written up in `docs/ACTIVE.md` and `docs/FINDINGS.md`. |

## Out of scope

- Audio. The frontend already reports `Failed to initialize audio driver. Will
  continue without audio.` and that is unchanged by this work.
- Shaders, filters, overlays and the XMB menu, beyond what comes free with the
  Vulkan driver. A1-A4 gate on the menus this project actually renders.
- Input. The pad driver already opens and delivers presses; nothing here changes it.
- Asking the maintainer for the `V0-sampler` or `V0-topology` probes. Every
  criterion above is reachable from this side. If A3 turns out to need repeat or
  mirrored-repeat sampling rather than clamp-to-edge, that is a scope change and
  comes back to the `docs/ACTIVE.md` open list rather than being assumed.

## Order of work

1. A1, A2, A3 as named patches, then one run. A4 is the gate: zero refusals.
2. Only then B - switch the compiled default to `vulkan`, one run, owner confirms.
3. Only then C - measure, because measuring before the path works measures nothing.

If A4 cannot be reached, the work stops there, the CPU path stays, and what was
learned is recorded. The menu on the screen is not put at risk for an optimisation.

## Where this stands now

A4 and B1 are met. A1, A2 and A3 were each fixed and the refusals are gone: the
trace carries no `vulkan: ` line at all.

**The A2 decision recorded above was right for the wrong reason, and the criterion
it names was wrong about the mechanism.** The storage-image refusal did not come
from an upload taking its compute branch. It came from `vkCreateComputePipelines`:
libps5vk's `ps5vk_descriptor_options` walks every set-0 binding whose stage flags
match the stage being compiled and refuses any whose stride is zero, a storage
image's stride is zero by design, and this frontend's shared set declares a
compute-only storage image at binding 3. So the fix is not a format decision at
all - patch 0027's name-the-one-format change had already made the formats match
and the compute branch was already unreachable. The fix is to stop compiling the
upload shader (0036): the branch it serves would fail its own `retro_assert`.

The lesson is in the criteria's own terms: "request RGB8888" and "match the menu's
format" were both answers about a path that was not being taken. The refusal named
the binding, not the upload, and reading it as an upload fault cost two rounds.

**What B actually needed was five more faults, none of them about formats.** They
are listed in `docs/ACTIVE.md` and each has a patch: widgets claiming the driver
before the display context is resolved (0042); nothing ever enabling the menu
texture, because the frontend's only `true` is a libretro concept this content-less
title never reaches (0046); the optimal menu image never being copied into after
the first handover (0051); a stale `/app0/args.txt` ending every run by itself
(0043); and the driver's own menu draw living in `vulkan_draw_quad` rather than in
the display-list `gfx_display_vk_draw` that was instrumented first.

**B2, B3 and B4 remain unmet.** The screen is black with the menu texture filled,
the quad drawn, and no error anywhere. That is where the work stops for now, and
the next step is the driver's own draw path in `../PS5_Vulkan`, not another
frontend gate.
