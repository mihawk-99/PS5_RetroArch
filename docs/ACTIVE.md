# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-18_

## Now

**The title builds.** `bash tools/build-title.sh` compiles RetroArch's own sources
(225 objects), archives them, links them with `src/` through the native pipeline's
CRT, signs the result and assembles `dist/PPSA99169/`: **`eboot.bin` 8,026,239
bytes**, `container: signed, plaintext`, 12 segments, `integrity: valid`, plus
`sce_module/libc.prx` and the `sce_sys/` metadata. The folder's digests are in
`dist/PPSA99169/manifest.sha256` and `bash tools/check-manifest.sh` passes on it.

**The video driver is registered, and that is verified, not assumed.** The port's
changes to RetroArch live in `patches/series` and are applied by
`tools/apply-port-patches.py` to the configured copy under `build/ra-conf` —
never to `vendor/retroarch`. The evidence is a relocation, not a run:
`readelf -r build/ra/obj/gfx_video_driver.c.o` shows `.rela.data.video_drivers`
holding exactly two entries, `video_ps5` then `video_null`, and
`nm build/llvm-pie.elf` shows `video_ps5`, `video_null`, `rarch_main` and one
`main`.

**The entry point is reconciled.** RetroArch's `main` is compiled out with
`-DHAVE_MAIN`, the flag RetroArch's own desktop build passes; `src/main.cpp`
supplies `main` and calls `rarch_main` with `-f -c /app0/retroarch.cfg --verbose`,
and the SDK's `_start` remains the process entry. Two `main` symbols would be a
link error, and there is no link error.

**Everything the abandoned route left behind is gone.** The conversion tools, the
baseline tree, the ports cache, `tools/deploy.py`, `tools/console-launch.sh`,
`tools/check-payload.sh`, `tools/scaffold-native.sh`, `tools/build-native-app.sh`,
`tools/install-title.sh` and `tools/deploy.sh` are deleted; the tools that remain
are the ones `tools/build-title.sh` and `tools/verify.sh` actually call, and every
tool named by another tool exists.

**`RGUI on screen` is the goal, and it is not met.** The title is built and
manifested; it has not run.

## Next

1. The title folder has to reach `/data/homebrew/PPSA99169` on the console. The
   owner uploads it: **writes from this machine do not take.** Measured on
   2026-09-18: `tools/deploy-title.py` stored a 1,284,674-byte `libc.prx` and read
   back the *previous* 1,335,962-byte file under the new name, twice, and a probe
   uploaded under a name never used before read back those same old bytes. The
   console's FTP accepts the transfer and serves something else.
2. Then `tools/console-run.sh PPSA99169` and read the console's log from the mark.
3. Then fix what the log says, and repeat until RGUI is on screen.

## Working notes

- **Nothing from the old websrv repository is used.** The build reads only
  `vendor/retroarch` (pinned by `tools/fetch-retroarch.sh` to v1.22.2 at
  `69a4f0e`), this project's `src/`, `patches/`, `tooling/`, `runtime/`, `sce_sys/`
  and `assets/`. `vendor/retroarch` no longer carries a git directory, so it cannot
  be committed into: upstream stays upstream.
- **The exact working command is `bash tools/build-title.sh`**, not bare
  `make app`. The Makefile's four unknowns — vendored SDK, `PS5_CLANG`, the
  `pystub` `PYTHONPATH`, and RetroArch's include paths plus the frontend archive —
  are set by that script and by nothing else.
- **Compile from `build/ra-conf`, never from `vendor/retroarch`.** A build that
  read sources from `vendor/` and headers from `build/ra-conf` linked, signed and
  started with `video_drivers[]` holding no ps5 entry: the file that lists the
  drivers came from the tree that had never heard of the patch.
- **The feature set is chosen once**, in `tools/retroarch-sources.sh`'s configure
  flags. `tools/retroarch-flags.sh` asks `make` which `-D` flags it would use
  rather than restating them; a hand-written list is wrong in both directions.
- The console's address and credentials come from the ignored `.env`.

## Last verified

| Check | Result |
| --- | --- |
| `bash tools/build-title.sh` | PASS: 225 of 225 sources, 225 objects archived, `eboot.bin` 8,026,239 bytes, signed, 12 segments, `integrity: valid` |
| Same build twice | PASS: identical `eboot.bin` digest `71c88975040535a02b462618dd394a7a378034272c9bffa43ce0ca4b717ab9b7` |
| `bash tools/check-manifest.sh` | PASS: 7 files match their digests, none extra, `eboot.bin` magic `4f153d1d`; FAILS as it should when one byte is changed |
| `readelf -r build/ra/obj/gfx_video_driver.c.o` | PASS: `.rela.data.video_drivers` holds `video_ps5` then `video_null` |
| `bash tools/fetch-retroarch.sh --verify` | PASS: `vendor/retroarch` is at the pinned revision, with no history |
| `bash tools/lint-shell.sh` | PASS: 28 scripts and 5 python tools parse, no CRLF, no committed address or credential |
| `bash tools/lint-format.sh` | PASS after applying the policy to `src/` |
| `python3 tools/deploy-title.py` | **FAILED**: the console stores the transfer and serves the previous bytes; two attempts, both read back the old file |
| `tools/verify.sh` (all gates) | not yet green: `unit`, `build` and `integration` still name commands that have to be wired |

## Open findings

- The console's FTP write path is the blocker for the goal, and it is not this
  project's code: the server answers `226` to `STOR` and then serves the file it
  already had. The owner uploading by hand is the route that works.
- `tools/verify.sh`'s `build` gate runs bare `make app`, which cannot work without
  the environment `tools/build-title.sh` sets. It must call that script instead.
  Its `unit` and `integration` gates name `make test-unit` and
  `make test-integration`, and the Makefile has no integration target yet.
- The RGUI menu reads its assets from `ASSETS_DIR`, compiled as `/app0/assets`,
  and the artifact does not carry an `assets/` folder: the menu will fall back to
  its built-in font and look sparse. Enough to prove the driver, not enough to
  ship. Whether RetroArch's asset directory should be bundled is undecided.
