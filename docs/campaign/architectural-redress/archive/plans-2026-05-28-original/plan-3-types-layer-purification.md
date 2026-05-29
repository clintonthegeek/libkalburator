# Plan 3 — Purify `types/`: introduce `models/` and `services/`

**Audit refs:** B2 (CRITICAL)
**Depends on:** Plan 2 (domain dirs ready to receive domain-typed interfaces; sync/
ready to receive sync-typed services).
**Branch:** `feature/redress-3-types-purification`
**State:** Task-level detail through Task 3; later tasks pending Plan 2 land.

## Goal

`types/` becomes truthful to its name (INVARIANTS §2): only value types, simple enums,
and lightweight invariant-checking helpers. Behaviour-carrying classes move to
`src/models/`; I/O-carrying classes move to `src/services/`; domain-typed interfaces
move to the relevant domain dir.

## Problem (from AUDIT B2)

- `types/logicalcalendar.h` (658 LOC): JSON ser/deser, validation, binding-promotion.
- `types/calendarmetadatamanager.{h,cpp}`: atomic file writes (VDir spec).
- `types/crashjournal.{h,cpp}`: JSON crash-recovery persistence.
- `types/backendconfiguration.{h,cpp}`: 200+ LOC JSON ser/deser.
- `types/incidencelock_registry.{h,cpp}`: lock state management.
- `types/iincidenceregistry.h`: `#include <KCalendarCore/Incidence>` — pulls
  KCalendarCore into every consumer of types/.
- `types/logicalcalendar.h:14` and `types/iincidenceregistry.h`: `#include "shape.h"` —
  the lowest layer depending on shape/. Inverted.

## Approach

Three new layers established, each with a single answer to "what is this":

- `src/types/` — value types and POD-with-helpers only. No I/O, no JSON, no SDK
  includes. The header limit per file is implicit: if you're past 200 LOC, you're in the
  wrong layer.
- `src/models/` — types with non-trivial behaviour: validation that depends on context,
  promotion/binding logic, computed properties that walk a domain. Models may
  `#include` types/ but **not** services/, sync/, or engine/.
- `src/services/` — infrastructure: file I/O, JSON persistence, lock management, crash
  journals. Services may `#include` types/, models/, and Qt/KF, but **not** engine/ or
  domain dirs.

Domain-typed interfaces (currently `IIncidenceRegistry`, `IIncidenceSource`) move to the
relevant domain dir (`calendar/` for incidence-typed) since they are domain contracts,
not neutral types.

Per Plan 1's discipline: write the protective test that exercises the public surface
through its current home before moving the file. The move is silent if the test stays
green; the move is wrong if it goes red.

## Tasks

### Task 1 — Establish the three layer directories

1. Create `src/models/` and `src/services/` (empty for now, no CMakeLists changes
   needed until files arrive).
2. Update root `CMakeLists.txt` source globs to include the new directories. Verify
   build with the existing tree (no-op change).

### Task 2 — Move domain-typed interfaces out of types/

These are not types and not neutral; they are calendar-domain contracts.

1. `types/iincidenceregistry.h` → `calendar/iincidenceregistry.h`. Update all includes.
   Per INVARIANTS §1, no `types/` consumer should reach for it; if any does, that
   consumer is also misplaced and needs to be triaged (FINDINGS entry).
2. `types/iincidencesource.h` → if Plan 9 has not yet deleted it as dead code, move to
   `calendar/iincidencesource.h`. (Coordinate with Plan 9.)
3. `types/iincidenceregistry.h:#include <KCalendarCore/...>` becomes a calendar/-local
   include, no longer leaking into every types/ consumer.
4. Build; run tests.

### Task 3 — Move `LogicalCalendar` to `models/`

The class is a `model`: it carries behaviour (validation, binding promotion, computed
properties) but no I/O.

1. Move `types/logicalcalendar.{h,cpp}` → `models/logicalcalendar.{h,cpp}`.
2. The JSON ser/deser **functions** in this file (`logicalCalendarFromJson`,
   `logicalCalendarToJson`) move to `services/logicalcalendarjson.{h,cpp}`. They are
   I/O-adjacent (they encode/decode at the persistence boundary) and the split is what
   makes the layering honest: a model exists in memory; a service crosses the boundary.
3. `models/logicalcalendar.h` removes its `#include "shape.h"` by replacing the shape-
   typed members with forward-declared opaque references; the .cpp keeps the include.
   If the public surface genuinely needs the shape type by value, that's an audit
   finding and means the type is in the wrong layer — but per the audit, the shape
   include is used for `DomainId`, which moves down to types/ in Task 5.
4. Update all consumers (probably ~20 .cpp files); use a `git grep -l "types/
   logicalcalendar.h"` pre-pass.
5. Build; run tests.

### Task 4 — Move I/O services to `services/`

1. `types/calendarmetadatamanager.{h,cpp}` → `services/calendarmetadatamanager.{h,cpp}`.
2. `types/crashjournal.{h,cpp}` → `services/crashjournal.{h,cpp}`.
3. `types/incidencelock_registry.{h,cpp}` → `services/incidencelockregistry.{h,cpp}`
   (also rename: underscore → camelCase to match repo convention).
4. `types/backendconfiguration.cpp` — keep `BackendConfiguration` *as a value type* in
   `types/backendconfiguration.h`; move only the JSON ser/deser functions into
   `services/backendconfigurationjson.{h,cpp}`. Same pattern as LogicalCalendar.
5. Update all consumers.
6. Build; run tests.

### Task 5 — Move `DomainId` (and any neutral identity types) from `shape/` to `types/`

The audit's "types/ depends on shape/" finding can be resolved by relocating the small
identity types shape/ exposes — they belong in types/ where every layer can reach them.

1. Identify the types shape/ exports that the prior types/ consumers wanted:
   `DomainId`, possibly `EncodingId`. Confirm by grep:
   ```
   grep -rn "DomainId\|EncodingId" src/types/ src/calendar/ src/contacts/
   ```
2. Move just these identity types to `types/domain.h` (or split `types/domainid.h` if
   `EncodingId` belongs with the shape graph proper — judgement call; lean toward
   moving both if both are used by non-shape consumers).
3. `shape/shape.h` re-exports them (or `#include`s from types/) so existing shape/
   callers compile unchanged.
4. Confirm no `#include "shape.h"` remains in `types/` or `models/`.

### Task 6 — Sweep remaining types/ headers

For each `.h` left in `types/`:

1. Read its public surface. If it has any method that opens a file, parses JSON, holds
   a `QMutex` as state, or `#include`s a domain SDK — it doesn't belong in types/. Move
   per the rules above.
2. The expected survivors: `synctypes.h`, `calendartype.h`, `backendrecord.h`,
   `collectioninfo.h`, `logicalcalendar`-related POD if any, `backendconfiguration.h`
   (the type, not the JSON functions), and similarly slim helpers.
3. Each remaining types/ header should be readable in under a minute.

### Task 7 — Re-run tests and close

1. Full ctest.
2. PlanStan ctest (reachable headers).
3. Grep audit:
   - `grep -rln "<KCalendarCore" src/types/` returns empty.
   - `grep -rln '#include "shape.h"' src/types/` returns empty.
   - `grep -rln 'QFile\|QJsonDocument' src/types/` returns empty.
4. Update FINDINGS: cross out B2-derived entries with closing commit hash. Note any new
   `types/`/`models/`/`services/` boundary violations discovered.
5. Open Plan 4.

## Files affected

- `src/models/` — **new directory**, receives `logicalcalendar.{h,cpp}`.
- `src/services/` — **new directory**, receives `calendarmetadatamanager.{h,cpp}`,
  `crashjournal.{h,cpp}`, `incidencelockregistry.{h,cpp}`, `logicalcalendarjson.{h,cpp}`,
  `backendconfigurationjson.{h,cpp}`.
- `src/calendar/iincidenceregistry.h` — **new** (moved from types/).
- `src/types/domain.h` — **new** (moved from shape/).
- `src/types/*` — removed: `logicalcalendar`, `calendarmetadatamanager`, `crashjournal`,
  `incidencelock_registry`, `iincidenceregistry`, `iincidencesource`.
- `src/shape/shape.h` — re-exports `DomainId`/`EncodingId` from types/.
- Root `CMakeLists.txt` — adds models/ and services/ to source globs.

## Acceptance criteria

- No `<KCalendarCore/*>` include in any `src/types/*.h`.
- No `#include "shape.h"` in any `src/types/*.h` or `src/models/*.h`.
- Every file in `src/types/` is under 250 LOC and contains only value types or
  light helpers (rule of thumb; deviations get a comment justifying them).
- Every file in `src/services/` `#include`s at least one of `<QFile>`, `<QJsonDocument>`,
  or `<QMutex>` (sanity check: services touch infrastructure).
- All tests pass; PlanStan ctest baseline holds.

## Risks

- **Massive include churn.** ~50–100 .cpp files need their `#include` lines updated.
  Use a scripted rewrite (sed pass driven by a manifest of moves) rather than
  hand-editing; record the manifest in the commit message.
- **`BackendConfiguration` is on the boundary** — value semantics but heavy JSON. The
  split (type in types/, JSON in services/) is the right call but the type will gain a
  forward-declared `class` instead of full type for the JSON functions. Confirm callers
  that want both still link cleanly.
- **`LogicalCalendar` is a recall trigger** — multiple memory entries reference it
  (e.g. `config-widget-no-bridge.md`). Confirm the move doesn't break the bridge plans.

## Estimated effort

3–4 sessions. The mechanical move is fast; the include rewrite is the bulk of the
keystroke count; the protective test discipline (write/run/move/run for each batch)
is the time governor.
