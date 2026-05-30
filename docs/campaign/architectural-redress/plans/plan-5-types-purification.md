# Plan 5 — `types/` purification via a light `TypeSupport` target Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to
> implement Phase 1 task-by-task. Steps use checkbox (`- [ ]`) syntax.
>
> **Campaign discipline (non-optional):** read `../INVARIANTS.md`, `../../AUDIT.md` (B2),
> `../STATUS.md`, `../FINDINGS.md` first. The audit wins over a plan. New smells → `FINDINGS.md`;
> `STATUS.md` updated in the same commit that changes plan state.

**Goal:** Make the `kalburator-types` CMake target a genuine "minimal type vocabulary" by moving its
behavioral surfaces (config JSON codec, crash-journal I/O, the incidence lock registry, and the
`LogicalCalendar` JSON codec) into a **new light sibling target `kalburator-typesupport`** that
depends only on `Kalburator::Types` + Qt — NOT on the heavy `Kalburator::Sync` engine. Downstream
light consumers (libkalcal, PlanStan tests, PlanEngine) relink to `Kalburator::TypeSupport` and keep
their behavior without dragging in DAV/KIO/Network.

**Architecture (and why this shape).** The first draft of this plan distributed the offenders to
domain dirs (`calendar/`, `journal/`, `sync/`). That was **wrong** and was reset (see "History"
below): those domains live behind the heavy `Kalburator::Sync` target, but several deliberately-light
downstream foundation libs link `Kalburator::Types` **only** and depend on these exact surfaces —
moving the behavior behind Sync breaks their builds, and the symbols cannot be shimmed back at link
time. The correct shape is the one AUDIT B2 actually recommended: *"split `types/` into a pure
vocabulary sub-target and a separate helpers target for codecs/I/O/lock machinery."* The helpers
target must be **light** (Types + Qt, no Sync) so the light consumers can link it. This is a
**coordinated cross-repo migration**: libkalburator first (Phase 1), then each downstream relink
(Phase 2), then merge + push (Phase 3).

**Tech Stack:** C++20, Qt6 (Core/Gui), KF6::CalendarCore (transitive via Types), CMake static libs,
Qt Test (`ctest`), AUTOMOC. Build dir `build/`. In-tree ctest baseline: **133 passing**.

---

## Locked decision recorded by this plan

> **2026-05-30 — Plan 5 target shape = a NEW light `kalburator-typesupport` target (Types + Qt, no
> Sync), NOT distribute-to-domains.** The distribute-to-domains shape (first draft, reset at
> `6010ee2`) broke deliberately-light Types-only downstream consumers (libkalcal `KalCal::Core`/
> `Models`, PlanStan tests, PlanEngine) that reach into `types/` for behavior; relocating that
> behavior behind the heavy `Kalburator::Sync` target removes the symbols from them and cannot be
> shimmed at link time. A light helpers target reachable by Types-only consumers is what AUDIT B2's
> fix direction actually prescribed. (AUDIT B2; user decisions 2026-05-30; supersedes the earlier
> "distribute to domain homes" entry, which is withdrawn.)

Copy this into `../STATUS.md`'s "Locked decisions" ledger in Phase 1 Task 1, and mark the earlier
"distribute to domain homes" entry (if it was committed) as **withdrawn** — but note: the reset means
that earlier entry never landed on `main`, so it should simply not be reintroduced.

## History (why the reset)

The first execution attempt distributed offenders to domain dirs and landed three commits
(T1 crashjournal→journal/, T2 calendarmetadatamanager→calendar/, T3 incidencelock_registry→calendar/)
that were **in-tree green (133) but broke the PlanStan/libkalcal downstream build**, because the
verification "no Types-only consumer exists" checked PlanStan/WildPalms `src/` but missed the
`libkalcal` sibling and PlanEngine. The branch was reset to `main` (`6010ee2`); the commits survive in
the reflog. The corrected blast-radius map (below) drove the TypeSupport shape.

## Verified downstream blast radius (2026-05-30)

Which downstream targets use each offender symbol, and how they link libkalburator:

| Offender | Downstream consumers | Their libkalburator linkage |
|----------|----------------------|------------------------------|
| `CrashJournal` | PlanStan/PSW1 `tst_crashjournal` | via `KalCal::Core` (Types-only) |
| `IncidenceLockRegistry` | libkalcal `view-infrastructure`/`calendar-views`; PlanStan `libs/editor`, `src/views`, `src/controllers`, tests; PlanEngine `libs/task-views`, `src/`; (PSW1 mirrors PlanStan) | Types-only (via `KalCal::Core`) |
| `BackendConfiguration` (incl. `::fromJson`) | libkalcal `kalcal-models`; PlanStan `src/sync/topology`+`wizard`; WildPalms `profile`/`accounts`/`runtime` | libkalcal Types-only; PlanStan/WildPalms link **Sync** |
| `logicalCalendar*Json` | libkalcal `models/collectionsettings.cpp`; PlanStan `dialogs`+tests; WildPalms `runtime/syncconfigstore_wp.cpp` | mixed Types-only / Sync |
| `CalendarMetadataManager` | **none outside libkalburator** | — |

Notes:
- **PSW1 == PlanStan** (same Codeberg remote, same root commit; a second working checkout). Migrate
  PlanStan; PSW1 follows by pull. Do not treat as a separate target.
- **editor-framework, OrgGrove** link `Kalburator::Types` but do **not** use any offender → unaffected.
- **WildPalms** links `Kalburator::Sync`, so once Sync links TypeSupport it gets the symbols
  transitively — it only needs the new `logicalcalendarjson.h` include where it calls those functions.
- `calendarmetadatamanager` has no downstream consumer, so it goes to `calendar/` (Sync), not
  TypeSupport — keeping TypeSupport free of pure file-I/O nothing-light-needs.

## File structure (after Phase 1)

```
src/types/        pure vocabulary only (value types, enums, pure interfaces; LogicalCalendar struct;
                  incidencelock.h value struct). NO JSON, NO file I/O, NO QObject machinery.
src/typesupport/  NEW — kalburator-typesupport (deps: Kalburator::Types + Qt Core/Gui only):
                    backendconfiguration.{h,cpp}
                    crashjournal.{h,cpp}
                    incidencelock_registry.{h,cpp}
                    logicalcalendarjson.{h,cpp}   (codec extracted from logicalcalendar.h)
src/calendar/   + calendarmetadatamanager.{h,cpp}   (only in-tree Sync consumes it)
```

Verified facts the plan relies on:
- All four TypeSupport file-groups use **only Qt Core/Gui/Json** (no KCalendarCore/Network/Sql/DAV in
  their includes), so `kalburator-typesupport` needs only `PUBLIC Kalburator::Types` (which already
  brings Qt6::Core/Gui + KF6::CalendarCore). It stays light.
- All movers are `namespace Kalburator::Sync` and stay so (namespace is independent of directory).
- Only `incidencelock_registry.h` is a `Q_OBJECT` (AUTOMOC is global, so it is moc'd once in the
  TypeSupport target).
- No `kalburator-types` file references any mover (verified empty grep) — no upward include created.
- In-tree, bare `#include "X.h"` keeps resolving for the moved headers because the Sync target will
  link `kalburator-typesupport` PUBLIC and TypeSupport exposes `src/typesupport` as a PUBLIC include
  dir. Only `logicalcalendarjson.h` (new) needs an explicit include added at its 2 in-tree callers
  (`src/sync/syncmappinggenerator.cpp`, `tests/calendar/tst_logicalcalendar_domain.cpp`).
- `logicalcalendar.h` JSON section = lines 449–651 (8 functions: `backendRoleToString`,
  `backendRoleFromString`, `calendarBindingToJson`, `calendarBindingFromJson`, `logicalCalendarToJson`,
  `logicalCalendarFromJson`, `logicalCalendarsToJson`, `logicalCalendarsFromJson`).

## TDD posture

Relocations change no behavior; the gate is **compiler + existing suite** (in-tree, and the downstream
builds in Phase 2). Each Phase-1 task ends green at the 133-test baseline. `tst_logicalcalendar_domain`
is the round-trip characterization test guarding the codec extraction (P1.T4). Known pre-existing
flaky: `tst_engine_cancellation` (SEGFAULT in `cancelDuringApply`, threading race, unrelated) — if
ONLY it fails, re-run; 133/133 on re-run is acceptable (already logged in FINDINGS).

---

# Phase 1 — libkalburator (the core change)

Phase 1 lands entirely in libkalburator and is **in-tree green at every task** (the Sync target links
TypeSupport, so it has all symbols). It is kept UNMERGED until Phase 2 relinks the downstream repos
against this branch. CMake source-list anchors (verified): `KALBURATOR_TYPE_*` glob at
`CMakeLists.txt:47–48`; `kalburator-types` target `:57–78`; `KALBURATOR_CALENDAR_HEADERS` opens `:96`,
`KALBURATOR_CALENDAR_SOURCES` opens `:138`; `kalburator` (Sync) `target_link_libraries` `:689–701`
(links `kalburator-types` at `:691`).

## Task 1: Create `kalburator-typesupport` + move `backendconfiguration` into it

Establishes the new target and the Sync→TypeSupport linkage, using `backendconfiguration` as its first
member (the type + its JSON codec; namespace `Kalburator::Sync`).

**Files:**
- Create dir + move: `src/types/backendconfiguration.{h,cpp}` → `src/typesupport/backendconfiguration.{h,cpp}`
- Modify: `CMakeLists.txt` (new target block + Sync link)

- [ ] **Step 1: Move the files into the new dir**

```bash
mkdir -p src/typesupport
git mv src/types/backendconfiguration.h src/typesupport/backendconfiguration.h
git mv src/types/backendconfiguration.cpp src/typesupport/backendconfiguration.cpp
```

- [ ] **Step 2: Define the `kalburator-typesupport` target**

In `CMakeLists.txt`, immediately AFTER the `kalburator-types` target block (after its
`set_target_properties(... POSITION_INDEPENDENT_CODE ON)` line, ~`:78`), insert:

```cmake
# --- Kalburator::TypeSupport ------------------------------------------------
# Light behavioural companion to Kalburator::Types: JSON codecs, crash-journal
# I/O, and the incidence lock registry that used to sit in the "minimal type
# vocabulary" (AUDIT B2). Depends ONLY on Kalburator::Types + Qt — NOT on the
# heavy Kalburator::Sync engine — so deliberately-light downstream consumers
# (libkalcal, PlanStan tests, PlanEngine) can link it without DAV/KIO/Network.
add_library(kalburator-typesupport STATIC
    src/typesupport/backendconfiguration.h
    src/typesupport/backendconfiguration.cpp
)
add_library(Kalburator::TypeSupport ALIAS kalburator-typesupport)

target_include_directories(kalburator-typesupport
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/typesupport>
)

target_link_libraries(kalburator-typesupport
    PUBLIC
        kalburator-types
)

set_target_properties(kalburator-typesupport PROPERTIES POSITION_INDEPENDENT_CODE ON)
```

- [ ] **Step 3: Make the Sync target link TypeSupport**

In the `kalburator` target's `target_link_libraries` (PUBLIC block at `:689`), add
`kalburator-typesupport` directly above the existing `kalburator-types` line:

```cmake
target_link_libraries(kalburator
    PUBLIC
        kalburator-typesupport
        kalburator-types
        Qt6::Sql
        ...
```
(Keep `kalburator-types` too; harmless and explicit. TypeSupport brings it transitively, and the new
`src/typesupport` PUBLIC include dir now propagates to the Sync target so bare includes of moved
headers resolve.)

- [ ] **Step 4: Record the locked decision + open plan state in STATUS.md**

In `docs/campaign/architectural-redress/STATUS.md`: append the "2026-05-30 — Plan 5 target shape = a
NEW light `kalburator-typesupport` target …" entry (verbatim from this plan's "Locked decision"
section) to the Locked-decisions ledger; set the Plan 5 sequence-table row to
`in progress (feature/redress-5-types-purification)`; update "Next action" to point at this plan and
note the cross-repo phasing.

- [ ] **Step 5: Configure, build, run the full suite**

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```
Expected: configure picks up the new target (the types glob auto-drops backendconfiguration; it now
compiles in TypeSupport), build succeeds (all ~43 in-tree `backendconfiguration.h` consumers resolve
via the propagated `src/typesupport` include dir and link the TypeSupport-provided symbol through
Sync), **133 passed**. Refresh clangd: `ln -sf build/compile_commands.json compile_commands.json`.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor(types): add light Kalburator::TypeSupport; move backendconfiguration into it (Plan 5 P1.T1, AUDIT B2)

New kalburator-typesupport STATIC lib (deps: Kalburator::Types + Qt only, no
Sync) is the home for the behavioural surfaces that wrongly sat in the 'minimal
type vocabulary' (AUDIT B2). backendconfiguration.{h,cpp} (config value type +
JSON codec, ns Kalburator::Sync) is its first member. The Sync target links
TypeSupport PUBLIC, so in-tree consumers resolve unchanged. Downstream light
consumers relink to Kalburator::TypeSupport in Phase 2. No behaviour change.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

## Task 2: Move `crashjournal` → `kalburator-typesupport`

**Files:** move `src/types/crashjournal.{h,cpp}` → `src/typesupport/crashjournal.{h,cpp}`; modify
`CMakeLists.txt` (TypeSupport source list).

- [ ] **Step 1:** `git mv src/types/crashjournal.h src/typesupport/crashjournal.h` and the `.cpp`.
- [ ] **Step 2:** Add both to the `add_library(kalburator-typesupport STATIC …)` source list, after
  the backendconfiguration entries:
```cmake
    src/typesupport/crashjournal.h
    src/typesupport/crashjournal.cpp
```
- [ ] **Step 3:** Build + full ctest:
```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build -j && ctest --test-dir build --output-on-failure
```
Expected: 133 passed (consumer `journal/calendarjournal.h` resolves `crashjournal.h` via the
propagated TypeSupport include dir).
- [ ] **Step 4: Commit**
```bash
git add -A && git commit -m "refactor(types): move crashjournal -> typesupport/ (Plan 5 P1.T2, AUDIT B2)

JSON-lines crash I/O moves out of the type vocabulary into the light
TypeSupport target. No behaviour change.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

## Task 3: Move `incidencelock_registry` → `kalburator-typesupport` (value struct stays in types/)

**Files:** move `src/types/incidencelock_registry.{h,cpp}` → `src/typesupport/`; KEEP
`src/types/incidencelock.h`; modify `CMakeLists.txt`.

- [ ] **Step 1:** `git mv src/types/incidencelock_registry.h src/typesupport/incidencelock_registry.h`
  and the `.cpp`. Confirm `src/types/incidencelock.h` is untouched.
- [ ] **Step 2:** Add both to the TypeSupport source list:
```cmake
    src/typesupport/incidencelock_registry.h
    src/typesupport/incidencelock_registry.cpp
```
  (`incidencelock_registry.h`'s bare `#include "incidencelock.h"` resolves downward
  typesupport→types via the Types PUBLIC include dir. It is a `Q_OBJECT`; AUTOMOC processes it in the
  TypeSupport target.)
- [ ] **Step 3:** Build + full ctest (reconfigure; the registry is now moc'd in its new target):
```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build -j && ctest --test-dir build --output-on-failure
```
Expected: 133 passed.
- [ ] **Step 4: Commit**
```bash
git add -A && git commit -m "refactor(types): move incidencelock_registry -> typesupport/ (Plan 5 P1.T3, AUDIT B2)

The QObject lock registry moves to the light TypeSupport target; the
IncidenceLockInfo value struct (incidencelock.h) stays in types/. It is used
downstream by libkalcal/PlanStan/PlanEngine (NOT dead code), which relink to
TypeSupport in Phase 2. No behaviour change.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

## Task 4: Extract `LogicalCalendar` JSON codec → `typesupport/logicalcalendarjson.{h,cpp}`

The `LogicalCalendar` struct stays in `types/logicalcalendar.h`; only its 8 JSON free functions
(lines 449–651) move. This is the one task that edits in-tree consumer includes (2 callers).

**Files:** create `src/typesupport/logicalcalendarjson.{h,cpp}`; modify `src/types/logicalcalendar.h`
(delete JSON section); modify `CMakeLists.txt`; modify `src/sync/syncmappinggenerator.cpp` and
`tests/calendar/tst_logicalcalendar_domain.cpp` (add include).

- [ ] **Step 1:** Confirm the characterization test passes now:
  `ctest --test-dir build -R logicalcalendar_domain --output-on-failure` → PASS.
- [ ] **Step 2:** Create `src/typesupport/logicalcalendarjson.h` declaring the 8 functions (move out
  of `inline`-in-header into a header/impl split):
```cpp
#pragma once

// LogicalCalendar JSON codec. Extracted from src/types/logicalcalendar.h in Plan 5
// (AUDIT B2: the type vocabulary must not carry JSON). The LogicalCalendar value
// type stays in types/; only its serialization lives here, in the light TypeSupport
// target. Valid downward dependency: typesupport/ -> types/.

#include "logicalcalendar.h"   // BackendRole, CalendarBackendBinding, LogicalCalendar

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace Kalburator::Sync {

QString backendRoleToString(BackendRole role);
BackendRole backendRoleFromString(const QString &str);
QJsonObject calendarBindingToJson(const CalendarBackendBinding &binding);
CalendarBackendBinding calendarBindingFromJson(const QJsonObject &obj);
QJsonObject logicalCalendarToJson(const LogicalCalendar &cal);
LogicalCalendar logicalCalendarFromJson(const QJsonObject &obj);
QJsonArray logicalCalendarsToJson(const QList<LogicalCalendar> &calendars);
QList<LogicalCalendar> logicalCalendarsFromJson(const QJsonArray &arr);

} // namespace Kalburator::Sync
```
- [ ] **Step 3:** Create `src/typesupport/logicalcalendarjson.cpp` with the 8 function bodies pasted
  **verbatim** from `logicalcalendar.h:449–651`, `inline` removed, byte-for-byte logic preserved
  (the `.kalb` back-compat comments inside must survive):
```cpp
#include "logicalcalendarjson.h"

namespace Kalburator::Sync {
// <<< paste the 8 functions here, verbatim, inline removed >>>
} // namespace Kalburator::Sync
```
- [ ] **Step 4:** Delete lines 449–651 (the `// JSON Serialization` banner through the last codec
  function) from `src/types/logicalcalendar.h`, leaving the `using LogicalCollection = LogicalCalendar;`
  alias and the namespace close. Verify: `grep -n 'QJson\|toJson\|fromJson' src/types/logicalcalendar.h`
  → empty.
- [ ] **Step 5:** Add to the TypeSupport source list:
```cmake
    src/typesupport/logicalcalendarjson.h
    src/typesupport/logicalcalendarjson.cpp
```
- [ ] **Step 6:** Add `#include "logicalcalendarjson.h"` to `src/sync/syncmappinggenerator.cpp` and
  `tests/calendar/tst_logicalcalendar_domain.cpp` (near their existing `logicalcalendar.h` include).
  Verify no other in-tree caller:
  `grep -rln 'logicalCalendar.*Json\|calendarBindingToJson\|backendRoleToString' src/ tests/ | grep -v typesupport/logicalcalendarjson`
  → only those two files.
- [ ] **Step 7:** Reconfigure, run the characterization test, then the full suite:
```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build -j
ctest --test-dir build -R logicalcalendar_domain --output-on-failure
ctest --test-dir build --output-on-failure
```
Expected: round-trip test PASS (bodies moved verbatim), then 133 passed.
- [ ] **Step 8: Commit**
```bash
git add -A && git commit -m "refactor(types): extract LogicalCalendar JSON codec -> typesupport/ (Plan 5 P1.T4, AUDIT B2)

The LogicalCalendar struct stays in types/; its 8 JSON codec functions move to
the light TypeSupport target (typesupport/logicalcalendarjson.{h,cpp}). Bodies
moved verbatim (.kalb on-disk back-compat preserved); tst_logicalcalendar_domain
round-trip green before and after. Two in-tree callers gain the include.
Downstream LC-JSON callers gain it in Phase 2. No behaviour change.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

## Task 5: Move `calendarmetadatamanager` → `src/calendar/`

No downstream consumer; only in-tree `calendar/localbackend.cpp` uses it. It is calendar-metadata file
I/O — a domain concern, not something a light consumer needs — so it goes to `calendar/` (Sync), not
TypeSupport.

**Files:** move `src/types/calendarmetadatamanager.{h,cpp}` → `src/calendar/`; modify `CMakeLists.txt`
(`KALBURATOR_CALENDAR_HEADERS`/`_SOURCES`).

- [ ] **Step 1:** Confirm sole consumer: `grep -rln 'calendarmetadatamanager.h\|CalendarMetadataManager' src/ tests/ | grep -v 'src/types/calendarmetadatamanager'` → only `src/calendar/localbackend.cpp`.
- [ ] **Step 2:** `git mv src/types/calendarmetadatamanager.h src/calendar/calendarmetadatamanager.h` and the `.cpp`.
- [ ] **Step 3:** Add to `KALBURATOR_CALENDAR_HEADERS` (with a Plan 5/AUDIT B2 comment) and
  `KALBURATOR_CALENDAR_SOURCES`, in alphabetical position among the `calendar*` entries.
- [ ] **Step 4:** Build + full ctest → 133 passed.
- [ ] **Step 5: Commit**
```bash
git add -A && git commit -m "refactor(types): move calendarmetadatamanager -> calendar/ (Plan 5 P1.T5, AUDIT B2)

Atomic QSaveFile Vdir-metadata I/O has no downstream consumer and is a calendar
domain concern; it moves to calendar/ (Sync), not TypeSupport. Sole consumer is
calendar/localbackend.cpp. No behaviour change.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

## Task 6: Purity gate + Types banner + close Phase 1

**Files:** `CMakeLists.txt` (Types banner), `STATUS.md`, `FINDINGS.md`.

- [ ] **Step 1:** Prove `types/` is pure:
```bash
grep -rln 'Q_OBJECT' src/types/                                   # expect: none
grep -rln 'QJsonDocument\|toJson\|fromJson' src/types/            # expect: only synctypes.h
grep -rln 'QSaveFile\|QFile\b\|QDir\b\|QTextStream' src/types/    # expect: none
```
  First and third empty; JSON grep returns only `synctypes.h` (audit-sanctioned in-memory helper). If
  anything else appears, an offender was missed — stop.
- [ ] **Step 2:** Update the `Kalburator::Types` CMake banner (`:7–10`) to state it carries no JSON /
  I/O / QObject, and add a one-line note that `Kalburator::TypeSupport` is the light companion for
  those.
- [ ] **Step 3:** In `FINDINGS.md`, record (in the prescribed `YYYY-MM-DD — file:line — inv N — phrase`
  format) that `IncidenceLockRegistry` is **used** by libkalcal/PlanStan/PlanEngine (correcting the
  earlier-reset false "dead code" note — do NOT reintroduce that note), and resolve the B2 line noting
  the TypeSupport split. Mark Phase 1 done in `STATUS.md` with an outcome section; set "Next action"
  to Phase 2 (downstream relinks).
- [ ] **Step 4:** Final build + full ctest → 133 passed.
- [ ] **Step 5: Commit**
```bash
git add -A && git commit -m "docs(campaign): close Plan 5 Phase 1 — types/ pure, TypeSupport in place (AUDIT B2)

types/ passes the purity grep; Kalburator::Types banner corrected; STATUS marks
Phase 1 done and points to the Phase 2 downstream relinks; FINDINGS records that
IncidenceLockRegistry is live (used by libkalcal/PlanStan/PlanEngine) and the B2
split. No code change beyond the comment.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

# Phase 2 — downstream relinks (detailed per-repo when reached, invariant P1)

Each is a separate git repo, built against the **local** libkalburator feature branch (PlanStan's
in-tree FetchContent override resolves to the local checkout). Detailed tasks are written when Phase 1
has landed and the repo is inspected; the per-repo shape is:

- **P2.A — libkalcal** (`~/dev/libkalcal`, branch `master`): in `core/CMakeLists.txt` add
  `Kalburator::TypeSupport` to `KalCal::Core`'s PUBLIC link block (beside `Kalburator::Types`, ~`:40`);
  same for `models/CMakeLists.txt` `kalcal-models` (~`:64`). Add `#include "logicalcalendarjson.h"` to
  `models/src/collectionsettings.cpp`. Configure/build/ctest green; commit; push.
- **P2.B — PlanStan** (`~/dev/PlanStan`, current branch): relink the Types-only targets/tests that use
  offenders (`tst_crashjournal`, `tst_incidencelock`, `tst_logicalcalendar`, the `libs/editor`,
  `src/views`, `src/controllers` targets) to `Kalburator::TypeSupport` (via `KalCal::Core` if those
  link it, else directly). Add `#include "logicalcalendarjson.h"` to the ~5 LC-JSON callers
  (`src/dialogs/newcollectiondialog.cpp`, the 3 `tests/controllers/*`, `tests/core/tst_logicalcalendar.cpp`).
  Build + full ctest green (INVARIANTS §10); commit; push. PSW1 follows via pull.
- **P2.C — PlanEngine** (`~/dev/PlanEngine`, branch `master`): relink the targets using
  `IncidenceLockRegistry` (`libs/task-views`, `src/` app context/shell-view services) to
  `Kalburator::TypeSupport`. Build green; commit; push.
- **P2.D — WildPalms** (`~/dev/WildPalms`, branch `feature/three-tier-sync`): links Sync (gets
  TypeSupport transitively), so only add `#include "logicalcalendarjson.h"` to
  `src/runtime/syncconfigstore_wp.cpp`. Build green; commit; push.

Acceptance gate for Phase 2: each downstream repo's reachable suite green against the local
libkalburator branch (INVARIANTS §10).

# Phase 3 — integrate

- Merge `feature/redress-5-types-purification` into libkalburator `main` (`--no-ff`); rebuild + ctest
  133; push `origin/main`.
- Push each downstream repo's relink commit to its remote.
- Update `STATUS.md`: Plan 5 done; point "Next action" at Plan 6 (`shape/` decoupling — move
  `ConflictPolicy` down, AUDIT B6).

---

## Self-review (run before execution)

- **Spec coverage:** all 5 AUDIT B2 offenders relocated (backendconfiguration P1.T1, crashjournal
  P1.T2, incidencelock_registry P1.T3, logicalcalendar-JSON P1.T4, calendarmetadatamanager P1.T5);
  P1.T6 proves purity; Phase 2 keeps every downstream consumer green; Phase 3 integrates.
- **Layer direction (§1):** TypeSupport depends on Types (downward); no `types/` file references a
  mover (verified). TypeSupport is light (no Sync) — does not invert any dependency.
- **Light-consumer contract:** TypeSupport links only Types + Qt, so libkalcal/PlanEngine keep their
  light footprint (no DAV/KIO/Network pulled in) — the reason the distribute-to-domains shape failed.
- **§10 downstream:** Phase 2 explicitly relinks + rebuilds every affected repo before Phase 3 merge;
  the branch stays unmerged until they are green.
- **No placeholders (Phase 1):** every step has exact commands/paths/code; the 8 codec functions in
  P1.T4 match the enumerated lines 449–651.
- **Type consistency:** target name `kalburator-typesupport` / alias `Kalburator::TypeSupport` used
  consistently; codec signatures in P1.T4 match across header/impl/callers.
