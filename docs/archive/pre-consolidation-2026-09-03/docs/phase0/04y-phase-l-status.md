# Phase L — Akonadi as first-class provider

**Status:** provider + contacts skeleton landed 2026-05-16; full functionality
2026-05-26; **Phase L.5 per-collection scoped-backend hardening 2026-06-12 – 06-14
(v0.74 → v0.76)** — see the dated sections below.  
**Tag:** `v0.41-phase-l-akonadi-provider` (pending user `git tag` authorization)  
**Plan:** `~/dev/refactor-engine-merger/2026-05-15-phase-l-akonadi-plan.md`

## What landed

- `AkonadiProvider` (implements `IProvider`) — discovers calendar and addressbook
  collections via `Akonadi::CollectionFetchJob`; `createBackend()` dispatches by
  MIME type (calendar → `AkonadiBackend`, contacts → `AkonadiContactsBackend`).
- `AkonadiContactsBackend` — new *skeleton*; identity, read (`loadRecords` via
  `ItemFetchJob`), and Monitor wiring for `KContacts::Addressee`. The write path
  (`createRecord`/`updateRecord`/`deleteRecord`), `createCollection`, and
  `Backend::ChangeDetection` were **stubs** at Phase L; real implementations landed
  2026-05-26 (see §2026-05-26 below).
- `AkonadiBackendContribution` + `AkonadiProviderPlugin` — registered in
  `stock_plugins.cpp` under `#ifdef HAVE_AKONADI`; WildPalms CMake changed from
  forced-OFF to `option()` so callers can enable it.
- `AkonadiConfigWidget` — minimal `QWidget` for provider display-name editing,
  returned by `AkonadiProvider::createConfigWidget()`.
- `AccountController::setProviderEnabled(id, bool)` — flips `BackendConfiguration::enabled`
  and fans out to all `SyncMapping` entries referencing that provider (by
  `sourceBackend/targetBackend` prefix match). Load-time re-apply on profile load.
- `AccountController::setMappingEnabled(id, bool)` — flips one mapping's `"enabled"`
  field in the profile JSON array directly.
- `AccountsPage` per-provider `QCheckBox` wired to `setProviderEnabled`.
- `MappingRowDialog` `enabled` checkbox was pre-existing; JSON round-trip verified.
- Pre-existing `emit` → `Q_EMIT` + `connect()` → `QObject::connect()` cleanup in
  `accountcontroller.cpp`, `accountspage.cpp`, `palmruntime.cpp`.

## What deferred

- PlanStan account-add UI (still raw config). See `04w-deferred-work.md`.
- Automatic Akonadi failover / hotswap — rejected as over-abstraction.
- Advanced Akonadi collection filtering (hidden/shared resources).

## Build notes

- Default build: `KALBURATOR_HAVE_AKONADI=OFF` — Phase L code excluded, all existing
  tests unaffected.
- Akonadi build: `cmake -DKALBURATOR_HAVE_AKONADI=ON -DCMAKE_MODULE_PATH=/usr/share/ECM/modules`
  (ECM path required on Arch/Manjaro; `KPim6AkonadiConfig.cmake` calls `include(ECMMarkAsTest)`).
- clangd: `.clangd` `CompilationDatabase` must point at `build-akonadi/` for
  Akonadi-gated code to be visible. The repo `.clangd` was updated accordingly.
- Live Akonadi tests guarded by `KALBURATOR_AKONADI_LIVE_TEST=1` env var (skip in CI).

---

## 2026-05-26 — full functionality landed (branch `feature/akonadi-full-functionality`)

Phase L shipped provider wiring, discovery, and the contacts skeleton. The following
landed 2026-05-26, bringing both the calendar and contacts backends to full sync-target
parity:

- **`createRecord` / `updateRecord` / `deleteRecord`** — real `ItemCreateJob` /
  `ItemModifyJob` / `ItemDeleteJob` via `KJob::exec()` sync bridge. Supersedes the
  vestigial `pushItems` path (now marked vestigial; full removal deferred to the
  `SyncBackend` ABI cleanup).
- **`createCollection`** — real `CollectionCreateJob` under a user-selected parent
  resource. Implemented for both calendar (MIME: event/todo/journal) and contacts
  (MIME: `KContacts::Addressee::mimeType()`).
- **`Backend::ChangeDetection`** — payload-free `ItemFetchJob` (id + revision only,
  no decode) digested via `AkonadiRevisionStore`. Persisted across restarts; lets the
  engine skip unchanged collections without reading any records.
- **`contentHash` memoization** — `m_hashMemo` keyed by `Item::revision()`; avoids
  re-serializing + re-hashing items whose Akonadi revision is unchanged.
- **Cross-backend record identity fix** — `BackendRecord.id` now set from the
  iCal UID / vCard UID (not the local Akonadi item id), matching `RemoteCalendarBackend`
  and enabling engine-level cross-backend diffing.

**Design and implementation plan:**
`docs/2026-05-26-akonadi-full-functionality-design.md`,
`docs/2026-05-26-akonadi-full-functionality-plan.md`

**Note:** the `ChangeRecorder` warm-path (incremental dirty-tracking on top of the
digest backbone) was scoped in the design but **deferred** — see `docs/campaign/FINDINGS.md`
O14 for rationale. The digest backbone is the correctness floor and is sufficient.

---

## 2026-06-12 – 06-14 — Phase L.5: per-collection scoped backends (on-device hardening)

WildPalms' first on-device exercise of the per-collection ("Phase L.5") scoped
Akonadi backends — `AkonadiProvider::createBackend(collectionId)` builds a fresh
backend scoped to ONE collection via `cfg["akonadiCollectionId"]` — surfaced three
defects the earlier whole-store path never hit. All fixed; the calendar path is
verified on device (Akonadi collection 54 → 83 events into the WP hub).

- **v0.74 — scoped read never resolved its collection.** A scoped backend stored
  `m_scopedCollectionId` but never populated `m_collections` (only the dead
  `loadCalendars()` did), so every `fetchItems`/`createRecord` fast-failed
  "Unknown calendar/collection". Fix: `ensureScopedCollection()` lazily seeds an
  id-only `Akonadi::Collection` (the server resolves it — no discovery round-trip),
  in both `AkonadiBackend` and `AkonadiContactsBackend`. Guard test
  `tests/calendar/tst_akonadi_scoped_collection.cpp`. Shipped with the engine
  "Fix B" (`SyncOperation::NotSupported` — a genuine fetch failure now fails the
  mapping instead of a silent 0-record success, closing a clobber-wipe data-loss
  footgun). Response: `docs/2026-06-12-akonadi-scoped-backend-fix-response.md`.
- **v0.75 — contacts id-prefix mismatch.** `AkonadiProvider` emits `"akonadi-<id>"`
  for ALL collections, but `AkonadiContactsBackend` parsed only
  `"akonadi-contacts-<id>"`, so contacts collections never resolved (0 records;
  calendar was immune). Fix: align the contacts prefix to `"akonadi-"`. The v0.74
  guard test had masked it (it invented the unused scheme); corrected + added a
  provider↔backend agreement test. Response:
  `docs/2026-06-14-akonadi-contacts-id-prefix-fix-response.md`.
- **v0.76 — shared collection-id helper.** Extracted the `"akonadi-<id>"` scheme to
  `src/sync/akonadicollectionid.h` (one round-trip helper used by the provider AND
  both backends); removed the duplicated `AKONADI_PREFIX` / `AKONADI_CONTACTS_PREFIX`
  constants, so producer↔consumer agreement is structural. Default-profile unit test
  `tests/sync/tst_akonadicollectionid.cpp`.

**Verification:** default ctest 150/150, Akonadi profile 159/159; PlanStan pretest
88/88 buildable (the 13 EXCLUDE_FROM_ALL integration tests are unrelated —
pre-existing API-drift casts fixed PlanStan-side in `0d0072b6`).

**Still open (tracked, not blocking):**
- Contacts N>0 read of a real address book — WP on-device step (acceptance criterion 2).
- The engine first-sync fast path (`dispatchFirstSync`, OneWayUpload + same-shape) is
  NOT fetch-gated, so a genuine fetch failure on a silent-`loadRecordsOrError` backend
  is still swallowed there; WP's real Akonadi routes are TwoWay (unified path, covered).
  See the v0.74 response doc.
