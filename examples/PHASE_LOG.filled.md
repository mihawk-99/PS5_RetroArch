# Phase log

A filled example, for the same fictional SaaS project. Two entries: the landed
step and the failure that shaped it. Copy the shape, not the content.

---

## 2026-03-04: The reconciliation worker reports mismatches instead of correcting them

The worker now consumes the ledger stream, writes the expected-balance table and
emits a mismatch record when the two disagree, leaving the ledger untouched. It
exists because finance needs the disagreement to be visible before anything
rewrites a balance (TICKET-412), and it unblocks the February backfill.

**The evidence.** `tools/verify.sh` at `4c1a9f2` returned PASS on all five gates
(412 unit, 31 integration, 6 evidence replays), and the staging run
(`logs/smoke-2026-03-04.log`, 12,400 events) reported 3 of 3 seeded mismatches
with 0 corrections. The distilled capture is committed at
`evidence/reconcile/2026-03-04/`, and `tools/verify.sh evidence` replays it.

**What was tried first.** The first version corrected the balance when the
expected value differed. The staging run then reported 0 mismatches and 3
corrections, which is the opposite of what finance asked for: the correction
erased the evidence. It was reverted in `9a1c4de`, and the worker now only
reports.

**Commit.** `4c1a9f2` — Report ledger mismatches instead of correcting them.

---

## 2026-02-27: The stream offset resets on redeploy

The consumer group was recreated on every deploy, so each deploy replayed the
whole stream and double-counted history. Found by comparing the row count before
and after a no-op deploy: 41,209 rows became 82,418. The fix reads the stored
offset when `STREAM_RESUME=1` (`b7d0e11`); the runbook and
`docs/TROUBLESHOOTING.md` were updated in the same commit, because the next
person to see the doubling will look there first.
