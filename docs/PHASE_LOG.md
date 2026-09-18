# Phase log

Append-only. A dated entry per landed step, newest at the end. Never rewrite an
existing entry: a correction is a new entry that says what it corrects. This
file is read by search and by its tail, never end to end.

An entry holds, in this order: what changed, why it is right, the evidence (the
exact command or run and what it returned), and the commit. Keep the failures in
too — the runs that were wrong are what stop the next agent from repeating them.

---

## 2026-09-18: The documentation contract is instantiated for PS5 RetroArch

The repository stopped being a template. `AGENTS.md`, `docs/PLAN.md`,
`docs/ACTIVE.md`, `docs/REFERENCE.md`, `docs/TESTING.md`,
`docs/DEPLOYMENT.md`, `docs/TROUBLESHOOTING.md`, `docs/FINDINGS.md` and
`tools/verify.sh` now describe a PS5 RetroArch port: its gates, its milestone
map (M0 contract, M1 console shell, M2 video and input, M3 cores and storage,
M4 audio, mapping and release), its four invariants, its step ladder, the
pinned environment, and the evidence rules. It exists because the project has
to be defined before any code is written, and it unblocks every later step.

**The evidence.** `grep -rno '{{[A-Z0-9_]*}}' AGENTS.md docs tools .gitignore`
returns nothing, so no template placeholder is left in the read path;
`bash -n tools/verify.sh` and `tools/verify.sh --list` return the five gate
commands; the toolchain was proven to work by cross-compiling a hello-world C
file, which produced an ELF 64-bit LSB pie executable, x86-64, version 1
(FreeBSD), 110,712 bytes, from `prospero-clang` 22.1.8 targeting
`x86_64-sie-ps5`. The four findings of the survey are committed in
`docs/FINDINGS.md`; the environment facts are in `docs/REFERENCE.md`.

**What was tried first.** The first draft of the plan assumed the shipped
graphics path was a Vulkan context driver through `../PS5_Vulkan` and treated
the OpenGL project as unrelated. Reading the two local checkouts showed the
opposite ordering: the OpenGL project is the only one with an installed consumer
package, a pkg-config contract and a recorded hardware acceptance run, while the
Vulkan project has driver sources and headers but no consumer package yet.
`docs/REFERENCE.md` now records both stages with the M2.1 measurement that
decides between them, instead of the assumption. Recorded here so the next step
does not rediscover it.

**Commit.** `3dfc3dd` — Instantiate the agent documentation contract for PS5
RetroArch.

---

## 2026-09-18: The existing PS5 RetroArch payload recipe joins the repository as a baseline

`reference/ps5-retroarch/` now holds `ps5-payload-dev/websrv`'s
`homebrew/RetroArch/` at commit `1afd476`, copied by sparse checkout and kept
read-only, with `PROVENANCE.txt` recording the source, the revision, the fetch
date and a SHA-256 for every file. It exists because that recipe already turns
the upstream RetroArch tarball into a loadable PS5 payload, so this project
starts from a known baseline instead of from nothing, and it unblocks the
packaging work: the recipe's staging step already produces the title's
`icon0.png` and its `retroarch.cfg` seed.

**The evidence.** The clone is reproducible with
`git clone --filter=blob:none --no-checkout --depth 1
https://github.com/ps5-payload-dev/websrv.git` followed by
`git sparse-checkout set homebrew/RetroArch`; it returned twelve files totalling
48 KB, and `sha256sum` of each is committed in the recipe's `PROVENANCE.txt`.
Reading `build.sh` shows it pins upstream **1.21.0** and configures with
`OS=BSD`, `--enable-sdl2 --enable-mmap --enable-dylib` and every GPU switch
disabled, which is what `docs/FINDINGS.md` records.

**What was tried first.** The recipe was expected to be a drop-in build, and it
is not: `prospero-pkg-config --exists sdl2` exits 1 on this host and
`$PS5_SYSROOT/user/homebrew/` holds an empty `include/`, so the SDL2 the recipe
enables is not present. That measurement is in `docs/FINDINGS.md` and is the
first item in `docs/ACTIVE.md`'s Next list. The recipe was not modified to work
around it: it is committed as it was fetched, and any change to it is a step of
its own.

**Commit.** `0d8a225` — Vendor the existing PS5 RetroArch payload recipe as a
baseline.

**A correction to this entry's own landing.** It first landed as commit
`107dbae`, whose message was a stray shell fragment rather than the text above:
the command that wrote it chained a nested here-document, so the shell closed
the message early and fed it the script instead. Nothing was pushed, and the
commit was amended in place to `0d8a225` with no change to a single file. The
lesson is in the command, not in the tooling: one message per command, written
from a file, never a here-document inside a chain.

---

## 2026-09-18: The baseline payload builds from cache in 34 seconds and reaches the console

`tools/fetch-ports.sh` and `tools/build-baseline.sh` now build the Option 1
baseline end to end, and `tools/deploy.py` publishes it to the console under this
project's own folder name. It exists because a console run of something we did
not write is what makes the later runs of our own build interpretable, and it
unblocks the move to the Vulkan driver: the frontend, its configuration seed and
its launcher are now known-good.

**The evidence.** `tools/build-baseline.sh` returned PASS in 34 s and staged four
files in `dist/baseline/` with a digest manifest; the payload check found
`libkernel_web.sprx` in its imports and no `libkernel_sys.sprx`. A rebuild after
appending a line to `menu/menu_driver.c` returned PASS in 35 s, which is the
measurement that matters for the edit-test loop. `tools/fetch-ports.sh` verified
PacBrew v0.40.2 against its SHA-256 and resolved SDL2 2.30.12. The staged tree was
uploaded and then listed back from the console: `retroarch.elf` at 70,642,656
bytes beside `retroarch.cfg`, `homebrew.js` and the manifest.

**What was tried first.** Three approaches to the toolchain's prefix failed before
the fourth worked, and all four are recorded in `docs/FINDINGS.md`: rewriting
`PS5_HBROOT` before the recipe sources `prospero.sh` (it exports the value
unconditionally), a private mount namespace (`unshare -Urm` cannot create `/user`
without root), and bubblewrap (`--tmpfs /user` fails the same way). What works is
to give the build both spellings: a pkg-config of ours that answers with the host
path, and a rewrite of the generated `config.mk`. The recipe's own build script
was never edited — the changes are injected into the copy under `work/`, so
`reference/ps5-retroarch/` stays the baseline it was measured against.

**Still open.** The launch run has not happened: the console's FTP service began
answering `550 Read-only filesystem` to writes during the last deploy, while
reads and the control payload stayed healthy. `sce_sys/icon0.png` is therefore
still at the folder root. Both are the first item in `docs/ACTIVE.md`'s Next
list, and neither is being treated as done.

**Commit.** `1036f50` — Build and deploy the Option 1 baseline.

---

## 2026-09-18: Option 1 is proved — the baseline loads on the console and draws its menu

The vendored recipe's RetroArch 1.21.0 payload was started on the console through
its own homebrew launcher, and the menu came up. It exists because a console run
of something we did not write is the reference every later run of our own build
is read against, and it unblocks the move to the Vulkan driver: the frontend, its
configuration, its launcher manifest and the console's launcher path are all
known-good now.

**The evidence.** `tools/console-launch.sh --capture` started
`/data/homebrew/PS5_RetroArch/retroarch.elf` through websrv's `/hbldr` endpoint
with the arguments and environment the launcher manifest declares, and the
capture is committed as `evidence/m1-baseline-loads/` with the run it came from
and the expectation `tools/evidence.py compare` replays (exit 0, one capture,
zero failures). The raw capture is `klog/launch-20260918-120546.log`. The
console's control payload reported the run as the active application —
`app=24600 pid=151` under the homebrew title — and the owner confirmed the menu
was on screen. While it ran, RetroArch wrote `retroarch.cfg` (34 KB to 110 KB) and
a `.config/retroarch/` tree into its own folder, which is independent proof that
the frontend reached its main loop with a working storage path.

**What was tried first.** The first launch captured a single line —
`Fontconfig error: Cannot load default config file: No such file: (null)` — which
looked like a failure and is not one: it is fontconfig falling back to its
built-in defaults before any driver starts, and the menu renders anyway. It is
now in `docs/TROUBLESHOOTING.md` with its exact text, so the next reader does not
chase it. A second wrong turn is worth recording too: two deploys looked like
failures while the files had in fact arrived, because this console's FTP service
ignores the path argument of a listing command and answers deletes with 226.
Both behaviours are handled in `tools/deploy.py` and written up in
`docs/FINDINGS.md`.

**Commit.** `{{SHA}}` — Prove the Option 1 baseline on the console.
