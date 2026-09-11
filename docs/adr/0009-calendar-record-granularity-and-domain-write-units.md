# ADR 0009: Calendar record granularity and the domain-declared write unit

- **Status:** Accepted
- **Date:** 2026-09-11
- **Refines:** [ADR 0008](0008-recurrence-override-identity-and-family-assembly.md),
  whose implementation-status section left the family-assembly point open with
  three unchosen candidates. This ADR chooses, and corrects the premise the
  candidates were drawn from.

## Context

ADR 0008 established per-override record identity and named one blocker for
CalDAV family assembly: the engine transcodes every record through canon on its
way between backends, and a canon object is one incidence, so a family is
already taken apart before the CalDAV writer sees it. It listed three candidate
assembly points and chose none.

Reading the code to choose between them turned up a fact the ADR did not have,
and it changes the question.

**The read side does not produce per-component records. It produces
per-component *ids* attached to whole-family *payloads*.**
`RemoteCalendarBackend` mints one `BackendRecord` per component, but every one
of the three sites that populates `m_lastRawIcsByUid` stores the bytes of the
entire calendar object resource (`remotecalendarbackend.cpp:1977`, `:2314`,
`:2531`). A master plus one detached override therefore comes back as two
records with:

- different ids (bare uid; `uid\x01<UTC-ISO recurrence-id>`),
- byte-identical `data`,
- byte-identical `contentHash` (SHA-256 over those same bytes).

`ICalToCanonStage::transform()` then parses that payload with
`KCalendarCore::ICalFormat::fromString()`, documented by KDE as returning "the
first iCal component as an Incidence". Both records consequently transcode to
the **master**. The override's canon object is a copy of its master's.

Two consequences follow that the earlier framing obscured:

1. ADR 0008 decision 3 — "conflict granularity stays per-override" — is not
   merely unimplemented, it is unreachable on the current read side. Two
   components that hash identically cannot be told apart by any diff.
2. Any fix confined to the write side leaves the override's bytes wrong. The
   family is not "taken apart upstream"; it was never correctly separated in
   the first place.

The unit-of-storage disagreement across backends is unchanged from ADR 0008's
table: CalDAV and vdir store one UID family per resource; Google and Microsoft
Graph store one event per override, with their own ids.

Three further facts constrain the choice:

- **The domain write seam exists and is bypassed.** `SyncEngineWorker`'s
  `applyBatch` builds a `RecordWriter` via `DomainOperations::createWriter()`
  and calls its no-op `prepareForApply()`, then calls
  `IBackendRecordApplier::applyRecords()` directly and never uses the writer.
  `RecordWriter` is dead wiring in the live path. It was retired in E5.3 for a
  threading reason — `apply()` blocked the calling thread, `applyRecords()`
  does not — never for a semantic one.
- **The engine already holds the target's complete current state at write
  time.** `classifyForWriter()` performs a full
  `loadRecordsResult(collectionId)` against the destination immediately before
  every apply, to decide create-versus-update. Materializing a family at that
  point costs no additional I/O.
- **The engine is domain-agnostic and that is load-bearing.** `src/engine/`
  reaches domain behaviour through `DomainDefinition`/`DomainOperations`, and
  the same canon, diff, merge, loss-profile and property-catalogue machinery
  serves the contacts, note, outline and todo domains.

## Decision

**1. A calendar record is one component, in payload as well as identity.** The
record id rule from ADR 0008 decision 1 is unchanged. What changes is that
`BackendRecord::data` must contain exactly the component that id names,
serialized as its own `VCALENDAR`. A record's `contentHash` therefore describes
that component and nothing else.

- `RemoteCalendarBackend` splits a fetched resource into per-component records
  on read instead of duplicating the resource bytes across them.
- `LocalBackend` drops its one-record-per-file exemption (W1's deliberate
  carve-out) and emits one record per component, keyed the same way.

Both sides change together. ADR 0008's implementation-status section already
established that compounding one side alone makes things worse.

**2. Family assembly happens at the apply boundary, from state the engine
already holds.** For a write unit with more than one member, the engine
materializes the unit's full desired state by applying the batch's
per-component operations onto the destination records `classifyForWriter()` has
already loaded, and hands the backend one whole-resource write. No extra round
trip, and no reconstruction inside a backend that can no longer see the family.

This adopts ADR 0008's candidate 1 and rejects candidates 2 and 3. Candidate 2
(backend read-modify-write) is retained only as the concurrency backstop
described in decision 4, not as the assembly mechanism. Candidate 3 (family in
canon) is rejected outright: it contradicts "a canon object is one incidence",
ripples through machinery shared by four other domains, collapses conflict
granularity to the whole series, and relocates assembly into Google and Graph,
the two transports where it is hardest.

**3. Grouping is declared by the domain, never inferred by the engine.** The
domain contract gains a write-unit key: given a record, return the key of the
write unit it belongs to. The calendar domain returns the bare UID (the
`decomposeRecordIdentity().uid` half). A domain that declares no key keeps
today's per-record behaviour exactly, so contacts, note, outline, todo and
every blob/universal backend are unaffected.

Whether the grouping is consumed through a restored, async `RecordWriter` or
through a narrower hook in `classifyForWriter()` is an implementation choice,
not a decision here. The constraint is that calendar-specific knowledge must
not appear in `src/engine/`.

**4. Transports that own a whole resource write it under `If-Match` and retry
on 412.** The engine's materialized family is computed from a read that
precedes the write, so a concurrent change by another client is possible.
`RemoteCalendarBackend` re-reads, re-materializes and retries on precondition
failure. This is ADR 0008 decision 3's precision preserved, not a substitute
for it.

**5. `FakeCalDavServer` enforces `CALDAV:no-uid-conflict`.** It currently has
no such check, and its source comment encodes "several resources may share one
UID" as a premise. Until it refuses a second resource carrying an already-used
UID, it will keep certifying designs that Radicale, Sabre, Nextcloud and Baïkal
reject. This is a precondition for the rest of this ADR, not a consequence of
it.

## Consequences

- `BackendRecord::contentHash` becomes meaningful per component. Existing
  baselines for calendars containing a detached override are wrong today (both
  components share a hash) and will be re-established on the next sync; this is
  a one-time reconciliation, not a migration.
- `LocalBackend`'s record layer changes shape. Its file layout does not: one
  `.ics` per UID family stays, per ADR 0008. The record layer stops being a
  one-to-one projection of the file layout, which means `recordPathFor()`,
  `createRecord()`, `updateRecord()` and `deleteRecord()` can no longer name a
  file by the raw record id — a composite id is not a legal filename and
  contains a control character.
- `WriterBatch`, or the applier interface that takes it, gains a notion of
  write units. Every `IBackendRecordApplier` implementation is affected;
  backends declaring no grouping keep their current behaviour by construction.
- `RemoteCalendarBackend::generateItemUrlForCreate()`'s distinct-href minting
  for overrides becomes dead and must be removed, not left as a fallback. It is
  wrong against every conforming server.
- ADR 0008 decisions 2 (family-ordered apply), 4 (orphan overrides preserved
  verbatim), 5 (`RANGE=THISANDFUTURE` never emitted) and 6 (divergence reported
  per leg) are unaffected and still stand.
- Google and Microsoft Graph remain a design constraint rather than a live one;
  they stay unregistered in PlanStan's `seedBuiltinContributions()`. Under this
  ADR they need no assembly at all, which is the point of choosing per-component
  records.

## Implementation status (2026-09-11)

Nothing in this ADR is implemented. Decision 5 is the first step and is
independent of the rest. The remainder is sized as a single coordinated change
across both calendar backends' read and write paths, the domain contract, and
the batch type — explicitly not something to fold into an in-flight
release-readiness task.
