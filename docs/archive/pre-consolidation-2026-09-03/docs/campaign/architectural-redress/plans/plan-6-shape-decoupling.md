# Plan 6 — `shape/` decoupling: narrow `RecordMerger::merge()` to `AutoResolveStrategy` (AUDIT B6) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`)
> syntax for tracking.
>
> **Campaign discipline (non-optional):** read `../INVARIANTS.md`, `../AUDIT.md` (B6),
> `../STATUS.md`, `../FINDINGS.md` first. The audit wins over a plan — but this plan *corrects*
> the audit's fix direction from verified code evidence (see "Why not the audit-literal fix");
> the AUDIT annotation lands in Task 3 per the audit's own header rule. New smells →
> `FINDINGS.md`; `STATUS.md` updated in the same commit that changes plan state.
>
> **Build discipline:** `make -C build -j 8` and `ctest --test-dir build -j 8` — NEVER
> `-j$(nproc)` or `--parallel` (GCC 16.1.1 on this machine throws flaky ICE segfaults under
> all-core load; if an ICE appears, retry the same command once before suspecting the code).

**Goal:** Sever the `shape/ → conflict/` upward dependency (AUDIT B6) by narrowing
`RecordMerger::merge()`'s policy parameter to the one thing mergers actually read — the
`AutoResolveStrategy` enum — and moving that enum (only) down into `shape/`.

**Architecture (and why this shape).** AUDIT B6's fix direction said "move `ConflictPolicy`
into `types/` (or `shape/`)". Code verification (2026-06-06) showed that is the wrong cut:
`conflict/conflictpolicy.h` is a 240-line qsynccore-era surface (behavior methods taking
`ConflictRecord`, a JSON codec, two handler classes, string helpers, a compat namespace alias)
— moving it down would violate the Plan 5 `types/` purity gate and drag `conflictrecord.h`
into the abstract layer. Meanwhile **all nine** `RecordMerger` implementations read exactly
**one field** (`policy.autoResolve`), and **both** production callsites pass the constant
`ConflictPolicy::deferAll()`. So the seam is the *signature*, not the struct: `merge()` takes
`Kalburator::Shape::AutoResolveStrategy`; the enum moves to a new tiny
`src/shape/autoresolvestrategy.h`; `conflict/` includes it downward and re-exports it as
`Kalburator::Conflict::AutoResolveStrategy` (alias) so every existing reference — including
WildPalms' `palmconflicthandler.cpp` — keeps compiling. `ConflictPolicy`, the handlers, and
the JSON codec stay in `conflict/` untouched.

**Tech Stack:** C++20, Qt6, KF6 (CalendarCore/Contacts transitive), CMake static libs, Qt Test.
Build dir `build/`. In-tree ctest baseline: **136 passing** (verified 2026-06-06).

---

## Locked decision recorded by this plan (copy into `../STATUS.md` ledger in Task 0)

> **2026-06-06 — Plan 6 fix shape = narrow `RecordMerger::merge()` to
> `Shape::AutoResolveStrategy`, NOT move `ConflictPolicy` down.** Verified evidence: all 9
> merger implementations read only `policy.autoResolve`; both production callsites
> (`syncengine.cpp:1665/:2529`) pass the constant `deferAll()`; zero downstream
> `RecordMerger` implementors or `merge()` callers exist (PlanStan/WildPalms grep, 2026-06-06);
> the audit-literal move would break the Plan 5 purity gate (the policy struct carries
> JSON + behavior + `ConflictRecord` coupling). The enum lands in `src/shape/`
> (NOT `types/` — `types/synctypes.h` already holds the rival `ConflictResolution` enum;
> a second near-synonym conflict enum there would deepen the U3-family collision Plan 10
> must untangle). `Kalburator::Conflict::AutoResolveStrategy` survives as an alias for
> source compatibility. (AUDIT B6, corrected from code; user decisions 2026-06-06.)

## Verified facts this plan relies on (2026-06-06, at `main` = `b254950`)

- The **only** `shape/ → conflict/` edges are `src/shape/recordmerger.h:4`
  (`#include "conflictpolicy.h"`) and `src/shape/canonjsonmerger.cpp:6` (same). No other
  file in `src/shape/` references `conflict/`.
- **Nine** `RecordMerger` implementations: `CanonJsonMerger` (`src/shape/canonjsonmerger.h:15`),
  `RecordMergerICal` (`src/calendar/icalrecordmerger.h:10`), `RecordMergerVCard`
  (`src/contacts/vcardmerger.h:9`), `RecordMergerVTodo` (`src/todo/icalvtodomerger.h:9`),
  `TextMerger` (`src/note/textmerger.h:9`), `OutlineMerger` (`src/outline/outlinemerger.h:8`),
  `RecordMergerBlob` (`src/blob/blobdomaindefinition.cpp:36`, file-local), and two test-fake
  `TrivialMerger`s (`tests/plugin/fakes/fake_plugin.cpp:18`,
  `tests/plugin/scenarios/fake_docstogo_plugin.cpp:22`).
- Every implementation reads **only** `policy.autoResolve`; `OutlineMerger` ignores the
  parameter entirely (`Q_UNUSED(policy)`, `outlinemerger.cpp:13`).
- Production callsites: `src/engine/syncengine.cpp:1665` and `:2529`, both passing
  `Kalburator::Conflict::ConflictPolicy::deferAll()`, whose `autoResolve` is
  `AutoResolveStrategy::None` (`conflictpolicy.cpp:163-170`). Behavior-preserving substitution
  is therefore `AutoResolveStrategy::None`.
- Factory → enum mapping for test rewrites: `autoSourceWins()` sets `SourceAlwaysWins`
  (`conflictpolicy.cpp:143`), `autoTargetWins()` sets `TargetAlwaysWins` (`:153`),
  `deferAll()`/`interactive()`/`ConflictPolicy{}` (default) set/leave `None`.
- **Zero downstream `RecordMerger` references** (grep over PlanStan `src/`+`libs/`, WildPalms
  `src/`+conduits, 2026-06-06). The only downstream `AutoResolveStrategy` user is
  WildPalms `src/palm/conflict/palmconflicthandler.cpp`, via `Kalburator::Conflict::` —
  preserved by the alias. PlanStan's `ConflictPolicy` users (`src/sync/topology/*`,
  `src/views/collectionview.cpp`) use the struct, which does not move.
- The string helpers `autoResolveStrategyToString/FromString` are declared/used **only** in
  `src/conflict/conflictpolicy.{h,cpp}` — they stay put and keep working via the alias.
- `conflictrecord.h` (included by `conflictpolicy.h` *before* the enum definition) does not
  reference `AutoResolveStrategy` — no cycle risk.
- Merger `.cpp`s already have `using namespace Kalburator::Shape;` **and**
  `using namespace Kalburator::Conflict;` (vcardmerger/icalvtodomerger/textmerger/
  icalrecordmerger) — after the move, bare `AutoResolveStrategy` resolves to the same entity
  through both directives (alias = same type → no ambiguity).
- Engine callsites see `RecordMerger` complete (they call `m_unifiedMerger->merge`), so
  `autoresolvestrategy.h` arrives transitively via `recordmerger.h` — no engine include edits.
- Downstream build wiring: PlanStan presets (`build-dev`) with
  `PLANSTAN_LIBKALBURATOR_SOURCE_DIR`, WildPalms legacy (`build/`) with
  `WILDPALMS_LIBKALBURATOR_SOURCE_DIR` — both can point at this checkout. **Check each
  build dir's existing `CMakeCache.txt` first and preserve its configuration** (the Plan 5
  Phase 2 relink work likely left the override set already).

## Why not the audit-literal fix (recorded so it is not re-litigated)

Moving `ConflictPolicy` down (even split: struct → `shape/`, codec → `typesupport/`):
forces `ConflictRecord` down with it (the struct's `shouldAutoResolve`/`getAutoDecision`/
`shouldPrompt` take `const ConflictRecord&`) or splits the behavior methods away from the
struct; moves engine-orbit *configuration* (prompt timeouts, batch-review flags, safety
limits) into the abstract transformation layer that demonstrably consumes none of it; and
touches a 15-file downstream surface for no consumer benefit. The narrowing achieves B6's
actual goal — `shape/` referencing nothing above it — with a strictly smaller, behavior-
preserving diff. (AUDIT B6 gets an annotation in Task 3 per the audit header rule.)

## File structure (after)

```
src/shape/autoresolvestrategy.h   NEW — the enum, namespace Kalburator::Shape, header-only
src/shape/recordmerger.h          includes autoresolvestrategy.h (same-dir); merge() takes the enum
src/shape/canonjsonmerger.{h,cpp} conflictpolicy.h include GONE
src/conflict/conflictpolicy.h     enum definition GONE → #include "../shape/autoresolvestrategy.h"
                                  + alias `using AutoResolveStrategy = Kalburator::Shape::AutoResolveStrategy;`
                                  (struct/handlers/JSON/string helpers otherwise untouched)
src/{calendar,contacts,todo,note,outline}/<*merger*>.{h,cpp}, src/blob/blobdomaindefinition.cpp
                                  signatures take Shape::AutoResolveStrategy; conflict includes dropped
src/engine/syncengine.cpp         2 callsites pass Shape::AutoResolveStrategy::None
tests/...                         6 files updated mechanically (mapping table above)
```

Layer result: `shape/` includes only `shape/` + `types/` + Qt. `conflict/ → shape/` is the
new (downward, legal) edge.

## TDD posture

This is a behavior-preserving narrowing: the gate is **compiler + the 136-test suite at every
task**, plus the existing merger characterization tests (`tst_ical_record_merger`,
`tst_vcard_differ`, `tst_canonjson_diff_merge`, `tst_outline_differ`) re-pinning strategy
behavior **through the new signature** after Task 2 — they call the production callsite
surface (`merge()`) directly, satisfying invariant 6. Task 3 adds the structural layering
gate (grep) that would have failed before Task 2 and passes after — the "new seam" pin.
Known flaky: `tst_engine_cancellation` (pre-existing threading race, FINDINGS 2026-05-29) —
if ONLY it fails, re-run; green on re-run is acceptable.

---

### Task 0: Branch, ledger, downstream baselines

**Files:**
- Modify: `docs/campaign/architectural-redress/STATUS.md` (Locked decisions ledger + plan table)

- [ ] **Step 1: Confirm clean start and create the branch**

```bash
cd ~/dev/libkalburator && git status --short && git checkout main && git pull
git checkout -b feature/redress-6-shape-decoupling
```
Expected: clean tree, new branch. Work in THIS checkout (not a worktree) so the downstream
`*_LIBKALBURATOR_SOURCE_DIR` overrides point at the branch automatically.

- [ ] **Step 2: Record the locked decision in STATUS.md**

Append the blockquote from "Locked decision recorded by this plan" (above) to the
`## Locked decisions` section of `../STATUS.md`, and change the Plan 6 row in the plan table
to `**IN PROGRESS — feature/redress-6-shape-decoupling**`.

- [ ] **Step 3: Capture the downstream baselines (invariant 10)**

```bash
# PlanStan — check how its build is wired before touching it:
grep LIBKALBURATOR ~/dev/PlanStan/build-dev/CMakeCache.txt
# If PLANSTAN_LIBKALBURATOR_SOURCE_DIR is unset or points elsewhere:
cmake --preset dev -S ~/dev/PlanStan -DPLANSTAN_LIBKALBURATOR_SOURCE_DIR=$HOME/dev/libkalburator
cmake --build ~/dev/PlanStan/build-dev -j 8
ctest --test-dir ~/dev/PlanStan/build-dev -j 8 2>&1 | tail -5 | tee /tmp/planstan_baseline.txt

# WildPalms — same pattern, legacy build dir:
grep LIBKALBURATOR ~/dev/WildPalms/build/CMakeCache.txt
cmake -B ~/dev/WildPalms/build -S ~/dev/WildPalms -DWILDPALMS_LIBKALBURATOR_SOURCE_DIR=$HOME/dev/libkalburator
make -C ~/dev/WildPalms/build -j 8
ctest --test-dir ~/dev/WildPalms/build -j 8 2>&1 | tail -5 | tee /tmp/wildpalms_baseline.txt
```
Record the pass/fail counts. These runs are against the **unchanged** branch point — any
failure here is pre-existing (PlanStan was last seen 97/118 with stale-binary artifacts;
relink `tst_integration_*` targets first if GUI tests fail — see auto-memory
clobber-sync-v065). The Task 4 gate is "no NEW failures vs. these baselines", not absolute
green. **If a baseline build fails to configure/compile, STOP and surface it to the user
before proceeding — do not paper over it.**

- [ ] **Step 4: Commit**

```bash
cd ~/dev/libkalburator
git add docs/campaign/architectural-redress/STATUS.md
git commit -m "docs(campaign): open Plan 6 — shape/ decoupling via merge() narrowing (locked decision)"
```

### Task 1: Extract `AutoResolveStrategy` into `src/shape/` (non-breaking)

**Files:**
- Create: `src/shape/autoresolvestrategy.h`
- Modify: `src/conflict/conflictpolicy.h:25-37` (enum block → include + alias)
- Modify: `CMakeLists.txt:341` area (shape headers list)

- [ ] **Step 1: Create the new header**

`src/shape/autoresolvestrategy.h`:

```cpp
#pragma once

/**
 * @file autoresolvestrategy.h
 * @brief Strategy vocabulary for automatic conflict resolution during merge.
 *
 * Extracted from conflict/conflictpolicy.h (architectural-redress Plan 6):
 * shape/'s RecordMerger consumes exactly this enum, so it lives in the
 * abstract transformation layer. conflict/ includes it downward and
 * re-exports it as Kalburator::Conflict::AutoResolveStrategy for source
 * compatibility (WildPalms palmconflicthandler.cpp et al.).
 */

namespace Kalburator::Shape {

/**
 * @brief Automatic resolution strategy
 */
enum class AutoResolveStrategy
{
    None,               ///< Never auto-resolve, always defer or ask
    SourceAlwaysWins,   ///< Source overwrites target automatically
    TargetAlwaysWins,   ///< Target overwrites source automatically
    NewerWins,          ///< Most recently modified version wins
    OlderWins,          ///< Preserve the older version (conservative)
    LargerWins,         ///< Keep the version with more content
    DuplicateAll        ///< Always create duplicates (never lose data)
};

} // namespace Kalburator::Shape
```

- [ ] **Step 2: Replace the enum in `conflictpolicy.h` with include + alias**

In `src/conflict/conflictpolicy.h`, replace the block (currently lines 25-37):

```cpp
/**
 * @brief Automatic resolution strategy
 */
enum class AutoResolveStrategy
{
    None,               ///< Never auto-resolve, always defer or ask
    SourceAlwaysWins,   ///< Source overwrites target automatically
    TargetAlwaysWins,   ///< Target overwrites source automatically
    NewerWins,          ///< Most recently modified version wins
    OlderWins,          ///< Preserve the older version (conservative)
    LargerWins,         ///< Keep the version with more content
    DuplicateAll        ///< Always create duplicates (never lose data)
};
```

with:

```cpp
/// AutoResolveStrategy moved to the shape layer (architectural-redress
/// Plan 6) — RecordMerger consumes it there. Alias preserved so existing
/// Kalburator::Conflict::AutoResolveStrategy references keep compiling.
using AutoResolveStrategy = Kalburator::Shape::AutoResolveStrategy;
```

and add the include after `#include "conflictrecord.h"` (line 15):

```cpp
#include "../shape/autoresolvestrategy.h"
```

- [ ] **Step 3: Register the header in CMake**

In `CMakeLists.txt`, in the Sync-target shape header list, change:

```cmake
    src/shape/recorddiffer.h
    src/shape/recordmerger.h
```

to:

```cmake
    src/shape/autoresolvestrategy.h
    src/shape/recorddiffer.h
    src/shape/recordmerger.h
```

- [ ] **Step 4: Build + full suite**

```bash
make -C build -j 8 && ctest --test-dir build -j 8 --output-on-failure 2>&1 | tail -3
```
Expected: `100% tests passed, 0 tests failed out of 136`. (Everything still compiles —
the alias preserves every existing reference; no signature changed yet.)

- [ ] **Step 5: Commit**

```bash
git add src/shape/autoresolvestrategy.h src/conflict/conflictpolicy.h CMakeLists.txt
git commit -m "refactor(shape): extract AutoResolveStrategy to shape/ with Conflict:: alias (Plan 6 T1, AUDIT B6)"
```

### Task 2: Narrow `RecordMerger::merge()` to `AutoResolveStrategy` (atomic)

A virtual-signature change — all implementors, callsites, and test callers move in ONE
commit so the tree never builds red. Every edit below is mechanical; `policy.autoResolve`
→ `strategy`, factory calls → enum values per the mapping table in "Verified facts".

**Files:**
- Modify: `src/shape/recordmerger.h` (the interface)
- Modify: `src/shape/canonjsonmerger.{h,cpp}`, `src/calendar/icalrecordmerger.{h,cpp}`,
  `src/contacts/vcardmerger.{h,cpp}`, `src/todo/icalvtodomerger.{h,cpp}`,
  `src/note/textmerger.{h,cpp}`, `src/outline/outlinemerger.{h,cpp}`,
  `src/blob/blobdomaindefinition.cpp`
- Modify: `src/engine/syncengine.cpp:1665,:2529`
- Modify: `tests/plugin/fakes/fake_plugin.cpp`, `tests/plugin/scenarios/fake_docstogo_plugin.cpp`,
  `tests/calendar/differs/tst_ical_record_merger.cpp`, `tests/contacts/tst_vcard_differ.cpp`,
  `tests/outline/tst_outline_differ.cpp`, `tests/shape/tst_canonjson_diff_merge.cpp`

- [ ] **Step 1: Narrow the interface**

`src/shape/recordmerger.h` — replace `#include "conflictpolicy.h"` with
`#include "autoresolvestrategy.h"` and change the pure-virtual:

```cpp
    /// 3-way merge. For each property in the union of source/target/
    /// baseline catalogues, decide TakeSource/TakeTarget/TakeBaseline
    /// per the supplied auto-resolve strategy. Result is a fully-realised
    /// CanonicalRecord ready for push-back to the target.
    virtual CanonicalRecord merge(
        const CanonicalRecord& source,
        const CanonicalRecord& target,
        const CanonicalRecord& baseline,
        AutoResolveStrategy strategy) const = 0;
```

- [ ] **Step 2: Update the seven production implementors**

Headers — replace the parameter `const Kalburator::Conflict::ConflictPolicy& policy` with
`Kalburator::Shape::AutoResolveStrategy strategy` (in `canonjsonmerger.h:23` the type is
spelled bare `AutoResolveStrategy` — it is already inside `namespace Kalburator::Shape`):

- `src/shape/canonjsonmerger.h:23`
- `src/calendar/icalrecordmerger.h:16`
- `src/contacts/vcardmerger.h:15`
- `src/todo/icalvtodomerger.h:15`
- `src/note/textmerger.h:15`
- `src/outline/outlinemerger.h:14`

Implementations:

- `src/shape/canonjsonmerger.cpp` — delete `#include "conflictpolicy.h"` (line 6) and
  `using Kalburator::Conflict::AutoResolveStrategy;` (line 8); signature param →
  `AutoResolveStrategy strategy`; line 30: `policy.autoResolve == AutoResolveStrategy::TargetAlwaysWins`
  → `strategy == AutoResolveStrategy::TargetAlwaysWins`.
- `src/calendar/icalrecordmerger.cpp:38` — param → `AutoResolveStrategy strategy`; line 92:
  `switch (policy.autoResolve)` → `switch (strategy)`.
- `src/contacts/vcardmerger.cpp` — delete `#include "conflictpolicy.h"` (line 3); param →
  `AutoResolveStrategy strategy`; line 65: `srcWins(src, tgt, policy.autoResolve)` →
  `srcWins(src, tgt, strategy)`. (The file-local `srcWins` already takes the enum.)
- `src/todo/icalvtodomerger.cpp` — delete `#include "conflictpolicy.h"` (line 3); param →
  `AutoResolveStrategy strategy`; `srcWinsOnPolicy(src, tgt, policy.autoResolve)` →
  `srcWinsOnPolicy(src, tgt, strategy)`.
- `src/note/textmerger.cpp` — delete `#include "conflictpolicy.h"` (line 3); param
  (line 62) → `AutoResolveStrategy strategy`; line 77: `srcWins(policy.autoResolve, …)` →
  `srcWins(strategy, …)`.
- `src/outline/outlinemerger.cpp:11` — param → `Kalburator::Shape::AutoResolveStrategy strategy`;
  line 13: `Q_UNUSED(policy)` → `Q_UNUSED(strategy)`.
- `src/blob/blobdomaindefinition.cpp` — delete `#include "conflictpolicy.h"` (line 6); param
  (line 42) → `Kalburator::Shape::AutoResolveStrategy strategy`; lines 53-54:
  `using AR = Kalburator::Conflict::AutoResolveStrategy;` →
  `using AR = Kalburator::Shape::AutoResolveStrategy;` and
  `policy.autoResolve == AR::TargetAlwaysWins` → `strategy == AR::TargetAlwaysWins`.

Note: the merger `.cpp`s keep their `using namespace Kalburator::Conflict;` lines only if
something else in the file still needs them — after these edits nothing does in
vcardmerger/icalvtodomerger/textmerger/icalrecordmerger; delete the now-dead
`using namespace Kalburator::Conflict;` from each (leave `using namespace Kalburator::Shape;`).

- [ ] **Step 3: Update the two engine callsites**

`src/engine/syncengine.cpp` — both occurrences (lines 1665 and 2529):

```cpp
                    Kalburator::Conflict::ConflictPolicy::deferAll());
```
→
```cpp
                    Kalburator::Shape::AutoResolveStrategy::None);
```
(Behavior-identical: `deferAll().autoResolve == None`.)

- [ ] **Step 4: Update the two test fakes**

`tests/plugin/fakes/fake_plugin.cpp` and `tests/plugin/scenarios/fake_docstogo_plugin.cpp`:
delete `#include "conflictpolicy.h"` (line 5 in both); in the `TrivialMerger` override
replace `const Kalburator::Conflict::ConflictPolicy&` with
`Shape::AutoResolveStrategy` (both files already qualify with `Shape::`).

- [ ] **Step 5: Update the four test callers**

- `tests/calendar/differs/tst_ical_record_merger.cpp` — replace
  `#include "conflictpolicy.h"` (line 9) with `#include "autoresolvestrategy.h"`; replace
  `using namespace Kalburator::Conflict;` (line 12) with
  `using Kalburator::Shape::AutoResolveStrategy;`; at the six `merge(` callsites replace
  `ConflictPolicy::autoSourceWins()` → `AutoResolveStrategy::SourceAlwaysWins` (lines
  65/81/97/113/145) and `ConflictPolicy::autoTargetWins()` →
  `AutoResolveStrategy::TargetAlwaysWins` (line 129).
- `tests/contacts/tst_vcard_differ.cpp` — replace `#include "conflictpolicy.h"` (line 6)
  with `#include "autoresolvestrategy.h"`; delete
  `using Kalburator::Conflict::ConflictPolicy;` (line 18) and change line 19 to
  `using Kalburator::Shape::AutoResolveStrategy;`; in the five blocks (lines 141-144,
  159-162, 177-180, 195-198, 214-217) delete the two policy-construction lines
  (`ConflictPolicy policy;` / `policy.autoResolve = AutoResolveStrategy::SourceAlwaysWins;`)
  and change the callsite to
  `merger.merge(src, tgt, base, AutoResolveStrategy::SourceAlwaysWins)`.
- `tests/outline/tst_outline_differ.cpp` — replace `#include "conflictpolicy.h"` (line 7)
  with `#include "autoresolvestrategy.h"`; delete
  `using Kalburator::Conflict::ConflictPolicy;` (line 12); replace
  `const ConflictPolicy policy{};` (line 59) and its use at the `merge(` callsite with
  passing `AutoResolveStrategy::None` directly (the file already has
  `using namespace Kalburator::Shape;`).
- `tests/shape/tst_canonjson_diff_merge.cpp` — at the four callsites (lines 77/88/99/110)
  replace `Kalburator::Conflict::ConflictPolicy{}` with
  `Kalburator::Shape::AutoResolveStrategy::None`; drop the `conflictpolicy.h` include if
  the file has one.

- [ ] **Step 6: Build + full suite**

```bash
make -C build -j 8 && ctest --test-dir build -j 8 --output-on-failure 2>&1 | tail -3
```
Expected: `100% tests passed, 0 tests failed out of 136`. If a merger test fails, the
factory→enum mapping was misapplied — check against the table in "Verified facts"; do NOT
adjust merger logic to make it pass.

- [ ] **Step 7: Commit**

```bash
git add -A src tests
git commit -m "refactor(shape): RecordMerger::merge() takes AutoResolveStrategy — shape/ no longer includes conflict/ (Plan 6 T2, AUDIT B6)"
```

### Task 3: Layering gate + campaign bookkeeping

**Files:**
- Modify: `docs/campaign/architectural-redress/AUDIT.md` (B6 resolution annotation)
- Modify: `docs/campaign/architectural-redress/FINDINGS.md` (three new entries)
- Modify: `docs/campaign/architectural-redress/STATUS.md` (plan state)

- [ ] **Step 1: Run the layering gate (the new-seam pin)**

```bash
grep -rn "conflict" src/shape/*.h src/shape/*.cpp | grep -v "^Binary" | grep -iv "conflict-policy-aware\|conflict;" | grep "include\|Conflict::"
```
Expected: **empty output** (before Task 2 this printed the `recordmerger.h:4` and
`canonjsonmerger.cpp:6` includes). Also confirm the `types/` purity gate is unaffected:

```bash
git diff main --stat -- src/types/
```
Expected: **empty** — Plan 6 does not touch `types/`. (Known pre-existing gate hole, logged
in FINDINGS 2026-06-06 and explicitly NOT this plan's scope: `synctypes.h` carries inline
`syncMapping{To,From}Json` codecs the Plan 5 grep missed by case — `toJson` ≠ `ToJson`.)

- [ ] **Step 2: Annotate AUDIT B6**

In `../AUDIT.md`, under the B6 section heading, append:

```markdown
- **RESOLVED 2026-06-XX (Plan 6) — by narrowing, not moving.** Code verification showed all
  9 merger impls read only `policy.autoResolve` and production passes constant `deferAll()`;
  `merge()` now takes `Shape::AutoResolveStrategy` (enum extracted to
  `shape/autoresolvestrategy.h`; `Conflict::` alias preserved). `ConflictPolicy` stays in
  `conflict/` — the audit's "move ConflictPolicy" direction would have violated the Plan 5
  purity gate and dragged `ConflictRecord` down. shape/→conflict/ edge count: 0.
```

- [ ] **Step 3: Add the FINDINGS entries**

Append to `../FINDINGS.md` under a new `### From Plan 6 (shape/ decoupling, 2026-06-XX)`:

```markdown
- 2026-06-XX — `src/types/synctypes.h:32` vs `src/shape/autoresolvestrategy.h` — inv 5 —
  DUAL conflict vocabulary: `Sync::ConflictResolution` (what `SyncMapping.conflictPolicy`
  holds and the engine switches on) vs `Shape::AutoResolveStrategy` (what mergers consume);
  the `SyncMapping.conflictPolicy` FIELD is named after the OTHER type
  (`Conflict::ConflictPolicy`, which never flows into the engine). Unification +
  field rename = Plan 10 (vocabulary) input; the AUDIT missed this entirely.
- 2026-06-XX — `src/engine/syncengine.cpp:1665/:2529` (pre-Plan-6 shape) — inv 4 — the rich
  `ConflictPolicy` never flowed into `merge()`: both production callsites passed constant
  `deferAll()`; the engine resolves LastWriteWins itself (`lastwritewins.h`, v0.64) and
  consults mergers only on the CustomMerge/unified-merge paths. Plan 6's narrowing makes the
  real dataflow explicit; the prompt/batch/safety knobs in `ConflictPolicy` are consumed only
  by the `ConflictHandler` UI path (downstream WildPalms/PlanStan).
- 2026-06-XX — `src/outline/outlinemerger.cpp:13` — inv (capability) — `OutlineMerger`
  ignores its strategy parameter (`Q_UNUSED`): structural outline merge is a documented
  follow-on (in-code comment); strategy-aware merge needs a design before Plan 11 closes
  test gaps over it.
```

- [ ] **Step 4: Update STATUS.md**

Plan table row 6 → `**DONE — feature/redress-6-shape-decoupling (gates pending Task 4)**`;
"Next action" section → Plan 7 (Remote/Local decomposition, replanned against post-v0.63
tree). Same commit as the FINDINGS/AUDIT edits (invariant 7).

- [ ] **Step 5: Commit**

```bash
git add docs/campaign/architectural-redress/
git commit -m "docs(campaign): Plan 6 close-out — B6 resolved by narrowing; dual-vocabulary + vestigial-policy FINDINGS (inv 7/9)"
```

### Task 4: Downstream gates, merge, push

- [ ] **Step 1: Re-run the downstream suites (invariant 10)**

```bash
cmake --build ~/dev/PlanStan/build-dev -j 8 && ctest --test-dir ~/dev/PlanStan/build-dev -j 8 2>&1 | tail -5
make -C ~/dev/WildPalms/build -j 8 && ctest --test-dir ~/dev/WildPalms/build -j 8 2>&1 | tail -5
```
Gate: **no NEW failures vs. the Task 0 baselines** (`/tmp/planstan_baseline.txt`,
`/tmp/wildpalms_baseline.txt`). Both repos compile against this checkout via their
`*_LIBKALBURATOR_SOURCE_DIR` override — a compile error here means the alias compat story
has a hole; STOP and fix before merging (expected: zero edits needed downstream).

- [ ] **Step 2: WildPalms five invariants spot-check (INVARIANTS §10)**

The five contracts (X-property round-trip, per-category virtual sub-collections, lossy-sync
warning channel, `SyncMapping.lossPolicy`, shape-side diff) are exercised by WildPalms' own
suite — a green Step 1 run covers them; note the run in the merge commit message.

- [ ] **Step 3: Merge to main and push**

```bash
cd ~/dev/libkalburator
git checkout main && git pull
git merge --no-ff feature/redress-6-shape-decoupling \
  -m "Merge Plan 6 (feature/redress-6-shape-decoupling): shape/ decoupling — RecordMerger::merge() narrowed to AutoResolveStrategy (AUDIT B6)"
make -C build -j 8 && ctest --test-dir build -j 8 2>&1 | tail -3
git push
```
Expected: 136/136 on the merged tree, then push.

---

## Self-review notes (done at write time)

- Every file named above was verified to exist with the quoted line numbers at
  `main` = `b254950` (2026-06-06). If the tree has moved, re-grep before editing —
  do not trust the line numbers blind (the same discipline this plan applied to the audit).
- No placeholder steps; the factory→enum mapping table covers every test rewrite.
- Type consistency: the parameter is `AutoResolveStrategy strategy` (by value — it is an
  enum) everywhere; the interface, seven implementors, two fakes, and all callsites agree.
- Out of scope (invariant 8): renaming `AutoResolveStrategy`/unifying it with
  `ConflictResolution` (Plan 10); strategy-aware outline merge; deleting the
  `Kalburator::Sync::QSyncCore` compat alias in `conflictpolicy.h`; the
  `synccommon.h` misplacement review (logged mentally for Plan 9's dir consolidation —
  note: it holds `RecordId`/common types under `Kalburator::Conflict`, smells like a
  Plan 9/10 item, NOT this plan's).
