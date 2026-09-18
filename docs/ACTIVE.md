# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-18_

## Now

**The console loop runs itself.** `bash tools/run-title.sh` builds, publishes,
verifies what the console stored, listens, launches, watches, closes the title
itself and prints the title's own trace. No upload needs a person any more.

**The title runs and stays up.** 1500+ frames in 25 s, closed by the script, after
`patches/series` 0004 fixed a null joypad driver that killed the second pass
through the runloop (see `docs/FINDINGS.md`).

**The menu does not draw yet, and the reason is measured.** RGUI initialises and
holds a 320x240 framebuffer, but `GFX_DISP_FLAG_FB_DIRTY` is never set, so
`rgui_set_texture` returns every frame and the driver reports `no-menu-source`.
That flag is set at the end of `rgui_render`, which is only reached through
`menu->driver_ctx->render` under `if (BIT64_GET(menu->state, MENU_STATE_BLIT))`.
The menu's renderer never runs; finding which condition above that call is false
is the next step, and `docs/FINDINGS.md` records the path there.

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
