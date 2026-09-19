# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-18_

## Now

**The Vulkan driver is linked into the title, and the link completes.** This is the
pivot the project was for: `../PS5_Vulkan`'s driver is no longer something RetroArch
dlopens - a PS5 title cannot, which that project measured - but ordinary symbols in
`eboot.bin`. `bash tools/build-title.sh` reaches a clean link and the artifact grew
from 8,225,706 to 29,859,722 bytes, which is the driver and its shader compiler.

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

## Next

1. **Find what returns 1 in the Vulkan build.** Probes at statement level inside
   `drivers_init` after the audio block and inside `rarch_main`'s content-load push,
   through `tools/apply-runtime-probes.py` so they stay revertible. The answer decides
   whether this is a Vulkan-init problem or the content-load path the parked 0006
   already covers.
2. **Then the config file.** Content loading discards the title's `-c`, and the fix
   (0006) is parked in `parked/config-path.patch.py` because reading the config crashes
   the launch inside `command_event`. Until it lands, `video_driver` comes from the
   compiled default and nothing in `config/retroarch.cfg` applies.
3. **Then the menu's size.** RGUI renders 320x240 into a 1920x1080 frame.

## Working notes

- **`bash tools/run-title.sh` is the only way to run the title.** It owns the whole
  sequence, including starting the klog listener before the launch and closing the
  title itself, and it prints the title's own trace. Do not hand-deploy.
- **`vendor/` is not in the diff.** Anything a build needs from it must either be
  regenerable or live in a tracked directory; `tooling/ps5-stubs/` is the second case.
- **A link that fails on twenty symbols stops there.** `--error-limit=0` is on the
  link line, and it turned three rounds of guesswork into one list of 24.
- **Runtime probes are kept: `tools/apply-runtime-probes.py`.** A rebuild wipes any
  probe written into `build/ra-conf`, so they live in that script - applied with one
  command, `--revert` to remove, `--list` to see what each one is for. They are not in
  the shipping build; `tools/build-title.sh` does not call it.
- **Never grep a compiled object for a patch's comment text**, and remember
  `strings(1)` ignores anything shorter than four characters. The instruction stream
  (`objdump -dr`) is the only reliable evidence that a code change compiled.
- **Place probes at statements, never inside a multi-line call or an `#if` block.**
- **A probe is not gone until `build/ra/obj` is deleted too.** Reconfiguring alone
  reuses stale objects and ships old probes into `eboot.bin`.
- **`src/` must be compiled with the frontend's `-D` flags**, which
  `tools/build-title.sh` does by asking `tools/retroarch-flags.sh`. A bare `make app`
  produces a driver table the frontend reads at the wrong offsets.
- **Compile from `build/ra-conf`, never from `vendor/retroarch`**; upstream is never
  edited and `patches/series` is the only record of this port's changes.
- **`-DHAVE_MAIN` must never be defined**: it compiles the frontend's main loop out.
- The console's address and credentials come from the ignored `.env`; console
  captures stay in the ignored `klog/` tree.

## Last verified

| Check | Result |
| --- | --- |
| `bash tools/run-title.sh --watch 20` on this build | **FAILS**: the title launches, then `rarch_main returned = 1` eight times with no video init, no frame, no signal; `klog/console-retroarch.log` stops after the audio fallback |
| `bash tools/build-title.sh` | PASS: clean link with the driver embedded; `eboot.bin` 29,859,722 bytes (was 8,225,706); 0 undefined symbols |
| `bash tools/build-mesa-util.sh` | PASS: `u_thread.o`, `anon_file.o`, `os_file.o`, each checked to define the symbols it is for |
| `bash tools/verify.sh` (all gates) | PASS (format unit build integration evidence) |
| `python3 -m unittest discover -s tests` | PASS: 18 tests, including the AGC import surface and the unwind boundaries in the linked image |
| Config path (0006) | **PARKED**: reading the config still crashes in `command_event`; see `parked/config-path.patch.py` |

## Open findings

- **`../PS5_Vulkan`'s PS5 object list has a hole**: `libpsbc_driver.ps5.a` references
  eight symbols nothing in that tree defines. Its `libvulkan.so.1` links only because a
  shared link may leave symbols undefined, so the hole is invisible there. This
  project compiles them instead of reimplementing them; if that project adds
  `u_thread.c`, `anon_file.c` and `os_file.c` to the archive, `tools/build-mesa-util.sh`
  can go.
- The config-path crash is the last thing between this title and a readable
  `retroarch.cfg`, and with it the file logger.
- The console's FTP will not replace `sce_module/libc.prx`; the title runs against the
  console's copy. A release has to solve this.
- RGUI's assets: the fonts are bundled under `assets/rgui/font/`; whether the rest of
  RetroArch's asset tree should ship is undecided.
- Pad input cannot be verified unattended yet: nothing in the pipeline presses a
  button.
