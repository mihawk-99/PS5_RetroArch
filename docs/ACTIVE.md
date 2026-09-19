# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-18_

## Now

**The pad works and the menu is live.** `bash tools/run-title.sh --watch 20` with the
console owner working the pad: the title runs, the driver reads the DualSense, and
the menu answers. From the title's own trace on the shipping build:

    input: pad opened, user=515310723 handle=51119872
    input: press, pad=0x00004000 retropad=0x00000001     CROSS  -> RetroPad B
    input: press, pad=0x00002000 retropad=0x00000100     CIRCLE -> RetroPad A
    menu: framebuffer commit 2 is a new picture (2 of 2 changed so far)
    ps5_frame 600: menu commits=76 changes=14 presented=yes

Both goals of the input round are met. Input works, with CIRCLE confirming and CROSS
cancelling as a PlayStation player expects. And the picture is **not** one frozen
frame: 76 framebuffer commits, 14 of them a different picture, against 1 and 1 in
every earlier run. The image was never frozen - the menu had nothing to redraw for,
which is what input supplies.

**Why it never worked before: one comparison.** `ps5_input_init` had never run.
`video_driver_init_input` opens with `if (*input) return true;`, which upstream means
for a video driver that pre-initialised an input driver of its own, and
`video_driver_init_internal` assigns `tmp = current_driver` *before* calling
`video_driver_find_driver` - so after the pre-initialisation pass selects a driver,
`tmp` **is** that selection. The early return fired, the wrap below was dead code,
`current_data` stayed NULL and every button read answered 0. Measured:
`probe INV: entered tmp=ba41e0 *input=ba41e0 configured="ps5"`. `patches/series` 0009
fixes it with `if (*input != NULL && *input == tmp)`.

**A list of things that were NOT the reason**, each measured: the driver's code, its
registration, its button map, `HAVE_TEST_DRIVERS` (necessary but not sufficient), the
compiled input default 0008 (the driver was named; naming was never the problem), and
the controller-port guard 0007. Three rounds of probes placed *around*
`video_driver_init_input` could not see it; the probes inside it found it in one run.

## Next

1. **The config file is still not read.** Content loading discards the title's `-c`,
   and the fix for that (0006) is parked in `parked/config-path.patch.py` because
   reading the config crashes the launch inside `command_event`. That crash is
   narrowed to the dispatch and is the next task. The input driver does not depend on
   it any more - `0008` names it as the compiled default - so the pad works either
   way, which means this is now about configurability and the file logger, not about
   whether RetroArch runs.
2. **Then measurement before more features.** The pad cannot be verified unattended:
   the trace reports presses, but nothing in the pipeline presses a button. Deciding
   how (a recorded input file, or the console owner) is worth doing explicitly.
3. **Then the menu's size.** RGUI renders 320x240 into a 1920x1080 frame and the
   driver scales it up whole.

## Working notes

- **`bash tools/run-title.sh` is the only way to run the title.** It owns the whole
  sequence and prints the title's own trace. Do not hand-deploy.
- **Runtime probes are kept: `tools/apply-runtime-probes.py`.** A rebuild wipes any
  probe written into `build/ra-conf`, so they live in that script now - applied with
  one command, `--revert` to remove, `--list` to see what each one is for. They are
  not in the shipping build; `tools/build-title.sh` does not call it.
- **Never grep a compiled object for a patch's comment text**, and remember
  `strings(1)` ignores anything shorter than four characters. Both mistakes made
  correctly-built fixes look missing. The instruction stream (`objdump -dr`) is the
  only reliable evidence that a code change compiled.
- **Place probes at statements, never inside a multi-line call or an `#if` block.**
  Two rounds were lost to a split brace (`undefined symbol: rarch_main`) and to a
  probe inside a declaration list.
- **Do not park a patch by slicing the script by text.** It emptied the parked file
  and desynced the script from the tree; `git checkout` recovered it. Edit the
  script, then verify the parked file is non-empty.
- **`/app0/trace.txt` is append-only** and grows across runs; read the tail or slice
  from the last `main() entered`.
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
| `bash tools/run-title.sh --watch 20` | PASS: pad read (`input: pad opened`), 6 button mappings correct, `menu commits=76 changes=14`, no fatal signal |
| `bash tools/verify.sh` (all gates) | PASS (format unit build integration evidence) |
| `python3 -m unittest discover -s tests` | PASS: 13 tests, including the input driver's registration and button map |
| `python3 tools/apply-runtime-probes.py build/ra-conf` | PASS: all 7 probes apply; `--revert` removes them |
| Config path (0006) | **PARKED**: reading the config still crashes in `command_event`; see `parked/config-path.patch.py` |

## Open findings

- The config-path crash is the last thing between this title and a readable
  `retroarch.cfg`, and with it the file logger, which is the instrument the next
  debugging round will want.
- The console's FTP will not replace `sce_module/libc.prx`; the title runs against
  the console's copy. A release has to solve this.
- RGUI's assets: the fonts are bundled under `assets/rgui/font/`; whether the rest of
  RetroArch's asset tree should ship is undecided.
- Pad input cannot be verified unattended yet: nothing in the pipeline presses a
  button.

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
