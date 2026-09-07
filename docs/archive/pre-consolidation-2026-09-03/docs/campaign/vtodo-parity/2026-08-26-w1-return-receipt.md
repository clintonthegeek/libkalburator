# Return receipt — W1 detached exceptions end-to-end contract

**Delivered:** 2026-08-26 (completes W1 after VP.c-step-1a/b/c identity
layer: commits `6a166ac`, `c5592e0`, `9fe2be5`; this receipt's doc +
matrix commit follows)
**Consumes:** handoff §W1; response doc §1 W1 (rescope "identity layer
first"); W2 receipt for the shared flatten strategy.
**Contract doc:** `docs/campaign/vtodo-parity/2026-08-26-w1-detached-exceptions-contract.md`
(§1 keying, §2 differ treatment, §3 delete semantics, §4 flatten
strategy, §7 full test matrix).

## What was delivered

1. **Identity layer** (VP.c-step-1, landed earlier same day):
   - `src/sync/recordidentity.h` — `composeRecordIdentity`,
     `decomposeRecordIdentity`, `isExceptionRecordId`
     (`uid + '\x01' + recurrenceId UTC-ISO`; masters stay bare uid).
   - Todo canon `recurrenceId` / `recurrenceRange` keys (mirror the event
     path byte-for-byte); scanner role-selector (`recurrenceIdUtc`,
     master-preference default intact).
   - Composite wiring in `RemoteCalendarBackend` (step-1b) and
     `SubscriptionBackend` (step-1c); `LocalBackend` deliberately NOT
     compounded (id↔filename bijection) with a full-file-bytes pin.
2. **Written contract doc** — keying, differ treatment (independent diffs,
   joined by shared uid for propagation), delete semantics (exception-only
   delete = EXDATE removal + tombstone; master delete = cascade tombstone),
   flatten strategy for Google/MS legs.
3. **Full test matrix** (create/edit/delete/reabsorb ×
   caldav/subscription/local/org/google/ms) — §7 of the contract doc; each
   implemented cell names its pinning slot, flatten/loss cells cite the
   declared ruling.
4. **Two new backend pins** (CalDAV matrix cells previously unstated):
   - `detachedException_reabsorb_surfacesMasterOnly` — server-side drop of
     the exception resource → refetch = 1 master record; stale composite
     `loadRecord()` degrades to the master.
   - `detachedException_masterDelete_removesOnlyMasterHref` — bare-uid
     master delete removes ONLY the master href; the exception resource
     survives; refetch = 1 composite-id record. (Engine-level cascade
     tombstoning is the §3 policy contract, not a backend single-href
     call.)
   - Supporting seam: `FakeCalDavServer::removeEventAt(href, fileName)`
     (per-resource server-side removal for reabsorb simulation).

## Contracts / public API

- `recordidentity.h` as in §6 of the contract doc (unchanged from step-1a).
- No canon key changes beyond step-1a (`recurrenceId` = `{"dateTime": <UTC
  ISO>}`, `recurrenceRange` = `"thisAndFuture"`).
- No engine behavior changed by W1: propagation remains per-record; uid
  family join is specified, not executed (contract doc §2/§3, §5).

## Tests proving acceptance

- `tst_record_identity` (8) — pure identity round-trips.
- `tst_remotecalendarbackend_blob_view` — now **27 slots** (was 25): +
  `detachedException_reabsorb_surfacesMasterOnly`,
  `detachedException_masterDelete_removesOnlyMasterHref`.
- `tst_subscriptionbackend_blob_view` (14) — unchanged, all green.
- `tst_localbackend_blob_view` (13) — unchanged, all green.
- `tst_todo_canon_roundtrip` / `tst_icalcomponentscan` — unchanged, all
  green (recurrenceId + selector pins from step-1a).
- Full suite status re-run at commit time.

## Deprecations / behavior changes affecting PlanStan callers

- None new beyond the W2 note: `ConflictInfo.sourceId/targetId` may carry a
  composite id (`\x01`) for exception records; decompose before display.
- Master-delete cascade (contract doc §3) is a host-side propagation policy
  to adopt when you delete a series: enumerate and delete the keyed
  exceptions too.

## Next (per campaign STATUS)

W4 completion-anchored recurrence (VP.d), then W3 series-split (VP.e),
then W5+W6.2+W7 (VP.f).
