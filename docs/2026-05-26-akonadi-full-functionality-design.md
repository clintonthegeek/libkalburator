# Akonadi full functionality — design

**Status:** design approved 2026-05-26, implementation plan pending.
**Scope:** bring `AkonadiBackend` (calendar) and `AkonadiContactsBackend`
(contacts) to full functionality as a **PlanStan/WildPalms sync target**
against a live Akonadi daemon.
**Build flag:** all work is behind `KALBURATOR_HAVE_AKONADI=ON` (default OFF;
CI stays stub-only).

---

## 1. Why this exists / the central finding

Akonadi support was scaffolded across Phase D (calendar backend), Phase L
(provider + contacts + plugin + config widget). On the surface the calendar
backend looked "mostly real" and contacts "a skeleton." Measuring against the
method the engine *actually calls*, that picture was wrong.

**The unified `SyncEngine` writes through the per-record `IBlobBackend` ops —
`createRecord` / `updateRecord` / `deleteRecord` — never `pushItems`.** Both
the `DefaultBlobWriter` (`src/shape/defaultblobwriter.cpp:14-21`) and the
calendar domain writer (`src/calendar/calendarplugin_writer.cpp:148-167`) call
the blob ops. `SyncBackend::pushItems()`/`startSync()` are vestigial leftovers
from the pre-F1 calendar-typed architecture and are never invoked at runtime.

The real Akonadi item-write logic (working `ItemCreateJob`/`ItemModifyJob`
code) lives on `pushItems`. The methods the engine calls —
`createRecord`/`updateRecord`/`deleteRecord` — are Phase-D / Phase-L.6
**stubs** that `qWarning()` and return empty/false. Net effect: **Akonadi can
read but cannot write through the engine at all today**, for either domain.
The working write code was wired to a dead contract.

The "deferred to Phase F" comments in `akonadibackend.cpp` are a dead
forward-reference: the Phase F that happened (F0/F1/F2) was the engine-merger
unification + threading, never Akonadi blob-op completion. No plan ever owned
this work — hence this doc.

### Status docs that currently lie (to be corrected when code lands)

- `docs/phase0/04y-phase-l-status.md` and the deferred-work catalog
  `04w-deferred-work.md` entry **C.1** both claim the contacts backend landed
  "with parity to existing calendar backend ... push/fetch/delete operations."
  In fact `AkonadiContactsBackend::pushItems` is a Phase-L.6 `qWarning` stub
  (`src/contacts/akonadicontactsbackend.cpp:199`) and its blob write ops are
  stubs. These docs must be corrected in the same change that lands the code.

---

## 2. Locked scope decisions (2026-05-26)

| Decision | Choice | Consequence |
|----------|--------|-------------|
| Primary consumer | PlanStan/WildPalms real sync target | Write path + change-detection must work against a live daemon, not just compile |
| Domains | **Calendar + Contacts** | Memo/standalone-todo backends out of scope (todos partly covered under calendar MIME monitoring) |
| Collection creation | **In scope** | `createCollection` becomes a real `CollectionCreateJob`, created under a user-selected parent resource |
| Verification | **Env-gated live tests + manual** | `KALBURATOR_AKONADI_LIVE_TEST=1` integration tests; CI remains stub-only |
| `ChangeRecorder` incremental | **In scope this round** | Live/incremental dirty-tracking, layered on top of the digest backbone |

---

## 3. The shape decision (and why it is *correct*, not merely convenient)

The shape graph is a canon-hub model: a rich canonical JSON spine per domain,
peer encodings attached by edges, routed via the canonical spine
(`src/shape/transformationregistry.cpp:105-157`). The asymmetry that matters
(`src/calendar/calendarstockshapes.cpp:52-92`):

- `ical → canon` is **lossless** (`ICalToCanonStage`, empty `LossProfile`).
- `canon → ical` is **lossy** — `canonToIcalLoss()`
  (`src/calendar/icalcanonstages.cpp:739-776`) drops `onlineMeeting`,
  `eventType`, `typedProperties`; simplifies `locations`; etc.

Akonadi's native storage model for calendar *is* `KCalendarCore::Incidence`
(the in-memory iCal object); for contacts it *is* `KContacts::Addressee` ↔
vCard. The fields `canon → ical` drops are fields **Akonadi cannot store
either** — they are not in KCalendarCore. Therefore reading Akonadi as iCal and
lifting via the lossless `ical → canon` edge captures *everything Akonadi
actually holds* (including X-properties, which round-trip through iCal).

**`{calendar, ical}` and `{contacts, vcard4}` are the truthful native shapes
for Akonadi.** A bespoke `{calendar, akonadi}` encoding would have to define
content beyond iCal — and there is none — so it would be a second node with
identical content, forking the encoding space to represent the same data. We
explicitly reject it.

A bespoke rich encoding *is* the right call for a backend whose native API
exceeds iCal/vCard (e.g. a Google/Exchange JSON API carrying native
online-meeting / typed-property fields) — that is precisely what the canon
superset and the unused `ShapeContribution` edge-contribution API exist for. No
backend does this today. Akonadi is not such a backend.

The Akonadi **envelope** (`Item::remoteId`, `gid`, flags, tags, parent
collection) is storage metadata, not calendar/contacts *domain* data, so it
correctly stays out of the canon. `BackendRecord.id` = `Item::id()` (local,
stable, unique). `remoteId`/`gid` matter only for remote-backed collections and
cross-resource dedup — out of scope for WildPalms local sync.

---

## 4. The change-detection parity bar

The engine consumes exactly two change-detection mechanisms; both are
domain-neutral, so "parity with CalDAV" means hitting these, not replicating
DAV's HTTP specifics.

1. **`BackendRecord.contentHash`** (per-record). `src/engine/perrecorddiff.cpp:103-105`:
   if both records carry a non-empty hash, compare hashes; else fall back to a
   full semantic decode-and-compare. The engine **promotes both sides to
   canonical bytes before diffing** (`src/engine/syncengine.cpp:2040,2105`) and
   diffs via the domain's canonical differ (`2135-2139`) — so cross-backend
   equality is the *engine's* responsibility, and the backend owes only a
   stable, content-derived hash.
2. **`Backend::ChangeDetection`** (per-collection skip).
   `src/backend/changedetection.h:40-104`, consumed at
   `src/engine/syncengine.cpp:662-747` (`prepareSyncFastPath`). Lets the engine
   skip an entire mapping before reading any records, comparing a stored
   collection token against a fresh one. Domain-neutral; DAV backs it with the
   CTag.

**Not needed for parity** (DAV-transport-internal): per-item ETag/If-Match
(concurrency only); RFC 6578 sync-tokens (libkalburator does not use them).

### Akonadi analogues

| CalDAV concept | Akonadi mechanism | Notes |
|----------------|-------------------|-------|
| Per-item dirty flag | `Item::revision()` (`item.h:441`), `modificationTime()` | Monotonic local-change counter. Used to **memoize** contentHash, not to replace it. |
| Per-item ETag (concurrency) | `Item::revision()` server-side | `ItemModifyJob` fails on stale revision — optimistic concurrency for free, no ETag storage. |
| Per-collection CTag | **synthesized digest** | `Collection::remoteRevision()` is remote-backed-only; `statistics()` misses in-place edits. We digest a payload-free id+revision `ItemFetchJob`. |
| Incremental "changed since" | `ChangeRecorder` (persistent journal) | Layered on top of the digest backbone (Component 4). |

---

## 5. Component design

### Component 1 — Write-path relocation (core fix)

For both backends, move the real Akonadi item-write logic onto
`createRecord`/`updateRecord`/`deleteRecord`:

- **Sync↔async bridge:** blob ops are synchronous (`QString`/`bool`); Akonadi
  jobs are async. Bridge with `KJob::exec()` (runs its own nested loop).
- **Payload translation:** deserialize `BackendRecord.data` (iCal/vCard bytes)
  → `KCalendarCore::Incidence` / `KContacts::Addressee`; set as `Akonadi::Item`
  payload with the correct MIME type; run `ItemCreateJob` / `ItemModifyJob` /
  `ItemDeleteJob`. `createRecord` returns the new `Item::id()` as a string.
- **Identity for update/delete:** resolve `BackendRecord.id` (= `Item::id()`)
  to the cached `Akonadi::Item` to target the job.
- **Cache coherency:** update `m_itemsByCalendar` on success so an in-sync
  re-read is consistent without waiting on a Monitor signal.
- **Retire** the vestigial `pushItems`/`startSync` item-write code (delete).

### Component 2 — Collection creation

- Implement `createCollection` via `Akonadi::CollectionCreateJob`, setting
  content MIME types per domain (events/todos/journals for calendar; addressee
  for contacts), under a **user-selected existing parent resource**.
- Extend the config surface (`AkonadiConfigWidget` already edits provider
  display name) to choose the parent collection/resource for created
  collections.
- `KJob::exec()` bridge as in Component 1.

### Component 3 — Change-detection parity

- **`contentHash`:** SHA-256 over native bytes (existing in `loadRecords`),
  **memoized by `Item::revision()`** — recompute only when revision advances.
  Valid because revision-unchanged ⟹ native bytes unchanged ⟹ hash unchanged.
- **`Backend::ChangeDetection`:** implement
  `collectionRevision`/`collectionRevisions`/`cachedCollectionRevision`/`primeRevisionCache`,
  `persistsCollectionRevisions()==true`. Fresh token = digest over a
  **payload-free** `ItemFetchJob` (id + revision only — local DB read, no
  payload decode).
- **New persistent store:** small `collectionId → token` store (mirrors DAV's
  `CTagStore`), since `ChangeDetection` requires the backend to persist its
  revisions across runs.

### Component 4 — `ChangeRecorder` live/incremental layer

- Upgrade the existing live `Akonadi::Monitor` to a `ChangeRecorder` with a
  stable identity via `setConfig(QSettings*)` (journal at
  `<our-config>/<account>_changes.dat`; identity is purely the QSettings file
  path — no agent registration needed, confirmed in
  `changerecorder_p.cpp:45-47`).
- **Layered strictly on top of the digest backbone:**
  - Warm path: recorder queue non-empty → use it for the exact dirty item set
    (zero scan).
  - Floor: recorder empty but collection digest differs (changes during our
    downtime, which a client-side recorder does not capture) → fall back to the
    digest/full-scan. Correct always, fast when warm.

### Component 5 — Tests & doc honesty

- Env-gated live integration tests (`KALBURATOR_AKONADI_LIVE_TEST=1`):
  create/update/delete round-trip; collection creation; change-detection skip;
  `ChangeRecorder` warm-path; concurrency-conflict (stale revision).
- Correct the lying status docs (§1) in the same change that lands the code.
- Remove the stale "deferred to Phase F" comments.

---

## 6. Out of scope

- Memo and standalone-todo Akonadi backends.
- Bespoke `{calendar, akonadi}` / `{contacts, akonadi}` encodings (rejected, §3).
- `remoteId`/`gid` cross-resource identity.
- RFC 6578 sync-tokens; per-item ETag storage.
- In-process / CI-runnable fake-Akonadi harness (banked as possible follow-up;
  this round uses env-gated live tests).

---

## 7. Known dependency / risk

The engine's baseline-load filters to `blob`-domain records
(`src/engine/syncengine.cpp:2121-2123`), so baseline-driven **deletion**
detection for non-blob domains (calendar/contacts) is mid-migration and applies
to *all* backends, DAV included — not Akonadi-specific. Akonadi inherits
whatever the calendar/contacts domains get here. To be logged in
`docs/campaign/FINDINGS.md` as an inherited dependency, not solved in this
round.
