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
