# Return receipt — W2 per-instance completion of recurring todos

**Delivered:** 2026-08-26 (post-W1 composite identity; commits
`7403509`, `c5592e0`, `9fe2be5`, `6a166ac`)
**Consumes:** handoff §W2; response doc §1 W2 (incl. correction noted
below); sequencing-correction note in parity STATUS.

## Canon schema / envelope keys

- Detached instance: existing `recurrenceId = {"dateTime": "<UTC ISO>"}`
  (+ `recurrenceRange` when THISANDFUTURE) — added to the todo canon in
  W1 step-1a, mirroring the event path. Master records have NO
  `recurrenceId` key.
- Master EXDATE: rides the existing verbatim `recurrence` StringList
  (catalogued; differ sees edits). EXDATE/RECURRENCE-ID form coherence is
  a PRODUCER obligation (both UTC date-time, never DATE-on-one/
  DATE-TIME-on-other); we pin verbatim capture and UTC normalization of
  the id, we do NOT rewrite your EXDATE lines.
- **Record identity:** exception record id = `uid + '\x01' + recurrenceId
  UTC-ISO` (`src/sync/recordidentity.h`). Masters stay bare uid. This is
  the W2 representation backbone; it is also the W1 contract (see W1
  receipt when delivered).

## Public APIs

- `src/sync/recordidentity.h`: `composeRecordIdentity`, `decomposeRecordIdentity`,
  `isExceptionRecordId` (landed W1 step-1a).
- `BaselineStore::transaction(fn)` — `src/storage/baselinestore.h`
  (additive template; nested calls join the outer frame; fn returns true
  to COMMIT, false to ROLLBACK; BEGIN IMMEDIATE; setError on failure).
  The engine's persist loop (`syncengine.cpp` unifiedContinueAfterConflicts)
  now persists baselines + id aliases atomically per mapping.
- `RemoteCalendarBackend` now mints composite-aware create URLs:
  exceptions PUT to `<uid>-<sanitizedRecurrenceStamp>.ics` (distinct from
  the master's `<uid>.ics`) so a master-edit + exception-create batch
  cannot clobber the master.

## Behavior contracts / edge cases

- **Q1 answered (your question):** baseline persistence for a
  master+exception pair is now ATOMIC at the store level — the engine
  wraps the whole per-mapping persist batch in one transaction. Your
  CreateExceptionCommand atomic staging is no longer load-bearing
  (still correct; can stay).
- One engine diff pass carrying master-edit(EXDATE) + exception-create
  becomes ONE WriterBatch → one `applyRecords` per side → two PUTs to two
  distinct hrefs on CalDAV (pinned by test).
- **Correction to the acceptance doc:** Google Tasks CANNOT carry the
  master's EXDATE — no extension point exists (O66(c)). Google leg:
  flattened completed exception = standalone completed task via the
  existing demote (status=completed + completed stamp; summary verbatim,
  no suffix convention — your call if you want one). Master EXDATE is an
  honest, declared loss on Google (in-place-advance semantics, same as
  tasks.org). MS To-Do leg: flattened completed exception = standalone
  completed todoTask (completedDateTime); master EXDATE rides the real
  nav-POST `x-canon-recurrence` extension (Reversible row).

## Tests proving acceptance

- `tst_record_identity` (8): compose/decompose round-trips, UTC-normalized
  ids, bare-uid passthrough, non-collision.
- `tst_todo_canon_roundtrip` (+3): recurrenceId/recurrenceRange promote
  + demote byte-equivalence; master has no key.
- `tst_icalcomponentscan` (+4): role-selected exception-block extraction;
  master-preference default intact.
- `tst_remotecalendarbackend_blob_view` (+5): two-record master+exception
  fetch, exception-addressed PUT/DELETE, id stability, loadRecord both
  forms. PLUS W2 engine fixes: exception-create PUTs to a distinct href;
  dual-write (master-edit + exception-create) batch → two distinct PUTs,
  no clobber (12 slots incl. storage transaction suite).
- `tst_baseline_store_v3` (+6): commit persists all, rollback persists
  none, nested join, failed-BEGIN reports lastError, master+exception
  pair atomic both directions.

## Deprecations / behavior changes affecting PlanStan callers

- None breaking. `ConflictInfo.sourceId/targetId` may now carry a
  composite id (`\x01`) for exception records — display code should
  decompose (helper exposed) or show the recurrenceId portion.
- Google-leg EXDATE loss is declared (was never working, now honest).

## Next

W1 written contract doc + full test matrix (cross CalDAV/org/Google/MS)
then W4 (completion-anchored recurrence). W2 round-trip acceptance
("completing occurrence round-trips without duplication after two syncs")
is satisfied at the backend layer; the editor-side CreateExceptionCommand
staging is PlanStan's half.
