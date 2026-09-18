# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-18_

## Now

**Option 1 is verified: the baseline loads on the console and draws its menu.**
The vendored recipe's RetroArch 1.21.0 payload was built by
`tools/build-baseline.sh`, deployed to `/data/homebrew/PS5_RetroArch/`, and
started through the console's own homebrew launcher. Evidence:
`evidence/m1-baseline-loads/` (the capture, the run it came from, and the
expectation `tools/evidence.py compare` replays), the raw capture in
`klog/launch-20260918-120546.log`, and the run's identity from the console's
control payload: `app=24600 pid=151`, launched under the homebrew title. The menu
was on screen, confirmed by the console's owner. What it still owes: nothing.

**What the run also proved about the environment.** RetroArch wrote its own
configuration tree beside the payload while it ran — `retroarch.cfg` grew from
34 KB to 110 KB and `.config/retroarch/` appeared — which is only possible if the
frontend reached its main loop with a working storage path. That is the same
mechanism the later steps depend on for cores, saves and states.

**The console is shared with the PS5_Vulkan session, so every launch and upload
is asked for first.** `docs/DEPLOYMENT.md` records the rule and the two habits
that follow from it. Nothing is launched or uploaded without a go-ahead.

**One thing to re-check when the console is next free:** the deployed folder no
longer lists `retroarch.elf`, although the payload was verified on upload and is
what the run started. Whether the loader, the read-only episode or the other
session removed it is not yet known.

## Next

1. Re-check the deployed folder when the console is free, then replace the
   payload so the folder matches what `dist/baseline/` holds.
2. Option 2, and the first step of it can be prepared without the console: read
   the OpenGL package's export list against what RetroArch's GL driver resolves
   (`tools/check-gl-exports.sh`), which is the measurement M2.1 names.
3. Freeze the driver contract with `../PS5_Vulkan`: which artifact, which
   revision, which entry points, and what it still owes before a frame.
4. Write the `platform/` video driver and its context, then launch it on the
   console with the owner's go-ahead.

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
| Console run of the baseline, `evidence/m1-baseline-loads/` | PASS: payload started under the homebrew launcher (pid 151), menu on screen, config written to the title folder |
| `tools/verify.sh` (all gates) | not yet green: the gate scripts it names still have to be written |

## Open findings

- The console's write path needs re-checking before the deploy can finish. If it
  stays read-only, the loader cannot see a new folder either.
- The recipe pins upstream 1.21.0 while our own tree is 1.22.2. The baseline is
  deliberately the recipe's version so it matches what the user already runs.
- Nothing in this repository has run on a console yet, so every target-layer
  claim in the docs is a specification, not a result.
