# Active work

A filled example, for a fictional SaaS project, so the density and the tone are
obvious. It is not part of the read order: copy the shape, not the content.

_Updated: 2026-03-04_

## Now

**The reconciliation worker is merged and proven on staging.** It consumes the
ledger stream, writes the expected-balance table and reports a mismatch instead
of correcting it, which is the behaviour the finance team asked for in
TICKET-412. Evidence: `tools/verify.sh` green at `4c1a9f2`, and the staging run
recorded in `evidence/reconcile/` (12,400 events, 3 seeded mismatches, all three
reported, 0 corrections). What it owes: the retry path when the stream redelivers
an event (it double-counts today; the test that reproduces it is `failing` in
`pnpm test`, so the gate is red until the next step lands).

**The vendor export is parked.** `patches/vendor-export.patch` implements it,
but the vendor's sandbox is the only place it can be verified and the account is
not provisioned yet. Not merged; the patch carries the verification steps.

## Next

1. Deduplicate redelivered stream events by `event_id`, with the test first.
2. Provision the vendor sandbox, then unpark and verify `vendor-export.patch`.
3. Backfill the reconciliation table for February and compare the totals.
4. Trim the raw stream retention from 30 days to 7 once the backfill is done.

## Working notes

- Migrations run before the API boots in staging; `pnpm db:migrate` twice in a
  row is safe by design, and that is the check that the down path still exists.
- `pnpm test` needs `DATABASE_URL` pointing at the disposable container
  (`compose.dev.yml`); pointing it at staging corrupts the seeded fixtures.
- The stream's consumer group resets its offset on redeploy unless
  `STREAM_RESUME=1` is set; the runbook now says so.

## Last verified

| Check | Result |
| --- | --- |
| `tools/verify.sh` (all five gates) at `4c1a9f2` | PASS: lint, 412 unit, build, 31 integration, 6 evidence replays |
| staging smoke, `logs/smoke-2026-03-04.log` | 12,400 events consumed, 3 of 3 mismatches reported, 0 corrections |
| February backfill dry run | 41,209 rows would be inserted; not committed to staging yet |
| vendor sandbox | not provisioned: the export patch is parked |

## Open findings

- Redelivery double-counts: the stream guarantees at-least-once and the worker
  assumes exactly-once. The fix is deduplication, not a stronger guarantee.
- `pnpm test` takes 90 s because the integration suite boots Postgres twice;
  acceptable for now, revisit if it passes 3 minutes.
- The vendor's sandbox rejects requests faster than 5 per second with a 429 and
  no `Retry-After`; the parked patch waits 250 ms between calls because of it.
