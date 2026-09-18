# Agent instructions

PS5 RetroArch is a native RetroArch homebrew application for jailbroken
PlayStation 5 consoles. It gives the console a libretro frontend it was never
shipped with, so the library of emulators and games that already runs through
libretro also runs here.

## Read order

Read in this order and stop as soon as you have what you need:

1. This file. Frozen.
2. `docs/PLAN.md` — the gates, the milestone map, the invariants that constrain
   the code, and the index of the reference files. About 150 lines, once per
   session.
3. `docs/ACTIVE.md` — the volatile state: the current step, the next actions,
   the blockers, the last verified runs. Read last, before you start work.
4. On demand only, when the task needs it: `docs/REFERENCE.md` (the step ladder,
   the workflow, the environment), `docs/FINDINGS.md` (the evidence behind each
   invariant), `docs/TESTING.md`, `docs/DEPLOYMENT.md`,
   `docs/TROUBLESHOOTING.md`, `docs/PHASE_LOG.md` (append-only run logs).

Never read a long log end to end to answer a status question: `docs/ACTIVE.md`
and each log's own summary say what passed.

## Volatility contract

| File | Rule |
| --- | --- |
| `AGENTS.md` | Frozen. Change only when the workflow itself changes. |
| `docs/PLAN.md` | Static. Edit only when a gate, a milestone or an invariant changes. Never record progress here, and keep it short: it is read every session. |
| `docs/ACTIVE.md` | Volatile. Rewrite in place; keep it under about 120 lines. |
| `docs/PHASE_LOG.md`, `docs/FINDINGS.md` | Append-only. New dated entries at the end; never rewrite an existing one. |
| `docs/REFERENCE.md`, `docs/TESTING.md`, `docs/DEPLOYMENT.md`, `docs/TROUBLESHOOTING.md` | Stable. Edit when the specification or the procedure changes. |
| Any other `docs/*.md` | Stable. Normal edits. |

Progress, run results and "done" markers belong in `docs/ACTIVE.md` or
`docs/PHASE_LOG.md`, never in the plan.

## Prompt caching

The agent's context is cached by exact prefix: only what sits *before* the new
material matters. Keep that part byte-identical between turns.

- Append, don't edit. Adding to the end of a file or a conversation is cheap;
  changing text already in context invalidates everything after it.
- Never put a date, a version or a "last updated" line at the top of a file that
  is read first. A date belongs in the volatile file, which is read last.
- Keep the read order fixed and never reflow, renumber or bulk-rename: a
  reorder or a reformat changes the prefix exactly as a rewrite does. Add a new
  on-demand document to the list when it is created, once.
- Keep `docs/ACTIVE.md` small: it is the only file expected to change every
  session, so its size is paid on every session.
- One fact, one home. Never copy status into the plan; link to `docs/ACTIVE.md`
  or a log instead.
- Batch documentation edits. One write-up at the end of a step beats continuous
  small edits, which invalidate the cache repeatedly.
- Long files are appended to and searched, never rewritten, never read whole.

## Your task

The task in your prompt is the only task. `docs/ACTIVE.md` describes what the
project is doing; it is context, not an assignment, and its "Next" list is not a
queue. When the prompt and `docs/ACTIVE.md` disagree, the prompt wins: do not
start the active file's next step, and say in your report that you noticed.

## Work loop

One step per commit, and no step is done until it is verified.

1. Read the prompt, then `docs/ACTIVE.md`. Inspect the workspace: it is
   authoritative. An earlier turn's narration, a summary or a compacted
   conversation is a claim, not a fact.
2. Choose the smallest step that makes real progress, and say what would prove
   it before building it.
3. Implement it, then run the gates.
4. Record the evidence: the command, the result, and the artifact it produced.
5. Commit it with that evidence, and write the step up once, in
   `docs/ACTIVE.md`, plus a dated entry in `docs/PHASE_LOG.md` when it lands.

Work that cannot be verified here is never committed as if it were: park it in
`parked/` with the plan that would finish it, and say so.

## Gates

| Gate | Purpose |
| --- | --- |
| `format` | Format, lint and static checks |
| `unit` | Unit tests |
| `build` | Build every shipped target |
| `integration` | Integration or end-to-end tests |
| `evidence` | Replay and compare recorded evidence |

`tools/verify.sh` holds the commands — its only home — and runs them in that
order, failing fast. A failing gate is fixed before the next step starts; a red
gate is never carried forward. What each gate means, and how to add a test:
`docs/TESTING.md`.

## Evidence

- Evidence is a committed artifact, not a claim: a machine-readable capture from
  the console (a `klog` run record, an ELF symbol/import dump, a staged-tree
  manifest) plus the expected result it is compared against. Every step's
  acceptance names the artifact and the command that reproduces it, and the
  commit message carries both.
- An unexplained failure keeps the gate red. A tolerated one is named in
  `docs/ACTIVE.md` with its reason, and removed when the reason is gone.
- Write logs factually: what was run, what it returned, what it proves, with
  failures at the same detail as successes.

## Long-running work

Keep one completion objective for work that spans many turns, and let it drive
the rounds.

- State it as something finishable and checkable, not a direction: for example
  "a release ZIP whose `eboot.bin` loads as a title on the 6.02 console, draws the RGUI
  menu and returns the driver identity recorded in `docs/FINDINGS.md`".
- Each round: inspect the workspace, make concrete progress, verify it, report
  what remains. Never restart finished work.
- Mark it complete only with the evidence it names. A blocker is a concrete
  condition that persisted across several rounds with the same cause; difficulty,
  uncertainty or remaining work is not a blocker.
- After a summary or a compaction, re-read `docs/ACTIVE.md` and inspect the
  workspace before acting.

## Working rules

- Commit finished, verified work. Never push and never rewrite published history
  unless the human asks.
- Keep generated and environment files out of the diff: `vendor/`, `build/`,
  `dist/`, `.deps/`, `.env`, `*.elf`, `*.so`, `klog/`.
- Do not add a toolchain flag, a dependency or a version pin to a shared script
  without saying why in the write-up. Never commit a secret, a key or a client's
  data. The console address, the FTP credentials and anything read from the
  console stay in the ignored `.env` and in a git-ignored capture directory.
