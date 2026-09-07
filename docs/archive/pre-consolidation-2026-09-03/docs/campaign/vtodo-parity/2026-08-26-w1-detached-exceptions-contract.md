# W1 — Detached exceptions end-to-end contract

**Delivered:** 2026-08-26 (completes W1; identity layer landed as
VP.c-step-1a/b/c 2026-08-26, commits `6a166ac`, `c5592e0`, `9fe2be5`)
**Consumes:** handoff §W1 (rescoped "identity layer first" per
`docs/2026-08-25-vtodo-parity-handoff-response.md`); W2 receipt
(`2026-08-26-w2-return-receipt.md`) for the flatten strategy this doc
quotes.
**Status:** BINDING. This is the written contract the handoff asked for.
The companion test matrix (create/edit/delete/reabsorb ×
caldav/org/google/ms) is §7 below.

---

## 1. Keying contract

- A recurring series' **master** keeps the bare RFC 5545 UID as its blob
  record id: `recordId(master) == uid`.
- A **detached instance** (a block carrying `RECURRENCE-ID`) mints the
  COMPOSITE id
  `uid + '\x01' + recurrenceId.toUTC().toString(Qt::ISODate)`
  via `composeRecordIdentity()` (`src/sync/recordidentity.h`). The UTC-ISO
  half normalizes zone forms, so one instant composes one id regardless of
  TZID/offset spelling.
- `decomposeRecordIdentity(id)` recovers `{uid, recurrenceId}`; malformed
  input (empty uid half, non-parsable timestamp half) comes back
  fail-loud-ish (both fields empty/invalid) so callers can reject without
  crashing. `isExceptionRecordId(id)` tests `'\x01'` presence.
- The separator `'\x01'` is shared with GenericSqliteBackend's component
  separator precedent and cannot appear in a legal UID or ISO-8601
  timestamp. The incidence-path twin separator `'\x00'`
  (src/diff/syncdiff.cpp) is deliberately NOT reused here (embedded NUL is
  a SQL/log/filesystem hazard).
- **Normalization obligations (producer):** EXDATE and RECURRENCE-ID forms
  must stay coherent — both UTC date-time (never DATE on one, DATE-TIME on
  the other). We pin verbatim capture and UTC-normalize the id for keying;
  we do NOT rewrite EXDATE lines.
- **Which backends compound:**
  - `RemoteCalendarBackend` — YES (every incidence-parse loop mints
    composite ids; maps `m_uidToUrl`/`m_lastRawIcsByUid` keyed by composite
    id; `loadRecord()` decomposes with graceful bare-master fallback).
  - `SubscriptionBackend` — YES (read-only; a feed carrying a master + a
    detached exception block yields two records; `loadRecord()` decomposes).
  - `LocalBackend` — **NO, deliberately.** Record id == filename minus
    `.ics` is a bijection to ONE file path; compounding would break
    id→filename. Compensation is already in place and pinned:
    `recordFromBytes()` stores the FULL file bytes, so a single `.ics`
    parsing to master + co-located exception keeps the RECURRENCE-ID block
    verbatim (test: `tst_localbackend_blob_view` →
    `coLocatedMasterAndException_preservesFullFileBytes`).
  - Deferred (NOT compounded at this stage, per recon): DecSyncBackend,
    AkonadiBackend, OrgBackend, GenericSqliteBackend.

## 2. Differ treatment of master + exception pairs

- Master and each exception are **independent records with independent
  diffs** — the engine joins by the canonical/baseline-key record id
  (`perRecordDiff()`, src/engine/perrecorddiff.cpp), and composite ids are
  distinct keys. No last-block-wins collision: a change to the master and a
  change to one exception produce two independent diff entries.
- Propagation logic joins the family by **shared uid** (the `decompose()`
  result): the contract below (delete semantics, §3) and the split carrier
  (W3) are expressed over the uid family, not over any single record.
- The todo canon differ (`CanonJsonDiffer(todoCanonPropertyIds())`,
  src/todo/tododomaindefinition.cpp) already catalogues `recurrenceId`,
  `recurrenceRange`, and the verbatim `recurrence` StringList
  (`todocanonproperties.{h,cpp}`), so a recurrence/edit change IS seen by
  the differ as an ordinary field change. EXDATE/RECURRENCE-ID coherence is
  a producer obligation (W2 receipt §"Canon schema").
- **Known differ blind spot (OPEN, O74):** `providerExtras` is invisible to
  the canonical differ — an X-prop-only change never dirties a diff. Fix is
  folded into W7 (catalogue a derived extras-hash or explicit extras key),
  NOT part of W1.

## 3. Delete semantics

The backend layer is per-record and independently addressable; the
propagation policy below is the engine/host contract.

- **Exception-only delete** (one detached instance is removed): the
  exception record is deleted (its own href/resource goes away on the
  source) AND the master must carry the instance's EXDATE — the instance
  returns to the master's rule, so the master's EXDATE keeps it from
  re-materializing. EXDATE write-out is the producer's job; the engine
  treats master-edit(EXDATE) and exception-delete as two independent diffs
  that a conforming consumer batches atomically (W2's BaselineStore
  transaction + the two-href CalDAV dual-write cover exactly this shape).
- **Master delete**: cascade tombstone of ALL keyed exceptions (every
  record whose `decompose().uid` equals the deleted master's uid). Backend
  reality pinned by test: `RemoteCalendarBackend::deleteRecord(bareUid)`
  removes ONLY the master's own href; the exception resource survives
  (`tst_remotecalendarbackend_blob_view` →
  `detachedException_masterDelete_removesOnlyMasterHref`). The cascade is
  therefore a propagation-policy item for hosts driving per-record deletes,
  not a single-href backend call.
- Tombstones flow through the normal delta pipeline (source tombstone →
  target delete); no special tombstone key is introduced by W1.

## 4. Non-supporting peers: flatten strategy

Shared with W2's mapping (W2 receipt §"Behavior contracts / edge cases").
Both vendor legs represent a detached exception as a **standalone
completed instance** — there is no wire identity for RECURRENCE-ID on
either API.

- **Google Tasks:** flatten — the detached completed instance becomes an
  ordinary standalone completed task via the existing demote
  (status=completed + completed stamp; summary verbatim, no suffix
  convention). The master's EXDATE is an **honest declared loss** on this
  leg — Google Tasks has NO extension point of any kind (O66(c), pinned),
  so the recurrence continues server-side in-place-advance semantics
  (identical to tasks.org). Not a regression; never worked, now honest.
- **MS To-Do:** same flattening — the detached completed instance becomes a
  standalone completed todoTask (completedDateTime). Unlike Google, the
  master's EXDATE rides the real nav-POST `x-canon-recurrence` open-type
  extension carrier (Reversible row, O66 correction / O73). No `recurrenceId`
  demote path exists on this leg; a detached-instance record arriving on the
  MS leg is flattened to its completion representation only.

## 5. What W1 does NOT promise

- No engine-level uid-family propagation grouping yet (the "joined by
  shared uid" rule in §2/§3 is specified here; execution is per-record in
  the current engine).
- No `RANGE=THISANDFUTURE` write-out — W3's series-split strategy covers
  this-and-future, capability-gated.
- No org-leg compounding (deferred with the other four backends, §1).
- `providerExtras` differ visibility stays OPEN (O74 → W7).

## 6. Public API / headers

- `src/sync/recordidentity.h` — `composeRecordIdentity`,
  `decomposeRecordIdentity`, `isExceptionRecordId`, `kRecordIdentitySeparator`.
- Todo canon keys `recurrenceId` (`{"dateTime": <UTC ISO>}`) and
  `recurrenceRange` — mirror the event path byte-for-byte.

## 7. Test matrix — create / edit / delete / reabsorb

Legs: **caldav** (RemoteCalendarBackend blob view over FakeCalDavServer),
**subscription** (read-only twin), **local**, **org**, **google**,
**ms**. Cells state the pinned behavior + the slot that proves it, or the
declared flatten/loss ruling.

| op | caldav | subscription | local | org | google | ms |
|---|---|---|---|---|---|---|
| **create** (exception) | mints DISTINCT composite href `<uid>-<stamp>.ics`; refetch = 2 records | rejected (read-only; id never becomes a path) | n/a — co-located in one file | not compounded (deferred) | flatten: standalone completed task | flatten: standalone completed todoTask |
| | `detachedException_applyRecordsCreate_mintsDistinctHref` | `detachedException_writePath_stillRejectedForCompositeId` | `coLocatedMasterAndException_preservesFullFileBytes` | — | (declared, W2) | (declared, W2) |
| **edit** (exception) | PUT targets the exception's OWN href, master untouched | rejected (read-only) | file bytes rewritten whole-file (full-bytes pin holds) | not compounded (deferred) | flatten: edit the standalone task | flatten: edit the standalone todoTask |
| | `detachedException_applyRecordsUpdate_targetsExceptionHref` | `detachedException_writePath_stillRejectedForCompositeId` | `updateRecord_modifies_existing_record` | — | (declared, W2) | (declared, W2) |
| **delete** (exception) | DELETE the exception's own href only; master survives; refetch = 1 record | rejected (read-only) | n/a (single file = single record) | not compounded (deferred) | flatten: delete the standalone task | flatten: delete the standalone todoTask |
| | `detachedException_deleteRecord_removesOnlyExceptionHref` | `detachedException_writePath_stillRejectedForCompositeId` | `deleteRecord_removesFile` | — | (declared, W2) | (declared, W2) |
| **delete** (master) | DELETE the master's own href only; exception resource survives; refetch = 1 record (composite id). Engine-level cascade tombstone is the §3 policy contract. | n/a (read-only feed) | n/a | not compounded (deferred) | flatten: deleting master deletes the flat task; flattened completed instances are independent tasks | flatten: same as google (independent tasks) |
| | `detachedException_masterDelete_removesOnlyMasterHref` | — | — | — | (declared) | (declared) |
| **reabsorb** | server drops the exception resource (override merged back into master) → refetch = 1 master record; stale composite `loadRecord()` degrades to the master | stale composite `loadRecord()` → graceful master fallback | n/a (single file keeps both blocks verbatim) | not compounded (deferred) | n/a (no detached-instance concept) | n/a (no detached-instance concept) |
| | `detachedException_reabsorb_surfacesMasterOnly` | `detachedException_loadRecord_addressesByIdentity` | `coLocatedMasterAndException_preservesFullFileBytes` | — | — | — |

Supporting pins (steady state / identity):
- `tst_record_identity` (8) — compose/decompose round-trips, UTC
  normalization, bare-uid passthrough, malformed fail-loud, non-collision.
- `tst_remotecalendarbackend_blob_view`:
  `detachedException_masterAndExceptionAreTwoRecords`,
  `detachedException_refetchAfterWrite_keepsIdsStable`,
  `detachedException_loadRecord_bareUidServesMaster`,
  `detachedException_dualWrite_masterEditAndExceptionCreate_twoHrefs`.
- `tst_subscriptionbackend_blob_view`:
  `detachedException_masterAndExceptionAreTwoRecords`,
  `detachedException_loadRecord_addressesByIdentity`.
- `tst_todo_canon_roundtrip`: `vtodoRoundTripPreservesRecurrenceId`,
  `vtodoRoundTripPreservesThisAndFutureRange`, `vtodoMasterHasNoRecurrenceId`.
- `tst_icalcomponentscan`: role-selected exception-block extraction
  (master-preference default intact).

## 8. Receipts owed / consumers

- Return receipt: `docs/campaign/vtodo-parity/2026-08-26-w1-return-receipt.md`.
- W2's "Next" line (W1 written contract doc + full test matrix) is now
  satisfied; the W2 receipt's "Next" refers here.
- ConflictInfo ids may be composite (W2 note already delivered to
  PlanStan): `decomposeRecordIdentity` is the display-side helper.
