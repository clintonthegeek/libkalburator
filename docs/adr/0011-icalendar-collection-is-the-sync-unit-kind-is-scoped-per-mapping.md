# ADR 0011: An iCalendar collection is the sync unit; component kind is scoped per mapping

- **Status:** Accepted
- **Date:** 2026-09-13
- **Supersedes:** the B2C P3.e kind-demux partition in
  `MultiProtocolDavProvider::createBackends()`. It was never an ADR. Its record
  is the archived campaign status,
  `docs/archive/pre-consolidation-2026-09-03/docs/campaign/b2c/2026-08-25-p3-todo-status.md`.

## Context

A CalDAV collection may hold `VEVENT`, `VTODO` and `VJOURNAL` components.
RFC 4791 §4.1 limits one calendar object resource to one component kind (plus
`VTIMEZONE`). `supported-calendar-component-set` declares which kinds a
collection accepts. Servers differ on whether they enforce it.

P3.e partitions an account as soon as any of its calendars advertises `VTODO`.
One shared `RemoteCalendarBackend` transport sits under two `KindDemuxBackend`s,
registered as specs `cal` and `todo`. Mixed collections appear in both, through
raw-kind `FilteredCollectionBackend` views. Task-only collections appear only in
`todo`.

A real Nextcloud account in PlanStan exposed this on 2026-09-13
(`../PlanStan/docs/bugs/dav-task-calendars-bound-to-cal-domain-backend.md`).
Reading the partition against the engine established six facts:

1. **The partition does not reach the todo domain.** A raw-kind view reports
   its parent's shape, `{calendar, ical}`, and `dispatchSync` rejects mappings
   whose endpoints are in different domains. A CalDAV task list on `todo`
   therefore cannot map to Google Tasks, Microsoft To Do or a Palm ToDoDB. That
   was the purpose of the partition. `todo` is a spec label, not a domain.
2. **The views filter reads only.** `createRecord`, `updateRecord`,
   `deleteRecord`, `modifiedSince` and `deletedSince` pass through unfiltered,
   and both views share one CTag. A view claims to hold one kind while its
   writes and change feeds carry both.
3. **A consumer binding a mixed collection has no safe choice.** Bound through
   one view, the other kind is silently never synced. PlanStan does this today.
   Bound through both views against an unfiltered peer, each TwoWay mapping sees
   the other kind as missing on its target, and either re-pushes it or infers a
   delete once baselined.
4. **A task-only collection has no route on `cal`.** `shapeFor()` returns
   `Shape::Any()` and the mapping is rejected on every run. PlanStan observed
   this for five of eight calendars.
5. **The calendar domain already carries every kind.** The
   `{calendar,canon}`↔`{calendar,ical}` edge is kind-polymorphic, and IP.11
   (O89) proved a `VTODO` through it equivalent to `{todo,canon}`
   (`tst_vtodo_domain_convergence`).
6. **Kind incompatibility is not specific to the partition.** A mixed copy
   mapped to an events-only copy has nowhere legitimate to put tasks. The engine
   pushes them today with no check: `BackendCapabilities::supportsCalendarType()`
   and `describeLoss()` have no engine caller
   (`../PlanStan/docs/bugs/component-type-restriction-not-enforced-at-sync-time.md`).

The underlying question is what the unit of sync is.

**Rejected: (collection, kind) as the unit.** Completing the partition would
need several things at once:

- each view gets its own filtered change feed, and kind-guarded deletes and
  writes; `deletedSince` carries ids only, so a deleted record's kind has to
  come from the baseline;
- the CTag stays shared regardless;
- every consumer's local copy is split the same way, so both ends of every
  mapping are single-kind;
- mapping counts double;
- changing an item's kind becomes an unrelated delete in one mapping and create
  in another;
- one server calendar is presented to users as two things.

## Decision

**1. The physical collection is the unit of sync state.** Backends expose an
iCalendar collection unfiltered, whatever kinds it holds.
`MultiProtocolDavProvider` registers one calendar-domain backend for all of an
account's iCalendar collections. `KindDemuxBackend` and
`FilteredCollectionBackend`'s raw-kind mode are deleted, not switched off by an
option. Baselines, revisions, change detection and deletions stay per
collection.

**2. Component kind is a record attribute declared by the domain.** The domain
contract gains a record-kind discriminator. The calendar domain returns the
record's component kind (`VEVENT`, `VTODO`, `VJOURNAL`). This follows ADR 0009's
write-unit key: the engine consumes the discriminator without calendar
knowledge, and a domain that declares none has one implicit kind and behaves
exactly as today.

**3. Each mapping has a kind scope, applied by the engine to both ends.** Source
listing, target listing, baselines and change feeds are restricted to the scope
before the diff. A record outside the scope is out of scope, never missing: no
create, update or delete is inferred from it on either side. The engine
distinguishes *absent* from *present with an out-of-scope kind* using the
unrestricted listing. A baselined record whose kind changes out of the scope is
reported and is never propagated as a delete.

**4. The default scope is the kinds both endpoints accept.** Accepted kinds come
from discovery. An endpoint whose support is unknown accepts every kind. A
consumer may narrow a mapping's scope explicitly and may not widen it past what
the target accepts.

**5. Writes are admitted by kind.** Before applying, the engine checks each
record's kind against the target collection's accepted set. A refused record is
reported per record against the mapping. It is never pushed, and never dropped
silently.

**6. Tasks cross into the todo domain only through a declared bridge, never
through a backend shape.** Syncing a calendar collection's tasks with a
todo-domain service is a `VTODO`-scoped mapping over a declared, lossy
`{calendar,canon}` (kind `vtodo`) ↔ `{todo,canon}` edge, with its losses listed
in the convergence matrix. It is not built until a consumer registers a
task-only service. Until then, cross-domain mappings stay rejected as they are
today.

**7. Consumers do not persist a provider's internal backend layout.** A binding
names a provider and a collection. Which backend instance hosts the collection
is resolved at load. This rules out persisted keys such as PlanStan's
`<provider>:cal`.

## Consequences

- Decision 1 alone fixes PlanStan's task-only and mixed-calendar failures.
- `BackendExecutor::start()`'s `KindDemuxBackend` exemption, added to the
  library on 2026-09-13, loses its subject. Matching
  on class names is still fragile, and replacing it with a property the backend
  declares is still wanted
  (`../PlanStan/docs/bugs/kinddemuxbackend-threaded-away-from-shared-caldav-transport.md`).
- `FilteredCollectionBackend` keeps its `RecordFilter` mode; WildPalms' category
  routes use it.
- `tst_multiprotocoldavprovider`'s demux cases are rewritten for the
  single-backend shape. `tst_vtodo_domain_convergence` remains the evidence for
  context fact 5.
- Decision 3 works at the same apply boundary as ADR 0009 (`classifyForWriter()`,
  `WriterBatch`). Their implementations must not be in progress at the same
  time.
- A mapping's definition gains a kind scope. Consumers show it and let users
  narrow it. Convergence validation treats two rules into one collection with
  disjoint scopes as not converging.
- Consumer file formats change under decision 7. PlanStan does not migrate
  `.kalb` files; backward compatibility was explicitly waived by the user on
  2026-09-13.

## Implementation status (2026-09-13)

Nothing is implemented. The work is tracked as `KND-001`–`KND-005` in
[`../TASKS.md`](../TASKS.md).
