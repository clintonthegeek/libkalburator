# Plan 7b — `LocalBackend` decomposition (AUDIT B3, second half)

**Audit refs:** B3 (MAJOR — "`LocalBackend` ~1300 LOC / 224-LOC header with ~55 methods…
four metadata setters where one would do"); the Plan 7 FINDINGS mirror-sketch.
**Branch:** `feature/redress-7b-localbackend-decomposition`
**Baseline at open:** `main` @ `885abea` (post-Plan-7 + Plan 8 RFC, tag v0.68), ctest **145/145**.
**State:** plan written 2026-06-10 against the current tree (1311 cpp / 224 h).

Method: identical to Plan 7 (subtract-first; net-LOC gate; protective tests first;
per-symbol plain-pattern deletion-warrant greps per the FINDINGS discipline entry).
LocalBackend is in much better shape than RemoteCalendarBackend was — no QEventLoop
boilerplate, no parallel-map triplication — so this is a smaller, surgical pass.

## Gates

- Net LOC across touched src files ≤ **−100** (1535 baseline for the pair; the shared
  iCal-codec extraction also touches `remotecalendarbackend.cpp`, counted separately).
- `localbackend.cpp` ≤ 1230; `localbackend.h` ≤ 215.
- LocalBackend-specific public methods beyond interface obligations: **13 → 4**, each
  survivor with a named consumer.
- Every task leaves the tree green (full ctest, `-j 8`).

## Consumer contract (verified 2026-06-10, one plain pattern per symbol)

| Surface | Consumer | Disposition |
|---|---|---|
| ctor `(calendarRootPath, parent)` | PlanStan `collectioncontroller.cpp:1099` | keep |
| `setcalendarRootPath` | PlanStan `collectioncontroller.cpp:1100` | keep (lower-case-c naming wart → Plan 10) |
| `setDbPath` | `tst_localbackend_blob_view.cpp:300` only — **zero PROD callers** | keep (fingerprint store's only init); FINDINGS: PlanStan never wires it, so the store is dark in production |
| `startSync` / `removeItem` / `loadCalendars` / `storeCalendars` | `SyncBackend` pure virtuals; PlanStan staging reaches `startSync` through `SyncBackend*` | keep; T1 pins the signal contract |
| `calendarColor` / `calendarDescription` / `discoveredWritable` / `capabilities` / binding metadata / operations API / IBlobBackend / ChangeDetection | interface overrides | keep |
| `calendarFingerprint`, `cachedFingerprint`, `setCachedFingerprint` | zero external callers (engine uses `ChangeDetection`) | **privatize** (mirror of Plan 7's ctag cluster) |
| `calendarDisplayName`, `setCalendarDisplayName`, `setCalendarColor`, `setCalendarDescription`, `calendarOrder`, `setCalendarOrder` | zero external callers (PlanStan's similarly-named calls are on its own collection type; the plugin test's are on its own `MinimalBackend` mock; `types/icalendarcollection.h` is an unrelated interface) | **privatize** — `updateCalendar(QVariantMap)` (an override, PlanStan-reachable) is already the public collapsed form the archived plan asked for |

Zero-caller verdicts (deletion warrant; lib + PlanStan + WildPalms, src and tests,
internal use checked against the full-file read):

| Symbol | Evidence |
|---|---|
| `buildHierarchy()` (cpp:286-294) | no-op body, zero callers |
| `writeIncidenceWithHierarchy()` (cpp:300-310) | no-op body, zero callers |
| `findParentUid` (h:214) | declared, never defined |
| `FingerprintStore::clear()` + `clearAll()` (cpp:102-124) | only `get`/`set` are used |
| `m_pendingWriteCount` (h:207, cpp:529/535/543) | written and decremented, never read |
| `#include <QCoreApplication>` (cpp:21) | sole mention (the `processEvents` it served was removed) |

## Tasks

### T1 — Protective tests first (inv 6)

`startSync` (the AsyncFileWriter path), `removeItem`, and `updateCalendar` have no
default-lane coverage; T3/T4 touch their internals and PlanStan staging is a PROD
caller. New `tests/calendar/tst_localbackend_writepaths.cpp` (clone the
`tst_localbackend` CMake stanza — `kalburator_add_calendar_test`), slots:

- `startSync_creations_write_files_with_signal_contract` — 2 creations into a temp
  root; spy `writeStarted(calId, 2)`, `writeProgressChanged` ticks,
  `syncCompleted(collectionId)` (async — `QTRY_COMPARE_WITH_TIMEOUT`); both `.ics`
  files exist with parseable content.
- `startSync_deletions_remove_files` — seed a file; stagedDeletions `{uid→""}`;
  file gone; `syncCompleted` fires.
- `startSync_empty_stages_completes_immediately` — synchronous `syncCompleted`,
  no `writeStarted`.
- `startSync_null_calendar_completes` — warns + synchronous `syncCompleted`.
- `removeItem_deletes_file` / `removeItem_missing_file_is_noop`.
- `updateCalendar_roundtrips_metadata` — set displayName/color/description/
  displayOrder via `updateCalendar(QVariantMap)`; read back through the public
  overrides `calendarColor`/`calendarDescription` and the VDir files
  (`CalendarMetadataManager` on the same dir); `calendarUpdated` emitted.
- `discoveredWritable_respects_readonly_marker` — touch a `readonly` file; false.

Falsifiability probe (not committed): suppress the null-calendar `syncCompleted`
emit → that slot must go red; restore. Expect suite 145 → 146.

### T2 — Delete the verified-dead code

`buildHierarchy` + `writeIncidenceWithHierarchy` (bodies + h:210-211 decls),
`findParentUid` decl (h:213-214), `FingerprintStore::clear`/`clearAll`,
`m_pendingWriteCount` (decl + 3 sites; the null/empty-uid `startSync` branches keep
their `continue`), `QCoreApplication` include. While in `storeCalendars`: anonymous
params + drop the `// Your implementation here` lie (it is a deliberate no-op —
say so, mirroring RemoteCalendarBackend's stub comment). ≈ −60 LOC. Suite green.

### T3 — Privatize the fingerprint + metadata clusters; `metadataFor` helper

1. Move to `private:`: `calendarFingerprint`, `cachedFingerprint`,
   `setCachedFingerprint` (the `ChangeDetection` overrides stay the public face;
   move their three inline bodies from the header to the cpp, as Plan 7 T6 did).
2. Move to `private:`: `calendarDisplayName`, `setCalendarDisplayName`,
   `setCalendarColor`, `setCalendarDescription`, `calendarOrder`,
   `setCalendarOrder` (used by `updateCalendar`/`createCalendar` internally; the
   public overrides `calendarColor`/`calendarDescription` remain).
3. Private helper collapsing the 8× metadata-manager boilerplate:

```cpp
// localbackend.h (private)
std::optional<CalendarMetadataManager> metadataFor(const QString &calendarId) const;
// localbackend.cpp — nullopt when calendarId or root path is empty (the guard
// every per-property method duplicated)
```

   Each accessor body becomes `auto md = metadataFor(calendarId); if (!md) return …;
   return md->color();` etc. (If `CalendarMetadataManager` is not movable, the
   helper degrades to returning the validated path:
   `std::optional<QString> metadataDirFor(...)` — decide at implementation against
   the real class; both shapes kill the duplication.) ≈ −30 LOC. Suite green.

### T4 — Shared iCal codec header; path-construction dedup

1. New `src/calendar/icalcodec.h` (header-only, `Kalburator::Sync` namespace,
   `inline` free functions — registered in the root CMakeLists header list):
   `icalFromIncidence(const KCalendarCore::Incidence::Ptr&) -> QByteArray`,
   `incidencesFromIcal(const QString&)` and `(const QByteArray&)` — moved verbatim
   from `remotecalendarbackend.cpp`'s anonymous namespace (which then includes the
   header and drops its copies). LocalBackend rewires: `fetchItems`'s parse loop →
   `incidencesFromIcal(data)` (drops the per-file temp-calendar dance; merged
   parse-failure/empty semantics, same caveat as Plan 7 T4), `pushItems`'s
   serialization → `icalFromIncidence(item)`.
   Other backends (`decsyncbackend`, `subscriptionbackend`, `mockbackend`,
   `orgbackend`) keep their own dances — out of scope (inv 8), FINDINGS note.
2. LocalBackend path helpers: `icsPathFor(calendarId, uid)` collapses the three
   hand-built `root + "/" + cal + "/" + uid + ".ics"` strings (`removeItem`,
   `getRawIcs`, `setRawIcs`) onto `filePathForCalendar`; private
   `recordPathFor(recordId)` collapses the triplicated subdir scan in
   `loadRecord`/`updateRecord`/`deleteRecord`. ≈ −45 LOC across the two backends.
   Suite green (incl. the untouched-verbatim Plan 7 contracts — `icalcodec.h` is a
   pure move of those bodies).

### T5 — Close-out

Metrics vs gates into this file's Outcome; FINDINGS (PlanStan never wires
`LocalBackend::setDbPath` → fingerprint store dark in PROD — candidate one-liner for
their side, referenced from the Plan 8 RFC thread; the remaining per-backend codec
dances; `setcalendarRootPath` naming → Plan 10); STATUS (B3 fully resolved — both
halves; Plan 8 = awaiting PlanStan ack); AUDIT B3 closing annotation. Gates: full
ctest; PlanStan ctest (LocalBackend is in their build — expect the same failed-set:
21 Not-Run GUI + their own `203744a4`-induced `tst_loader_empty_backends`).
Merge `--no-ff`, push.

## Risks

- **AsyncFileWriter timing in T1's startSync tests** — completion is signalled from
  a worker thread; use `QTRY_*` waits generously (the writer stops/restarts per
  startSync call).
- **`CalendarMetadataManager` copyability** for `metadataFor` — fallback shape
  documented in T3.
- **Codec-header move** must be a pure move (no behavior drift) — Plan 7's T1/T4
  pins plus the convergence/blob_view contracts cover the RemoteCalendarBackend
  side; T1 covers LocalBackend.

## Outcome

_To be filled at T5._
