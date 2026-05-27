# Phase L — Akonadi as first-class provider

**Status:** landed 2026-05-16  
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
