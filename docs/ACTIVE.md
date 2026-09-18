# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-18_

## Now

**The menu is on screen and the title runs; the pad does not work yet.** An
unattended `bash tools/run-title.sh --watch 12` runs the title, shows the RGUI menu
and is closed by the script with no fatal signal. `bash tools/verify.sh` passes all
five gates and the host suite is 13 tests.

**A complete input driver for this console exists and is registered.** It is
`src/input_ps5.cpp`: the DualSense is read with
`scePadInit`/`scePadOpen`/`scePadRead` and the 120-byte sample layout
`../ProsperoLight` verified on hardware, its button words are mapped onto
RetroArch's numbering (CIRCLE is RetroArch's A, so CIRCLE confirms a menu entry),
and sticks and triggers are reported as axes. `tests/test_frontend.py` pins both the
registration and the pairing. What is missing is that the frontend never gets as
far as calling it.

**Two faults stood in front of it, and both are fixed.**

1. **RetroArch's built-in test input driver was on** (`HAVE_TEST_DRIVERS=yes` by
   default), and while it is on `video_driver_init_input` returns before
   initialising any real input driver. `--disable-test_drivers` is now in
   `tools/retroarch-sources.sh`.
2. **The title's `-c` never reached RetroArch.** Content loading rebuilds argv from
   the frontend's environment and kept only `["retroarch", "--menu"]`, so the config
   file was never read and the title ran on compiled defaults -
   `probe config_parse_file: path="(null)"`. A guarded fix restores the path.

**That fix is parked, because it exposes a crash that is not yet understood.** With
the config actually read, the title dies on launch: `SIGSEGV`, `rip=0`, a call
through a null function pointer - with `input_driver="ps5"` and equally with
`"null"`. Markers show the whole video path completing, so the fault is after driver
initialisation. The change and its reasoning are in `parked/config-path.patch.py`;
`config/retroarch.cfg` keeps `input_driver = "null"` so the title keeps running.

## Next

1. **Fix the config-path crash, then turn the pad on.** The driver is written,
   registered and tested; the only thing between it and a working controller is that
   crash. Instrument `runloop_iterate` and the content task, not the driver: the
   driver is never entered.
2. **Then answer the frozen-frame question.** The menu redraws only when something
   changes, which is why it looks still today; `ps5_frame ... menu commits=N
   changes=M` in the trace is the instrument that will say whether input makes it
   redraw, and the commit/change counters are already in the driver.
3. **Then the menu's size.** RGUI renders 320x240 into a 1920x1080 frame and the
   driver scales it up whole.

## Working notes

- **`bash tools/run-title.sh` is the only way to run the title.** It owns the whole
  sequence and prints the title's own trace. Do not hand-deploy.
- **Trace files are append-only and grow without bound.** One session took
  `/app0/trace.txt` past 70,000 lines. Read the tail, or slice from the last
  `main() entered`.
- **A probe is not gone until `build/ra/obj` is deleted too.** Reconfiguring alone
  reuses stale objects and ships old probes into `eboot.bin`.
- **Do not patch the configured tree with multi-line scripted replacements.** Two
  rounds were lost to a replaced brace that produced
  `undefined symbol: input_config_reset` at link time. Single-line insertions,
  verified by brace count, are safe; blocks are not.
- **The console's signal block prints `rip`.** `rip=0` is a call through a null
  function pointer, not a bad data read, and reading it first saves a round.
- **`src/` must be compiled with the frontend's `-D` flags**, which
  `tools/build-title.sh` does by asking `tools/retroarch-flags.sh`; a bare
  `make app` produces a driver table the frontend reads at the wrong offsets.
- **Compile from `build/ra-conf`, never from `vendor/retroarch`**; upstream is never
  edited and `patches/series` is the only record of this port's changes.
- **`-DHAVE_MAIN` must never be defined**: it compiles the frontend's main loop out.
- The console's address and credentials come from the ignored `.env`; console
  captures stay in the ignored `klog/` tree.

## Last verified

| Check | Result |
| --- | --- |
| `bash tools/run-title.sh --watch 12` | PASS: title runs, RGUI menu visible, no fatal signal, closed by the script |
| `bash tools/verify.sh` (all gates) | PASS (format unit build integration evidence) |
| `python3 -m unittest discover -s tests` | PASS: 13 tests, including the input driver's registration and button map |
| `nm -S build/obj/src_input_ps5.cpp.o` | PASS: `input_ps5` is 0x58 bytes, the frontend's `sizeof(input_driver_t)` |
| `nm -u build/ra/obj/input_input_driver.c.o` | PASS: the frontend references `input_ps5` |
| Input driver selected on the console | **FAILS**: `SIGSEGV`, `rip=0`, before `ps5_input_init`; parked |

## Open findings

- The config-path crash above is the blocker for input, and therefore for the
  frozen-frame question. It happens with any input driver selected, so it is the
  config being read - not the driver - that exposes it.
- The console's FTP will not replace `sce_module/libc.prx`; the title runs against
  the console's copy. A release has to solve this.
- RGUI's assets: the fonts are bundled under `assets/rgui/font/`; whether the rest
  of RetroArch's asset tree should ship is undecided.
- A pad-driven build cannot be verified unattended yet: the trace reports the first
  press, but nothing in the pipeline exercises a button.

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
