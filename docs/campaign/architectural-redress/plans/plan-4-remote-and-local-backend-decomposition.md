# Plan 4 — Decompose `RemoteCalendarBackend` and `LocalBackend`

**Audit refs:** B3 (MAJOR)
**Depends on:** Plan 2 (sync/ cycle broken so backends can be split without inverted
includes), Plan 3 (services/ exists so the extracted I/O bits have a home).
**Branch:** `feature/redress-4-backend-decomp`
**State:** Architectural plan + first-task detail. Subsequent tasks written when Plan 2
and Plan 3 land and the post-cycle-break interfaces are visible.

## Goal

Each backend class answers one question (INVARIANTS §4). Extract the orthogonal
collaborators that currently share a class: CTag/fingerprint cache, discovery cache,
metadata factory, capability mixin duplication.

## Problem (from AUDIT B3)

### `RemoteCalendarBackend` — 2649 cpp + 427 h, ~58 public methods

Concerns sharing one class:

1. **CTag management** — `ctag`, `setCtag`, `clearCtag`, `fetchAllCtags`,
   `primeCtagCache`.
2. **Discovery cache** — `discoveredUrl`, `discoveredColor`, `discoveredCtag`,
   `discoveredSupportsEvents`, `discoveredWritable`, ~6 getters total.
3. **Calendar CRUD** — `createCalendar`, `updateCalendar`, `deleteCalendar`.
4. **Incidence operations** — `fetchItems`, `pushItems`, `deleteItems`, `storeCalendars`.
5. **Raw ICS access** — `getRawIcs`, `setRawIcs`.
6. **`IBlobBackend` impl** — `recordId`, `recordData`, `exists`, etc.
7. **Capabilities** — `backendCapabilities`, `bindingMetadataKeys`,
   `discoveredCalendarType`.
8. **`ChangeDetection` mixin** — `collectionRevision`, `cachedCollectionRevision`,
   `primeRevisionCache` (which trivially delegate to CTag methods — duplicated API).

### `LocalBackend` — 1300 cpp, ~35 public methods

Same shape, smaller scale: fingerprint cache (`cachedFingerprint`,
`setCachedFingerprint`), `ChangeDetection` mixin duplicating fingerprint, four separate
metadata setters (`setCalendarColor`, `setCalendarDisplayName`,
`setCalendarDescription`, `setCalendarOrder`) where one would do.

## Approach

Extract collaborators **without changing the backend's public method names** — call
sites continue to compile against the same surface; the backend forwards to the
collaborator. Once the collaborator is owned externally (the backend takes it via DI
rather than constructing internally), the duplicated APIs can be deleted.

The collaborators to extract:

- `CalDavCTagStore` (or extend the existing one if `CTagStore` already lives in sync/)
  with the full lifecycle (fetch, prime, get, set, clear, fetch-all).
- `CalendarDiscoveryCache` — a struct `DiscoveredCalendarMetadata { url, color, ctag,
  supportsEvents, supportsJournals, supportsTodos, writable, ... }` plus a
  `QHash<QString, DiscoveredCalendarMetadata>`. Six getters become one DTO accessor.
- `MetadataFactory` — the binding-metadata population logic currently inline in
  Remote/Local. Likely one method that takes a backend handle and a calendar ID and
  returns the metadata map.
- `FingerprintCache` for LocalBackend (mirror of CTagStore for Remote).
- `VDirMetadataIO` — the four metadata setters collapse into one
  `updateCalendarMetadata(collectionId, calendarId, CalendarMetadataPatch)` whose impl
  delegates to the existing `services/calendarmetadatamanager` (post Plan 3).

The `ChangeDetection` mixin's purpose was to let the engine query revision tokens
generically. Per AUDIT, its only implementations delegate trivially. Decision: keep
`ChangeDetection` as the engine-facing interface (the engine has a reason to be
abstract over it); delete the redundant `collectionRevision()` / `cachedCollection
Revision()` duplicate names from the backend's *own* public surface — the engine still
sees them via the interface, but the backend doesn't expose two-names-one-concept.

## Tasks

### Task 1 — Protective integration tests

Before any extraction:

1. In `tests/calendar/`, add `tst_remotecalendarbackend_decomposition.cpp` with
   integration scenarios that exercise the public surface the decomposition must
   preserve:
   - CTag fetch + prime + get round-trip.
   - Discovery cache population + readback across all six getters.
   - Metadata factory output stability (capture the current binding-metadata map for a
     known input and pin it; this is the contract WildPalms reads).
   - `pushItems` end-to-end against a stub CalDAV server (use existing stubs if any;
     otherwise mock at the KDAV job boundary).
2. `tst_localbackend_decomposition.cpp` — fingerprint round-trip, metadata setter
   round-trip across all four properties, `pushItems` against a tmp dir.
3. Run against unmodified backends. Verify they pass.
4. Deliberately break each collaborator-to-be (e.g. comment out the CTag set in
   `setCtag`) and verify the relevant test goes red. This is the falsifiability proof
   per INVARIANTS §6.

### Task 2 — Extract `CalDavCTagStore` (Remote)

(Detail written after Plan 2 lands. Sketch: create `calendar/caldavctagstore.{h,cpp}`,
move CTag state and methods, inject via ctor, backend forwards calls. Confirm with
integration test.)

### Task 3 — Extract `CalendarDiscoveryCache` and collapse getters (Remote)

(Detail written after Task 2. Sketch: introduce `DiscoveredCalendarMetadata` struct,
add `DiscoveredCalendarMetadata discovered(calendarId) const` to backend, mark the six
individual getters `[[deprecated]]`, delete after Plan 8 sweep.)

### Task 4 — Extract `FingerprintCache` (Local) — mirror of Task 2

### Task 5 — Collapse Local's four metadata setters into one

(Detail written after Tasks 2–4. Sketch: introduce `CalendarMetadataPatch` (struct of
optionals), one `updateCalendarMetadata(collectionId, calendarId, patch)`, delegate to
`services/calendarmetadatamanager`. Old setters become `[[deprecated]]` shims; Plan 8
deletes.)

### Task 6 — Remove duplicated `ChangeDetection` surface from backend's own public API

(Detail written after Tasks 2 and 4. Sketch: the engine reaches change-detection via
the `ChangeDetection*` interface getter; the backend stops exposing
`collectionRevision()` as its own public method — only the interface impl path remains.)

### Task 7 — Re-run tests and close

1. Full ctest including the protective tests from Task 1.
2. PlanStan ctest (reachable surface).
3. WildPalms smoke if locally runnable; pin metadata factory output against pre-plan
   capture.
4. Confirm `RemoteCalendarBackend.cpp` is under 2000 LOC and the public method count is
   ≤ 25.
5. Update FINDINGS: cross out B3 entries with closing commit hash.
6. Open Plan 5.

## Files affected (anticipated)

- `src/calendar/caldavctagstore.{h,cpp}` — **new**.
- `src/calendar/calendardiscoverycache.{h,cpp}` — **new**, with
  `DiscoveredCalendarMetadata` struct.
- `src/calendar/localfingerprintcache.{h,cpp}` — **new**.
- `src/calendar/calendarmetadatapatch.h` — **new** struct.
- `src/calendar/remotecalendarbackend.{h,cpp}` — slimmed; collaborators injected.
- `src/calendar/localbackend.{h,cpp}` — slimmed; collaborators injected.
- `tests/calendar/tst_remotecalendarbackend_decomposition.cpp` — **new**.
- `tests/calendar/tst_localbackend_decomposition.cpp` — **new**.

## Acceptance criteria

- `RemoteCalendarBackend.cpp` LOC < 2000.
- `RemoteCalendarBackend` public method count ≤ 25.
- `LocalBackend.cpp` LOC < 1000.
- Six `discoveredX()` getters collapsed to one `discovered()` returning a DTO; old
  names marked `[[deprecated]]` for Plan 8 sweep.
- Four metadata setters on Local collapsed to one `updateCalendarMetadata(patch)`; old
  names marked `[[deprecated]]`.
- `ChangeDetection`-mixin methods no longer appear in backend's own public surface
  (only via the interface).
- Protective tests pass; full ctest passes; PlanStan ctest baseline holds.

## Risks

- **WildPalms binding-metadata contract.** The metadata factory output is what
  WildPalms's per-category virtual sub-collections consume; per INVARIANTS §10, this
  must not regress. Task 1's pinning capture is the safety belt.
- **CTag store may already exist partially in sync/.** Check before creating a new one;
  if so, extend rather than duplicate.
- **Discovery cache invariants.** The six getters today might return subtly different
  fall-back values on cache miss; the DTO accessor must reproduce each fall-back. Pin
  in the integration test before the extraction.

## Estimated effort

5–7 sessions. The extraction itself is mechanical; the binding-metadata pinning and
WildPalms verification gate is the time governor.
