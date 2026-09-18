# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-18_

## Now

**The RGUI menu is on the console's screen.** `bash tools/run-title.sh --watch 20`
builds, publishes, verifies, launches, watches and closes the title in one
unattended command; the console reports no fatal signal, and the console's owner
watched the menu on the television during the run. The title's own trace is
committed as `evidence/ppsa-99169-rgui-menu-on-screen/`, and `bash tools/verify.sh`
passes all five gates. Pad input was out of scope and is still not wired.

**The end of the frame path.** The driver opens VideoOut at 1920x1080, tells the
frontend the size with `video_driver_set_size`, and presents by flushing the back
buffer, submitting a flip to that buffer's registered index, waiting a vblank, and
alternating buffers. RGUI renders the menu into its own 320x240 RGB565 framebuffer
and hands it over through `poke->set_texture_frame`; the driver scales it into the
console's frame through the tiled addressing. The bands that were this project's
only instrument are still painted underneath, so a black screen can still be told
apart from a menu that is not drawing: they vanish as soon as a menu frame exists.

**Two faults were behind the black screen, and they were independent.**

1. **The title and the frontend disagreed about the size of RetroArch's driver
   interface struct.** `src/` was compiled with no `-DHAVE_*` flags; the archive is
   compiled with fifty. `video_ps5` was 136 bytes where the frontend read 144, so
   `poke_interface` and `wrap_type_to_enum` were both read one member late and both
   came back NULL. The frontend calls `poke_interface` only when it is not NULL, so
   RGUI's hand-over was dropped in silence on every frame while the driver
   presented happily. `tools/build-title.sh` now passes `tools/retroarch-flags.sh`'s
   own list to the title's sources, and `tests/test_frontend.py` fails on any
   recurrence by comparing the two sizes.
2. **`sceAgcInit(8)` was called before the display opened**, left over from the
   round that submitted flips through an AGC command buffer.
   `../PS5_Vulkan/src/demo_renderer.cpp`, whose output has been seen on this
   console, contains no `sceAgc` call. Removing it produced the first pixels this
   port ever put on the screen.

**A correction to the lead this round started from.** Holding the frame inside
`present()` instead of returning into RetroArch's runloop was expected to be the
fix. It is not: the bands appeared immediately - before the hold began - and were
still up twelve seconds after `present()` had returned into the runloop, with the
display's flip status at `marker=1` throughout. The runloop does not take a
presented frame back. See `docs/FINDINGS.md` for the timing and for what the run
does and does not separate.

## Next

1. **Input.** The driver owns no input and hands it back to the frontend, which has
   no joypad driver on this console (`primary_joypad` is NULL; `patches/series` 0004
   stops the menu crashing on it). A pad path is the next milestone, and it is what
   makes the menu navigable rather than merely visible.
2. **The menu is 320x240 in a 1920x1080 frame.** That is what RGUI's framebuffer is
   and the driver scales it up whole. `menu_rgui_internal_upscale_level` and the
   RGUI aspect-ratio settings are the frontend's own knobs for this, and the
   driver's blit is nearest-neighbour on purpose.
3. **`sce_module/libc.prx` still cannot be replaced over FTP** (see Open findings):
   the console runs its own copy. Harmless today, a real gap for a release.

## Working notes

- **`bash tools/run-title.sh` is the only way to run the title.** It owns the whole
  sequence, including starting the klog listener before the launch and closing the
  title itself, and it prints the title's own trace. Do not hand-deploy.
- **Nothing this project's `src/` compiles may be built without the frontend's
  defines.** `tools/build-title.sh` is the only correct way to build the title: it
  asks `tools/retroarch-flags.sh` for the flags the archive uses and passes them
  through `APP_DEFINITIONS`. A bare `make app` produces a driver table the frontend
  reads at the wrong offsets, and the only symptom is a menu that never appears.
- **A probe is not gone until the object is.** Deleting `build/ra-conf` and
  reconfiguring is not enough: `tools/build-retroarch.sh` reuses
  `build/ra/obj/*.o` and will archive a stale object carrying yesterday's probes
  into `eboot.bin`. Delete `build/ra/obj` as well when a probe must really go.
- **`/app0/trace.txt` is append-only and is never rotated.** Every run adds to it;
  one session took it to 72,904 lines. Read the tail, or slice from the last
  `main() entered`, rather than reading it whole.
- **Compile from `build/ra-conf`, never from `vendor/retroarch`.** The configured
  tree is rebuilt from upstream plus the four patches in `patches/series`; upstream
  is never edited.
- **`-DHAVE_MAIN` must never be defined**: it compiles the frontend's main loop out.
- The console's address and credentials come from the ignored `.env`; anything read
  from the console stays in the ignored `klog/` tree.

## Last verified

| Check | Result |
| --- | --- |
| `bash tools/run-title.sh --watch 20` | PASS: no fatal signal, menu on screen, `ps5_frame 1: menu 320x240 pitch=640 present=1`, `display: flip 1200 of buffer 1, status=0 marker=1` |
| `bash tools/verify.sh` (all gates) | PASS (format unit build integration evidence) |
| `python3 -m unittest discover -s tests` | PASS: 11 tests |
| `tests/test_frontend.py` ABI check, built without the defines | FAILS as it must: `video_ps5 is 136 bytes ... video_driver_t is 144` |
| `nm -S build/obj/src_video_ps5.cpp.o` | PASS: `video_ps5` is `0x90` bytes, the frontend's `sizeof(video_driver_t)` |
| `diff -rq build/ra-conf vendor/retroarch` | PASS: only the four files `patches/series` names differ; no probes |
| `python3 tools/evidence.py compare evidence/` | PASS: 3 captures replayed, 0 failed |

## Open findings

- The console's FTP will not replace `sce_module/libc.prx`: four uploads, a fresh
  filename and a full listing all served the console's own 1,335,962-byte file.
  `tools/deploy-title.py` reports it and carries on, so the title runs against the
  console's copy. It has to be solved before anything ships.
- RGUI's assets: the menu reads `ASSETS_DIR`, compiled as `/app0/assets`, and the
  RGUI fonts are bundled under `assets/rgui/font/`. Whether the rest of RetroArch's
  asset tree should ship with the title is still undecided.
- The blit is nearest-neighbour for both formats and converts RGB565 per source
  pixel. If a core ever presents a 1080p frame, the line-by-line scale is the part
  to look at first.
