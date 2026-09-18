# Active work

Volatile by design. Rewrite this file in place and keep it under about 120 lines.
Specifications belong in `docs/PLAN.md`, and run results belong in
`docs/PHASE_LOG.md`. This is the only file that carries a date: the line below is
the last thing read, so changing it costs nothing.

_Updated: 2026-09-18_

## Now

**The PPSA title folder exists and is one file short.** `tools/stage-ppsa.sh`
assembles `dist/PPSA99005/` — the title id, the 512x512 launcher icon, the
loader's compatibility module `sce_module/libc.prx`, the configuration seed, the
payload beside it, and a digest manifest. Evidence: the staged folder and its
`manifest.sha256`; the identity is `title/sce_sys/param.json`, checked for a
valid id, content id, version and launch intent before anything is written. What
it still owes: `eboot.bin`. Without it the console will not list the title, and
the script says so and exits 3 rather than pretending the folder is complete.

**Getting `eboot.bin` is now down to one known limitation.** The image converter
in `../ps5-native-app-boilerplate-main` rejected three things in turn, and two
are solved here: the linked layout now reserves room for the console's process
parameters (`linker/ps5-pie.ld` applied by `tools/prospero-clang-link`), and the
symbols this target references but no SDK stub defines are now provided
(`platform/ps5_dl_stubs.c`). The third is the converter's own limit — it refuses
to publish application exports, and our payload has them:
`error: native converter does not yet publish application exports`. All three are
recorded with their exact text in `docs/FINDINGS.md`.

**The Option 1 baseline remains verified.** `evidence/m1-baseline-loads/` holds
the console run: payload started under the homebrew launcher, menu on screen,
configuration written into the title folder. That is unchanged by the work above,
which is about the PPSA packaging path rather than the payload.

**The console is shared with the PS5_Vulkan session; every launch and upload is
asked for first** (`docs/DEPLOYMENT.md`). Nothing is touched without a go-ahead.

## Next

1. Decide where the export limitation is fixed: relax the converter's
   `symbol.undefined()` requirement, or link the payload with an export list
   that hides RetroArch's own symbols while keeping the ones the loader needs.
   The first is a change in the sibling project, the second is a change here.
2. Produce `eboot.bin` and let `tools/stage-ppsa.sh` finish the folder.
3. Ask for a console window, deploy the PPSA folder and install it so the home
   screen lists it, then capture the run as `evidence/m-ppsa/`.
4. Option 2 proper: the Vulkan driver. `../PS5_Vulkan` already implements every
   WSI entry point RetroArch's display-based Vulkan path needs, so the open
   questions are the consumer package and what RetroArch's renderer asks for
   beyond it.

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
| `tools/stage-ppsa.sh` | assembles `dist/PPSA99005/` with 5 files and a digest manifest; exits 3 with `eboot.bin` absent, by design |
| Image conversion | layout requirement solved (`linker/ps5-pie.ld`), symbol requirement solved (`platform/ps5_dl_stubs.c`), blocked on the converter not publishing exports |
| `tools/verify.sh` (all gates) | not yet green: the gate scripts it names still have to be written |

## Open findings

- The console's write path needs re-checking before the deploy can finish. If it
  stays read-only, the loader cannot see a new folder either.
- The recipe pins upstream 1.21.0 while our own tree is 1.22.2. The baseline is
  deliberately the recipe's version so it matches what the user already runs.
- Nothing in this repository has run on a console yet, so every target-layer
  claim in the docs is a specification, not a result.
