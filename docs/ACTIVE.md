# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-18_

## Now

**The baseline payload is built and deployed to the console; the launch run is
the one thing left.** `tools/build-baseline.sh` cross-compiles upstream RetroArch
1.21.0 from the vendored recipe, stages `dist/baseline/` (payload, config, icon,
launcher manifest, digest manifest) and passes the PS5-object check. Evidence:
the staged digest manifest and the build log at `work/baseline/logs/build.log`;
the findings are recorded in `docs/FINDINGS.md`. What it still owes: a console
run showing the menu, which is what makes it a baseline rather than a build.

**It is on the console at `/data/homebrew/PS5_RetroArch/`.** Verified by listing
the folder: `retroarch.elf` (70,642,656 bytes), `retroarch.cfg`,
`homebrew.js`, `manifest.sha256`. The user's own clean build in
`/data/homebrew/RetroArch/` was not touched, by design and by the tool's own
rule. What it still owes: the icon, which sits at the folder root instead of
`sce_sys/icon0.png` because of the FTP quirk below.

**The console's FTP went read-only in the middle of the last deploy.** Writes
returned `550 Read-only filesystem` while reads kept working and the control
payload still answered `ok idle`. Whether that is a console-side remount or
something transient is not yet known; the next step is to re-check the write
path before blaming the tool. Recorded because a read-only console looks exactly
like a broken uploader from here.

**The build is now cheap.** 33–35 s from cache, warm or incremental, against
about five minutes cold: the tarball is cached and digest-checked, the prepared
tree is reused, every compile goes through ccache, and make runs with `-j14`.
Evidence: `work/build-run5.log` and `work/build-incr.log`, both with their
timings; the three fixes that made it work are in `docs/FINDINGS.md`.

## Next

1. Re-check the console's write path, then finish the deploy so `sce_sys/icon0.png`
   is where the loader expects it.
2. Launch `/data/homebrew/PS5_RetroArch/` from the console's own loader and
   capture the run: this is the Option 1 baseline proof.
3. Record that run as the first entry under `evidence/`, with the distilled
   capture and the command that reproduces it.
4. Then Option 2: swap the software SDL2 path for the Vulkan driver, which needs
   the driver's consumer package and the `platform/` work in
   `docs/REFERENCE.md`.

## Working notes

- `tools/fetch-ports.sh` provisions the ports image once (346 MB, digest-checked,
  cached in `.deps/pacbrew/`) and `tools/build-baseline.sh` runs `--check`
  without building anything. Both are cheap to re-run.
- The recipe's own build script is never edited: our changes are injected into a
  copy at `work/baseline/build.sh`, so `reference/ps5-retroarch/` stays the
  baseline it was measured against.
- This console's FTP service ignores the path in a listing command and answers
  deletes with 226. `tools/deploy.py` handles both; do not "simplify" that away.
- `source $PS5_PAYLOAD_SDK/toolchain/prospero.sh` before any manual PS5 compile;
  the clang is 22.1.8 targeting `x86_64-sie-ps5`.
- The console's address and credentials come from the ignored `.env`.

## Last verified

| Check | Result |
| --- | --- |
| `tools/build-baseline.sh` from cache | PASS in 34 s; 4 files staged; payload imports `libkernel_web.sprx`, not `libkernel_sys.sprx` |
| `tools/build-baseline.sh` after a source edit | PASS in 35 s |
| `tools/fetch-ports.sh` | PASS: v0.40.2 verified by SHA-256, SDL2 2.30.12 resolved |
| Console listing of `/data/homebrew/PS5_RetroArch/` | payload, config, manifest and launcher present; icon still at the folder root |
| Console FTP writes | FAILED: `550 Read-only filesystem` on the last attempt; reads fine |
| Console run of the baseline | not yet: this is the open step |
| `tools/verify.sh` (all gates) | not yet green: the gate scripts it names still have to be written |

## Open findings

- The console's write path needs re-checking before the deploy can finish. If it
  stays read-only, the loader cannot see a new folder either.
- The recipe pins upstream 1.21.0 while our own tree is 1.22.2. The baseline is
  deliberately the recipe's version so it matches what the user already runs.
- Nothing in this repository has run on a console yet, so every target-layer
  claim in the docs is a specification, not a result.
