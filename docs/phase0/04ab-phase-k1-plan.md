# Phase K.1 — capability interfaces (plan)

**Status:** **landed 2026-05-09 — commit `017ac94`, tag
`v0.30-phase-k1-capabilities` applied.** Build and test gates met.
Design doc: `04ab-phase-k-engine-generalization-design.md` §4 (K.1
row). Convention doc: `04ac-phase-k-semantic-cleansing-proposal.md`.

**Outcome (post-landing):**
- T1, T2: capability headers added at `src/backend/changedetection.h`
  and `src/backend/resourcelinearization.h`. ✓
- T3: CMake module `KALBURATOR_BACKEND_HEADERS` registered. ✓
- T4: `RemoteCalendarBackend` implements `Backend::ChangeDetection`
  via thin delegations to `fetchAllCtags` / `ctag` / `primeCtagCache`. ✓
- T5: `LocalBackend` implements `Backend::ChangeDetection` via
  `calendarFingerprint` / `cachedFingerprint` / `setCachedFingerprint`. ✓
- T6: `RemoteContactsBackend` opts into `Backend::ChangeDetection`
  with empty stubs (CardDAV CTag wiring deferred). ✓
- T7: No backend implements `Backend::ResourceLinearization` yet
  (interface exists; future Palm backend will adopt). ✓
- T8: Build green; ctest matches baseline (same 2 flaky timing
  failures: `tst_calendar_sync_full` #27, `tst_engine_subset_dispatch`
  #36). No regression. ✓
- T9: Commit `017ac94`; tag `v0.30-phase-k1-capabilities` applied. ✓

**Goal:** Introduce the two capability interfaces the engine will
consume in K.2 to retire its `qobject_cast<RemoteCalendarBackend*>`
/ `qobject_cast<LocalBackend*>` fast-path. **No behavior change in
K.1.** The interfaces exist and a few backends opt into them; the
engine ignores them until K.2.

**Branch:** `refactor/engine-merger` (in libkalburator worktree).

**Naming convention (from §2.4 of the cleansing proposal):**
- No `I` prefix.
- No `Abstract` prefix on these (they're not QObject-derived; bare
  descriptive names per Qt6 convention for non-QObject interfaces).
- Live under new `Kalburator::Backend::` namespace, in new
  `src/backend/` directory. K.4 populates the rest of `src/backend/`;
  K.1 establishes the directory with these two files.

---

## Tasks

### K.1.T1 — Create `src/backend/changedetection.h`

Pure abstract, non-QObject. Declares:
- `QString collectionRevision(const QString &collectionId)` — current
  revision token (CTag, fingerprint, etc.). Empty = "I cannot answer
  cheaply right now."
- `QMap<QString, QString> collectionRevisions(const QStringList &collectionIds)`
  — batched form. Default impl loops over `collectionRevision`.
- `QString cachedCollectionRevision(const QString &collectionId)` const
  — last persisted revision for this collection.
- `void primeRevisionCache(const QMap<QString, QString> &cache)` —
  optional; default no-op for backends that don't persist.
- `bool persistsCollectionRevisions() const { return true; }` — default
  true (most backends that implement this interface do persist).

### K.1.T2 — Create `src/backend/resourcelinearization.h`

Pure abstract, non-QObject. Declares:
- `QString linearizationKey() const { return {}; }` — non-empty key
  means "mappings touching this resource must run serially."

Default-empty body so it can be virtual-with-default; this lets
backends inherit but only override if they need linearization.

### K.1.T3 — Add `src/backend/` module to CMake

- New `set(KALBURATOR_BACKEND_HEADERS ...)`.
- Headers added to `add_library(kalburator STATIC ...)`.
- No new `.cpp` files yet (interfaces are header-only).

### K.1.T4 — `RemoteCalendarBackend` implements `Backend::ChangeDetection`

Hook the existing CTag surface (`ctag(calId)`, `fetchAllCtags(ids)`,
`primeCtagCache(map)`) onto the interface methods:

| Interface method | Existing implementation |
|---|---|
| `collectionRevision(calId)` | calls `fetchAllCtags({calId})` and returns the value (single-collection slow path; engine prefers batched form) |
| `collectionRevisions(calIds)` | direct delegation to `fetchAllCtags(calIds)` |
| `cachedCollectionRevision(calId)` | direct delegation to `ctag(calId)` |
| `primeRevisionCache(map)` | direct delegation to `primeCtagCache(map)` |

Multiple inheritance: `class RemoteCalendarBackend : public SyncBackend, public Backend::ChangeDetection`.
No QObject-diamond risk because `Backend::ChangeDetection` is
non-QObject.

### K.1.T5 — `LocalBackend` implements `Backend::ChangeDetection`

Same shape with fingerprints:

| Interface method | Existing implementation |
|---|---|
| `collectionRevision(calId)` | calls `calendarFingerprint(calId)` (the fresh-fingerprint call) |
| `cachedCollectionRevision(calId)` | calls `cachedFingerprint(calId)` |
| `primeRevisionCache(map)` | iterates and calls `setCachedFingerprint(id, fp)` for each |

### K.1.T6 — `RemoteContactsBackend` opts into `Backend::ChangeDetection` (returning empty)

CardDAV CTag is not currently implemented on `RemoteContactsBackend`.
Wiring it up is real new code (PROPFIND for `cs:getctag`, parse, cache).
**Out of K.1 scope.**

K.1 has `RemoteContactsBackend` declare the inheritance and override
`collectionRevision()` to return empty string. This is safe: empty
revision means "always sync" — the engine's K.2 fast-path treats
that as "no skip eligible," which is the current behavior anyway.

The CardDAV CTag implementation becomes a separate small task
(could be K.1.5 or fold into K.4). Documented as a TODO in the
file.

### K.1.T7 — No backend implements `Backend::ResourceLinearization` yet

The interface exists but no concrete backend overrides it (default
empty key). When a Palm backend lands, it implements this. K.1 just
establishes the contract.

### K.1.T8 — Build + test gate

- `cmake -S libkalburator -B libkalburator/build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`.
- `cmake --build libkalburator/build -- -j 10` (per user's memory: cap at 10).
- All existing tests pass (`ctest --test-dir libkalburator/build` or
  the worktree's `verify-all.sh` once that's run; `verify-all.sh`
  also checks PlanStan + WildPalms which haven't been touched yet
  and should be unchanged).

### K.1.T9 — Commit + tag

- Commit message: `Phase K.1: introduce ChangeDetection +
  ResourceLinearization capability interfaces`.
- Tag `v0.30-phase-k1-capabilities` per the tag plan in
  `CURRENT-STATUS.md` (replaces the freed `v0.30-phase-j-wildpalms-providers`
  slot).
- Update `CURRENT-STATUS.md` Recently committed section.

---

## Falsifiable verification

After K.1.T9:

- `grep -rn 'class ChangeDetection\b' libkalburator/src/` returns
  the file at `src/backend/changedetection.h` and nothing else.
- `grep -rn 'class ResourceLinearization\b' libkalburator/src/`
  same shape.
- `grep -rn ': public.*ChangeDetection' libkalburator/src/` returns
  3 entries (`RemoteCalendarBackend`, `LocalBackend`,
  `RemoteContactsBackend`).
- `grep -rn 'qobject_cast<RemoteCalendarBackend\|qobject_cast<LocalBackend' libkalburator/src/engine/` UNCHANGED — engine still uses qobject_casts. K.2 retires them.
- All existing tests pass (no regression).

---

## What K.1 does NOT do

- **Does not modify the engine.** No fast-path changes; engine
  still qobject_casts to concrete backend types. K.2 lands those.
- **Does not add CardDAV CTag.** Real CTag-for-contacts is its own
  line item.
- **Does not introduce `Backend::RecordRevision`** (per-record
  revision capability, e.g. ETag). Deferred to K.5+.
- **Does not move existing files** into `src/backend/`. K.4 does
  the bulk move when it lifts calendar virtuals off `SyncBackend`.

K.1 is intentionally tiny.
