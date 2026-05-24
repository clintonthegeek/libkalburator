# Plan 4 — Calendar Convergence (retire `src/transcoding/`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **Two-stage review (spec then quality) after every task is mandatory — a single subagent must NOT run multiple tasks unsupervised** (this campaign already paid for that once: FINDINGS 2026-05-24 "Parts A,B&C fixups").

**Goal:** Retire the second record-conversion mechanism (`src/transcoding/`) by re-homing its one real behavior — org-mode RRULE simplification — as a `canon → org-ical` shape-graph edge with a `Simplified` `LossProfile`, then remove the `TranscodingPlan` seam from the live write path. After this plan libkalburator has exactly **one** transformation mechanism (the shape graph), closing the campaign.

**Architecture:** Org-mode is modeled as a *less-capable peer encoding* `{calendar, org-ical}` (invariant 1/7): the `canon → org-ical` edge parses the canon's verbatim RFC5545 recurrence (the *one* edge allowed to parse it — invariant 3, like `canon → Microsoft`), simplifies complex rules to a basic daily/weekly/monthly/yearly pattern, and stashes the original verbatim in `X-ORIGINAL-RRULE`; the reverse `org-ical → canon` edge restores it. The lossy-sync warning channel (a WildPalms invariant, invariant 10) is re-sourced from the write pipeline's composed `LossProfile.summary()` instead of the transcoding plan's warnings. The backend-type-keyed `TranscodingRegistry`/`Router`/`Plan`/`RRuleTranscoder`/`PropertyTranscoder` are then deleted; the unrelated diff engines `incidencediff`/`syncdiff` (which merely lived in that directory) move to a new `src/diff/`.

**Tech Stack:** C++17 / Qt6, KCalendarCore (iCal / Recurrence parsing), CMake, QtTest. Build/test per `docs/campaign/STATUS.md`.

**Source of truth:** design (`docs/2026-05-23-canon-upgrade-and-convergence-design.md` §7 "keying tension dissolves", §10 "affected code", §11 acceptance); INVARIANTS (esp. 1, 3, 4, 5, 6, 8, 10); STATUS Plan-4 row + locked decisions; FINDINGS O8 (calendar canon already landed by Plan 3), **O10 (incidencediff/syncdiff are load-bearing, not transcoding)**.

**Human decisions locked (2026-05-24, recorded before writing — invariant 7):**
1. **`incidencediff`/`syncdiff` move to a new `src/diff/`** and `src/transcoding/` is deleted (not kept as a shim). FINDINGS O10.
2. **The `TranscodingPlan` parameter is dropped entirely** from `pushItems`/`startSync`/`ApplyContext` (no deprecated overload). Downstream PlanStan/WildPalms backend subclasses adapt **after this branch merges** — same migration model as Plan 2's injecting ctor (FINDINGS O7). A new FINDINGS watch item tracks the downstream port.

---

## Landed APIs this plan builds against (verified in tree, invariant P1)

- **Calendar canon already exists (Plan 3).** `src/calendar/calendarstockshapes.cpp` registers peer `{calendar, ical}` + edges: `canon→canon` identity, `ical→canon` (`ICalToCanonStage`, lossless), `canon→ical` (`canonToIcalLoss()`, `CanonToICalStage`). The stages live in `src/calendar/icalcanonstages.{h,cpp}`; the loss builder `canonToIcalLoss()` is a file-local function in `calendarstockshapes.cpp`. Calendar canonical head is `{calendar, canon}` (`calendardomaindefinition.cpp`).
- **Canon recurrence is a `StringList` of verbatim RFC5545 lines** (`recurrence` PropertyId), captured by `ICalToCanonStage::extractRecurrenceLines()` (raw text scan, `icalcanonstages.cpp`) and re-injected verbatim by `CanonToICalStage` before `END:VEVENT`. No canon code parses recurrence (invariant 3).
- **Shape edge API:** `struct TransformationEdge { Shape from; Shape to; LossProfile loss; std::shared_ptr<TransformationStage> stage; };` (`src/shape/transformationedge.h:31`). `class TransformationStage { virtual QByteArray transform(const QByteArray&) const = 0; };` (`:15`). `struct LossProfile { QHash<PropertyId,LossKind> affected; bool isLossless() const; LossProfile compose(const LossProfile&) const; QString summary() const; ... };` (`src/shape/lossprofile.h:23`); `enum class LossKind { Dropped, Simplified, Reversible, Degraded };` (`:12`). `class ShapeContribution { DomainId targetDomain() const; QList<std::pair<Shape,PropertyCatalogue>> peerShapes() const; QList<TransformationEdge> edges() const; };`.
- **Pipeline composed loss:** `class Pipeline { LossProfile composedLoss() const; QByteArray apply(const QByteArray&) const; bool isIdentity() const; ... };` (`src/shape/pipeline.h:16`). `TransformationRegistry::compile(Shape from, Shape to) const -> std::optional<Pipeline>` and `inspect(Shape,Shape) const -> LossProfile`.
- **RRULE transcoder logic to re-home** (`src/transcoding/rruletranscoder.cpp`): `RRuleTranscoder::transcode(Incidence::Ptr&)` — if `isComplexRecurrence` (multiple RRULEs, or any of byDays/byMonthDays/byYearDays/byWeekNumbers/byMonths/bySetPos, or RDATE/EXDATE present), stash all rRules via `ICalFormat::toString(&ruleCopy)` joined by `;` into custom property `("X-ORIGINAL","RRULE")`, then `simplifyRecurrence()` (read `recurrenceType`/`frequency`/`duration`/`endDateTime`, `recurrence->clear()`, set `setDaily/Weekly/Monthly/Yearly(interval)` by primary type, restore duration/endDate). `RRuleReverseTranscoder::transcode` restores: read `customProperty("X-ORIGINAL","RRULE")`, `recurrence->clear()`, split on `;`, `ICalFormat::fromString(&rule, str)` + `addRRule`, `removeCustomProperty("X-ORIGINAL","RRULE")`.
- **Live write-path transcoding seam to remove:**
  - `ApplyContext` (`src/shape/recordwriter.h:43-51`) member `Kalburator::Sync::TranscodingPlan transcodingPlan;`.
  - Engine plan computation + injection: `src/engine/syncengine.cpp` — `m_router.plan(...)` at `:2451-2454` (and the sibling computation in the other write method near `:2434`), `applyBatch` lambda param `const TranscodingPlan &plan` and `ctx.transcodingPlan = plan` at `:2481`/`:2495`. Members `m_router`/registry wiring at `:57`; warning forward `onWorkerTranscodingWarning()` at `:1210-1219`; worker→engine forward at `:2595-2596`,`:2616-2617`.
  - `CalendarPluginWriter` (`src/calendar/calendarplugin_writer.{h,cpp}`): `prepareForApply` stores `m_plan` (`:53-61`), `setTranscodingPlan` (`:68-71`), plan passed to `CreateIncidenceItem`/`UpdateIncidenceItem` (`:202-203`,`:215-216`).
  - Backend `pushItems(const QString&, const QList<...>&, const TranscodingPlan&)` + `executeTranscodingPlan(plan, original)` calls in: `akonadibackend`, `decsyncbackend`, `localbackend`, `mockbackend`, `orgbackend` (gated), `remotecalendarbackend`, `subscriptionbackend`, `syncbackend` (base), and `contacts/akonadicontactsbackend`; item ctors `createincidenceitem.{h,cpp}`, `updateincidenceitem.{h,cpp}`.
  - `transcodingWarning(QString calendarId, QString uid, QStringList)` is a **public SyncEngine signal** (`src/engine/syncengine.h:227-229`) and a WildPalms invariant — it is **kept**, but re-sourced (Task 4).
- **`incidencediff`/`syncdiff` (KEEP, relocate):** `src/transcoding/incidencediff.{h,cpp}`, `src/transcoding/syncdiff.{h,cpp}`. Consumers (update includes): `src/engine/syncengine.{h,cpp}`, `src/engine/enginediff.h`, `src/engine/propertydiff.h`, `src/calendar/icalrecorddiffer.{h,cpp}`, `src/calendar/icalrecordmerger.cpp`, `src/calendar/updateincidenceitem.cpp`, `src/calendar/decsyncactivecontroller.{h,cpp}`, tests `tests/calendar/tst_syncdiff.cpp`, `tst_incidencediff.cpp`, `tst_calendar_subsequent_sync_uses_blob_view.cpp`. CMake lists them at `CMakeLists.txt:192,195,201,204`; the dir is on the include path at `CMakeLists.txt:590`.
- **Transcoding warning test:** `tests/calendar/tst_calendar_transcoding_warning.cpp` — uses mock backends `"mock"`/`"lossy-mock"` and a stub `ByDayStripTranscoder` registered into `TranscodingRegistry::instance()`, `cleanup()` calls `TranscodingRegistry::instance().clear()`. Pins: backend-type mismatch → `transcodingWarning` with uid + a message mentioning the lost field. **Rewritten in Task 4** to drive the warning through the shape graph.
- **Org-IO is OFF by default** (`CMakeLists.txt:32` `KALBURATOR_HAVE_ORG_IO=OFF`); `OrgBackend` is gated and not built. The `org-ical` peer + edges are therefore **registered and tested synthetically** in `CalendarStockShapes` (always compiled) — they do not require the org backend. Wiring `OrgBackend` to declare `{calendar, org-ical}` as its backend shape is **downstream/org-on work, out of this plan's default-profile scope** (invariant 8); this plan lands the edge + the warning re-sourcing that PlanStan's org sync will consume.

---

## Why each task stays green (read before starting)

- **Task 1** is a pure file move (`src/transcoding/` → `src/diff/` for the two diff engines) + include/CMake updates. No behavior change; full suite stays green.
- **Tasks 2–3** add the `org-ical` peer + edges — additive, like Plan 3's bridges. Nothing routes to `org-ical` in the default build (no org backend), so existing behavior is unchanged; new synthetic tests cover the edge.
- **Task 4 adds the new warning source before Task 5/6 remove the old one** — the lossy-sync warning channel never lapses (invariant 10). The rewritten `tst_calendar_transcoding_warning` proves the shape-graph path emits the warning.
- **Tasks 5–6** remove the `TranscodingPlan` seam. Because transcoding plans are empty no-ops in the default build (no orgmode backend) and the warning is already re-sourced (Task 4), removing them is behavior-preserving for the default suite. The calendar engine/integration suite is the green-gate.
- **Task 7–8** delete the now-unreferenced transcoding machinery + its tests. With all references removed in 5–6, deletion compiles clean.
- **Task 9** is the final clean-build + suite gate + STATUS/FINDINGS close-out (campaign complete).

---

## File structure

| File | Responsibility | Change |
|------|----------------|--------|
| `src/diff/incidencediff.{h,cpp}` | Incidence property-diff engine (moved from `src/transcoding/`) | **Move** |
| `src/diff/syncdiff.{h,cpp}` | Sync diff engine (moved from `src/transcoding/`) | **Move** |
| `src/calendar/orgicalcanonstages.{h,cpp}` | `CanonToOrgICalStage` (simplify + stash), `OrgICalToCanonStage` (restore) | **Create** |
| `src/calendar/calendarstockshapes.cpp` | Register `{calendar, org-ical}` peer + the two edges; `canonToOrgIcalLoss()` | Modify |
| `src/engine/syncengine.{h,cpp}` | Re-source `transcodingWarning` from composed `LossProfile`; delete plan computation, router/registry members, `ApplyContext.transcodingPlan` injection | Modify |
| `src/shape/recordwriter.h` | Remove `ApplyContext::transcodingPlan` | Modify |
| `src/calendar/calendarplugin_writer.{h,cpp}` | Drop `m_plan`/`setTranscodingPlan`/plan threading | Modify |
| `src/calendar/createincidenceitem.{h,cpp}`, `updateincidenceitem.{h,cpp}` | Drop `TranscodingPlan` ctor param/member | Modify |
| all calendar backends + `syncbackend` base + `contacts/akonadicontactsbackend` | Drop `TranscodingPlan` from `pushItems`/`startSync`; remove `executeTranscodingPlan` | Modify |
| `tests/calendar/tst_calendar_transcoding_warning.cpp` | Drive warning via shape graph; drop `TranscodingRegistry` usage | Rewrite |
| `tests/transcoding/tst_transcoding_router.cpp` | Router deleted | **Delete** |
| `src/transcoding/` (machinery: `transcodingregistry`, `transcodingrouter`, `transcodingplan`, `rruletranscoder`, `propertytranscoder`) | Retired into the shape graph | **Delete** |
| `CMakeLists.txt` | Move diff sources to `src/diff/`; drop transcoding-machinery sources; drop the `src/transcoding` include-path export | Modify |
| `docs/campaign/STATUS.md`, `FINDINGS.md` | Campaign close-out + downstream-port watch item | Modify (final task) |

---

## Conventions used in this plan

**KCalendarCore-heavy stage bodies are specified by "reuse the landed stage + port the named function," not re-typed line-by-line** — the same convention Plan 3 adopted and defended (its self-review §"Deliberate convention"): speculative KCalendarCore code rots (P1's spirit), and the existing `icalcanonstages.cpp` + `rruletranscoder.cpp` are the exact, tested reference. Each such step names the precise source to read, the function to port, and the executable round-trip/loss test that is the acceptance contract. Mechanical edits (CMake, signature changes, deletions, registrations, `LossProfile` construction) are given as exact code/commands.

---

# PART 0 — Relocate the diff engines (Task 1)

## Task 1: Move `incidencediff`/`syncdiff` to `src/diff/`

**Files:** Move `src/transcoding/incidencediff.{h,cpp}` and `src/transcoding/syncdiff.{h,cpp}` → `src/diff/`; modify every consumer's include; modify `CMakeLists.txt`.

- [ ] **Step 1: Move the four files with git.**

```bash
mkdir -p /home/clinton/dev/libkalburator/src/diff
git -C /home/clinton/dev/libkalburator mv src/transcoding/incidencediff.h src/diff/incidencediff.h
git -C /home/clinton/dev/libkalburator mv src/transcoding/incidencediff.cpp src/diff/incidencediff.cpp
git -C /home/clinton/dev/libkalburator mv src/transcoding/syncdiff.h src/diff/syncdiff.h
git -C /home/clinton/dev/libkalburator mv src/transcoding/syncdiff.cpp src/diff/syncdiff.cpp
```

- [ ] **Step 2: Update `CMakeLists.txt`.** Change the four paths in the source/header lists (currently `CMakeLists.txt:192,195` headers and `:201,204` sources) from `src/transcoding/incidencediff.*`/`src/transcoding/syncdiff.*` to `src/diff/incidencediff.*`/`src/diff/syncdiff.*`. Add `src/diff` to the target include path (mirror the existing `src/transcoding` entry at `CMakeLists.txt:590` — add a `$<BUILD_INTERFACE:.../src/diff>` line; leave the `src/transcoding` entry for now, it is removed in Task 8).

- [ ] **Step 3: Update includes in every consumer.** These files `#include "incidencediff.h"`/`"syncdiff.h"` (or `transcoding/…`) — grep and fix each so the include resolves via the new `src/diff` include dir (the include is by basename, so if the path is already `"incidencediff.h"` only the include *dir* matters and no edit is needed; if any file uses `"transcoding/incidencediff.h"` or a relative `../transcoding/...`, repoint it). Consumers: `src/engine/syncengine.h`, `src/engine/syncengine.cpp`, `src/engine/enginediff.h`, `src/engine/propertydiff.h`, `src/calendar/icalrecorddiffer.h`, `src/calendar/icalrecorddiffer.cpp`, `src/calendar/icalrecordmerger.cpp`, `src/calendar/updateincidenceitem.cpp`, `src/calendar/decsyncactivecontroller.h`, `src/calendar/decsyncactivecontroller.cpp`, `tests/calendar/tst_syncdiff.cpp`, `tests/calendar/tst_incidencediff.cpp`, `tests/calendar/tst_calendar_subsequent_sync_uses_blob_view.cpp`. Run: `grep -rn "transcoding/incidencediff\|transcoding/syncdiff\|incidencediff.h\|syncdiff.h" src/ tests/` and verify no include still points into `src/transcoding/`.

- [ ] **Step 4: Configure + full build + suite.**

```bash
cmake -S /home/clinton/dev/libkalburator -B /home/clinton/dev/libkalburator/build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build /home/clinton/dev/libkalburator/build
ctest --test-dir /home/clinton/dev/libkalburator/build
```
Expected: clean build; suite green at the Plan-3 baseline (111/112; the lone failure is the known `tst_providerlifecycle` async flake — re-run isolated to confirm). STOP if any new failure (a missed include).

- [ ] **Step 5: Commit.**

```bash
git -C /home/clinton/dev/libkalburator add -A
git -C /home/clinton/dev/libkalburator commit -m "diff: relocate incidencediff/syncdiff to src/diff (Plan 4 Task 1)

These are conflict/diff engines, not transcoding; they merely lived in
src/transcoding/. Moved ahead of retiring that directory (FINDINGS O10).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

# PART 1 — Re-home RRULE simplification as the `org-ical` edge (Tasks 2–3)

## Task 2: `org-ical` (de)serialization stages

**Files:** Create `src/calendar/orgicalcanonstages.{h,cpp}`; Test `tests/calendar/tst_orgical_canon_roundtrip.cpp`; Modify `CMakeLists.txt`, `tests/calendar/CMakeLists.txt`.

The `org-ical` encoding is iCal whose recurrence is reduced to a single basic pattern (org-mode's limit). The demote edge `canon → org-ical` is the **one edge allowed to parse recurrence** (invariant 3 — like `canon → Microsoft`); a parse failure is a localized `Simplified` loss, never canon corruption.

- [ ] **Step 1: Header.** Create `src/calendar/orgicalcanonstages.h`:

```cpp
#pragma once
#include "transformationedge.h"
namespace Kalburator::Calendar {
/// canon -> org-ical: emit iCal with recurrence SIMPLIFIED to a basic pattern
/// (org-mode cannot represent complex RRULEs). The original RRULE set is stashed
/// verbatim in X-ORIGINAL-RRULE so org-ical -> canon restores it (Reversible at
/// the property level; the edge LossProfile classes `recurrence` as Simplified).
class CanonToOrgICalStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& canonBytes) const override;
};
/// org-ical -> canon: restore X-ORIGINAL-RRULE into the recurrence, then promote
/// to canon exactly as ICalToCanonStage does. Lossless.
class OrgICalToCanonStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& orgIcalBytes) const override;
};
}  // namespace Kalburator::Calendar
```

- [ ] **Step 2: Write the failing synthetic round-trip + simplification test.** Create `tests/calendar/tst_orgical_canon_roundtrip.cpp`. Register it in `tests/calendar/CMakeLists.txt` mirroring `tst_calendar_canon_roundtrip` (read that registration). Slots:
  - `canonToOrgIcalSimplifiesComplexRecurrence`: build a canon JSON object whose `recurrence` StringList holds a **complex** rule (e.g. `["RRULE:FREQ=WEEKLY;BYDAY=MO,WE,FR"]`) plus core fields (uid/summary/start), run `CanonToOrgICalStage`; parse the output iCal with `KCalendarCore::ICalFormat`; assert the event's recurrence is a **basic weekly** rule (no `byDays`), AND the output contains `X-ORIGINAL-RRULE` carrying the original `FREQ=WEEKLY;BYDAY=MO,WE,FR` verbatim.
  - `orgIcalRoundTripRestoresComplexRecurrence`: feed that canon through `CanonToOrgICalStage` then `OrgICalToCanonStage`; parse the resulting canon; assert `recurrence` again contains the original `RRULE:FREQ=WEEKLY;BYDAY=MO,WE,FR` (restored byte-equivalent, modulo `RRULE:` prefix) — i.e. the simplification is reversible.
  - `canonToOrgIcalLeavesSimpleRecurrenceUnchanged`: a canon with `["RRULE:FREQ=DAILY;INTERVAL=2"]` (already basic) → org-ical output has the same daily rule and **no** `X-ORIGINAL-RRULE`.

- [ ] **Step 3: Run (FAIL — undefined stages), then implement `orgicalcanonstages.cpp`.** Implement by **reuse + port**:
  - `OrgICalToCanonStage::transform`: parse `orgIcalBytes` to an `Incidence::Ptr`; apply the **`RRuleReverseTranscoder::transcode` logic ported from `src/transcoding/rruletranscoder.cpp:138-174`** (restore `X-ORIGINAL`/`RRULE` → recurrence, remove the custom prop); re-serialize to iCal bytes; then run that through the existing `ICalToCanonStage` (call it / share its impl) to produce canon JSON. Net: lossless promote that un-simplifies.
  - `CanonToOrgICalStage::transform`: build the `Incidence`/iCal from canon **exactly as `CanonToICalStage` does** BUT do not rely on the verbatim recurrence re-injection for the simplified output — instead let the recurrence be parsed into the Event, then apply the **`RRuleTranscoder::transcode` logic ported from `rruletranscoder.cpp:9-136`** (`isComplexRecurrence` → stash all rRules into `X-ORIGINAL`/`RRULE` via `ICalFormat::toString`, then `simplifyRecurrence`), and serialize. Practical approach: produce canon→ical bytes via `CanonToICalStage`, parse to `Incidence`, run the ported `transcode`, re-serialize. Keep the X- property name consistent with the reverse stage (`X-ORIGINAL`/`RRULE`, surfaced in iCal as `X-ORIGINAL-RRULE`).
  - Read `src/calendar/icalcanonstages.cpp` first to reuse its event-building/parsing helpers (extract shared helpers if cleaner, but do not change `ICalToCanonStage`/`CanonToICalStage` behavior — the existing calendar tests pin them). Add the new `.cpp`/`.h` to `CMakeLists.txt` calendar sources.
  - Iterate to green on all three slots. The first two prove simplification + reversibility; the third proves no spurious stash.

- [ ] **Step 4: Build + run the calendar suite; commit.**

```bash
cmake --build /home/clinton/dev/libkalburator/build --target tst_orgical_canon_roundtrip
ctest --test-dir /home/clinton/dev/libkalburator/build -R "tst_orgical_canon_roundtrip|tst_calendar_canon_roundtrip"
git -C /home/clinton/dev/libkalburator add src/calendar/orgicalcanonstages.h src/calendar/orgicalcanonstages.cpp \
        tests/calendar/tst_orgical_canon_roundtrip.cpp tests/calendar/CMakeLists.txt CMakeLists.txt
git -C /home/clinton/dev/libkalburator commit -m "calendar: add org-ical (de)serialization stages re-homing RRULE simplification (Plan 4 Task 2)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

## Task 3: Register the `org-ical` peer + edges in `CalendarStockShapes`

**Files:** Modify `src/calendar/calendarstockshapes.cpp` (and `.h` if needed).

- [ ] **Step 1: Read the current file** (peer `{calendar, ical}`; the three canon/ical edges; the file-local `canonToIcalLoss()`).

- [ ] **Step 2: Add a `canonToOrgIcalLoss()` file-local function** classing recurrence as `Simplified` (invariant 4 — simplified, NOT dropped; the original is recoverable from `X-ORIGINAL-RRULE`, which is itself a Reversible carrier):

```cpp
namespace {
Kalburator::Shape::LossProfile canonToOrgIcalLoss()
{
    using Kalburator::Shape::PropertyId;
    using Kalburator::Shape::LossKind;
    Kalburator::Shape::LossProfile p;
    // org-mode cannot hold complex RRULEs; the canon->org-ical edge reduces them
    // to a basic pattern but keeps the original verbatim in X-ORIGINAL-RRULE.
    p.affected.insert(PropertyId{QStringLiteral("recurrence")}, LossKind::Simplified);
    return p;
}
}  // namespace
```

- [ ] **Step 3: Register the peer + two edges.** Add `#include "orgicalcanonstages.h"`. In `peerShapes()`, append `{ orgIcal, makeICalCatalogue() }` (org-ical shares the iCal catalogue — capability, not field-set, differs). In `edges()`, append (do NOT modify the existing ical/canon edges):

```cpp
const Shape orgIcal{ DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("org-ical")} };
// ... within the returned list, after the existing ical edges:
TransformationEdge{ orgIcal, canon, LossProfile{}, std::make_shared<OrgICalToCanonStage>() },   // promote (lossless, un-simplifies)
TransformationEdge{ canon, orgIcal, canonToOrgIcalLoss(), std::make_shared<CanonToOrgICalStage>() }, // demote (Simplified)
```

- [ ] **Step 4: Add edge/loss tests** to `tests/calendar/tst_orgical_canon_roundtrip.cpp` using a populated `ShapeRegistries` (build one with the calendar stock plugin registered — mirror the registries setup in `tst_calendar_canon_roundtrip.cpp`'s edge tests):
  - `canonRoutesToOrgIcalDirectly`: `registries.transformation.compile(canon, orgIcal).has_value()` is true.
  - `canonToOrgIcalLossChargesRecurrenceSimplified`: `registries.transformation.inspect(canon, orgIcal).affected.value(PropertyId{QStringLiteral("recurrence")}) == LossKind::Simplified`.

- [ ] **Step 5: Build + run the full calendar + engine suite (green-gate); commit.**

```bash
ctest --test-dir /home/clinton/dev/libkalburator/build -R "calendar|tst_engine_unified_routing|tst_carddav|tst_engine_universal_sink_dispatch|tst_engine_silent_success_guard|tst_mass_delete_guard|tst_cancellation_reason"
git -C /home/clinton/dev/libkalburator add src/calendar/calendarstockshapes.cpp src/calendar/calendarstockshapes.h \
        tests/calendar/tst_orgical_canon_roundtrip.cpp
git -C /home/clinton/dev/libkalburator commit -m "calendar: register org-ical peer + canon<->org-ical bridges (Plan 4 Task 3)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

# PART 2 — Re-source the lossy-sync warning (Task 4)

## Task 4: Emit `transcodingWarning` from the write pipeline's composed `LossProfile`

The WildPalms invariant (10) requires *a* lossy-sync warning channel. Today it comes from the backend executing a `TranscodingPlan`. Re-source it: when the engine demotes a canon record to the target backend shape for write, if the demotion pipeline's `composedLoss()` is **not lossless**, emit `transcodingWarning(collectionId, recordId, { composedLoss().summary() })`. **This lands before Task 5/6 remove the old source**, so the channel never lapses.

**Files:** Modify `src/engine/syncengine.cpp` (the demote-on-write site, ~`:2585-2611`, where canon records are demoted to the target shape before `applyBatch`). Rewrite `tests/calendar/tst_calendar_transcoding_warning.cpp`.

- [ ] **Step 1: Read the demote-on-write region** (`src/engine/syncengine.cpp` around `:2560-2620`) to find where each outbound record is demoted via the compiled `canon→tgtShape` pipeline, and where `collectionId`/`recordId` are in scope. Confirm the existing `emit transcodingWarning(...)` forward at `:2595-2596`/`:2616-2617` and `onWorkerTranscodingWarning` (`:1210-1219`) — the **signal stays**; only its *source* changes.

- [ ] **Step 2: Add the loss-derived warning at the demote site — warn on *materialized*, non-reversible loss only.** A static edge `LossProfile` is a *capability* statement (e.g. `canon→ical` lists `onlineMeeting=Dropped` whether or not a given record has an online meeting), so a blanket `!isLossless()` emit would over-warn on every demotion and is dishonest (it would "warn" about fields the record never had — and iCal-origin records round-trip `canon→ical` losslessly). The honest, shape-driven rule (no encoding special-casing): warn only for affected `PropertyId`s that are **present and non-empty in this record's canon JSON** AND whose `LossKind` is user-visible (`Dropped`/`Simplified`/`Degraded` — exclude pure `Reversible`, which round-trips clean). In the default build this fires for nothing (only `canon→ical` runs, and iCal-origin records have none of its dropped Google-only fields present); it fires for the `canon→org-ical` recurrence simplification (recurrence present + `Simplified`), which is exactly the org-mode case the campaign converges.

```cpp
// Re-sourced lossy-sync warning (was: TranscodingPlan warnings). Honest signal:
// only properties this record actually carries AND that the demotion edge loses
// in a user-visible way (Reversible is round-trip-clean → no warning).
const Kalburator::Shape::LossProfile demoteLoss = pipeline.composedLoss();
const QJsonObject canonObj = Kalburator::Shape::CanonEnvelope::parse(rec.data); // rec.data is canon JSON pre-apply
QStringList lostHere;
for (auto it = demoteLoss.affected.constBegin(); it != demoteLoss.affected.constEnd(); ++it) {
    if (it.value() == Kalburator::Shape::LossKind::Reversible) continue;
    const QString key = it.key().toString();
    const QJsonValue v = canonObj.value(key);
    const bool present = !v.isUndefined() && !v.isNull()
        && !(v.isString() && v.toString().isEmpty())
        && !(v.isArray() && v.toArray().isEmpty())
        && !(v.isObject() && v.toObject().isEmpty());
    if (present) lostHere << key;
}
if (!lostHere.isEmpty())
    emit /*worker*/ transcodingWarning(collectionId, uid, { demoteLoss.summary() }); // or lostHere.join(", ")
```
(Use the exact pipeline/record/uid variable names found in Step 1; the canon bytes are `rec.data` *before* `pipeline.apply`. Emit through the existing worker→engine forward wiring — `SyncEngineWorker::transcodingWarning` → `SyncEngine::onWorkerTranscodingWarning` → public `transcodingWarning`; the signal signature `(QString,QString,QStringList)` is unchanged. Add the `canonenvelope.h`/`<QJsonObject>` includes to the engine if needed.)

- [ ] **Step 3: Rewrite `tests/calendar/tst_calendar_transcoding_warning.cpp`** to drive the warning through the shape graph instead of `TranscodingRegistry` + a stub `PropertyTranscoder`:
  - Remove the `ByDayStripTranscoder` stub, the `TranscodingRegistry::instance().register…` in `init()`, and the `TranscodingRegistry::instance().clear()` in `cleanup()`.
  - Drive a sync where the target backend's shape is a **lossy peer** so the `canon→peer` demotion charges a loss. Two options — pick the one that fits the existing harness with least churn: (a) point the target mock backend at `{calendar, org-ical}` (its demotion charges `recurrence=Simplified`) and seed a complex-RRULE event; or (b) register a synthetic lossy peer + edge in a stack-local `ShapeRegistries` and route to it. Assert `SyncEngine::transcodingWarning` fires with the conflicting uid and a `summary()` string naming the simplified/lost property.
  - Keep the test's name/intent (lossy-sync warning is emitted) — only the mechanism changes.

- [ ] **Step 4: Build + run; commit.** The warning test plus the calendar/engine suite must be green.

```bash
ctest --test-dir /home/clinton/dev/libkalburator/build -R "tst_calendar_transcoding_warning|calendar|tst_engine_unified_routing"
git -C /home/clinton/dev/libkalburator add src/engine/syncengine.cpp tests/calendar/tst_calendar_transcoding_warning.cpp
git -C /home/clinton/dev/libkalburator commit -m "engine: re-source lossy-sync warning from composed LossProfile (Plan 4 Task 4)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

# PART 3 — Remove the `TranscodingPlan` seam (Tasks 5–6)

## Task 5: Delete plan computation + `ApplyContext` injection + router/registry from the engine

**Files:** Modify `src/engine/syncengine.{h,cpp}`, `src/shape/recordwriter.h`.

- [ ] **Step 1: Remove `ApplyContext::transcodingPlan`** (`src/shape/recordwriter.h:45`) and the `#include` of `transcodingplan.h` there. The struct keeps `collectionId` + `calendarCollection`.

- [ ] **Step 2: In `src/engine/syncengine.cpp`,** delete the `TranscodingPlan` computation (`m_router.plan(...)` at `:2451-2454` and the sibling computation near `:2434`), drop the `const TranscodingPlan &plan` parameter from the `applyBatch` lambda (`:2481`) and every call to it, and remove `ctx.transcodingPlan = plan;` (`:2495`). Remove the `m_router` member + its init (`:57`) and the `m_transcodingRouter`/registry members and `SyncEngineWorker` ctor params (`syncengine.h:51,82,85,90,149,330,759`). **Keep** `onWorkerTranscodingWarning` + the `transcodingWarning` signal (now fed by Task 4). Remove the `#include`s of `transcodingrouter.h`/`transcodingregistry.h`/`transcodingplan.h` from the engine.

- [ ] **Step 3: Build the library** (`cmake --build .../build`). Fix fallout (callers of the changed ctor/lambda). Do NOT run the engine suite until Task 6 lands the backend-signature changes if they are interdependent; if the engine compiles standalone, run `ctest -R "tst_engine_unified_routing|tst_engine_universal_sink_dispatch"` and confirm green. Commit.

```bash
git -C /home/clinton/dev/libkalburator add src/engine/syncengine.cpp src/engine/syncengine.h src/shape/recordwriter.h
git -C /home/clinton/dev/libkalburator commit -m "engine: delete TranscodingPlan computation + ApplyContext seam + router (Plan 4 Task 5)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

## Task 6: Drop the `TranscodingPlan` parameter from the writer, items, and all backends

**Files:** `src/calendar/calendarplugin_writer.{h,cpp}`, `createincidenceitem.{h,cpp}`, `updateincidenceitem.{h,cpp}`, `syncbackend.{h,cpp}` (base), `akonadibackend`, `decsyncbackend`, `localbackend`, `mockbackend`, `orgbackend` (gated), `remotecalendarbackend`, `subscriptionbackend`, `contacts/akonadicontactsbackend`, `src/sync/syncbackendbase.h`.

- [ ] **Step 1: Writer.** In `calendarplugin_writer.{h,cpp}`: remove `m_plan`, `setTranscodingPlan()`, the `ctx.transcodingPlan` read in `prepareForApply`, and the plan args passed to `CreateIncidenceItem`/`UpdateIncidenceItem` (`:202-203`,`:215-216`). Remove the `transcodingplan.h` include.

- [ ] **Step 2: Items.** In `createincidenceitem.{h,cpp}` / `updateincidenceitem.{h,cpp}`: remove the `const TranscodingPlan&` ctor param + `m_plan` member; change the `backend()->pushItems(calendarId(), {m_incidence}, m_plan)` call to the 2-arg `pushItems(calendarId(), {m_incidence})`.

- [ ] **Step 3: Backend base + all subclasses.** Change the virtual `pushItems(const QString&, const QList<...>&, const TranscodingPlan&)` to `pushItems(const QString&, const QList<...>&)` on `syncbackend.h` (and `syncbackendbase.h` forward decl) and every override listed above; remove each backend's `executeTranscodingPlan(plan, original)` call (the records are already demoted by the shape pipeline before reaching the backend, so the backend writes them as-is). For `orgbackend.cpp` (gated, `executeTranscodingPlan` + the old `transcodingWarning` emit at `:237-239`): remove the plan execution and the backend-side warning emit (the warning now comes from the engine, Task 4). Drop `TranscodingPlan` from any `startSync` signatures that carry it. Remove now-dead `transcodingplan.h` includes.

- [ ] **Step 4: Build (default profile) + the full suite.** Green-gate:

```bash
cmake --build /home/clinton/dev/libkalburator/build
ctest --test-dir /home/clinton/dev/libkalburator/build
```
Expected: 111/112 (known flake only). If an org-gated file fails to compile, note it but the default profile does not build it; a follow-up `-DKALBURATOR_HAVE_ORG_IO=ON` configure to compile-check `orgbackend.cpp` is recommended in Step 5.

- [ ] **Step 5: Org-on compile check (best effort).**

```bash
cmake -S /home/clinton/dev/libkalburator -B /home/clinton/dev/libkalburator/build-orgon -DKALBURATOR_HAVE_ORG_IO=ON >/dev/null 2>&1 \
  && cmake --build /home/clinton/dev/libkalburator/build-orgon 2>&1 | tail -5 || echo "org-on profile unavailable (org-io dep missing) — note in report"
```
If the org-io dependency is present, `orgbackend.cpp` must compile with the new signatures. If the dep is absent in this environment, record that the org-on profile was not compile-checked (downstream/PlanStan covers it).

- [ ] **Step 6: Commit.**

```bash
git -C /home/clinton/dev/libkalburator add -A
git -C /home/clinton/dev/libkalburator commit -m "calendar,contacts: drop TranscodingPlan param from writer/items/backends (Plan 4 Task 6)

Downstream PlanStan/WildPalms backend subclasses adapt post-merge (FINDINGS O7-style).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

# PART 4 — Delete the machinery + close out (Tasks 7–9)

## Task 7: Delete the transcoding-router test

**Files:** Delete `tests/transcoding/tst_transcoding_router.cpp` (+ its `tests/transcoding/CMakeLists.txt` registration; remove the `add_subdirectory(transcoding)` if the dir becomes empty).

- [ ] **Step 1:** `git rm tests/transcoding/tst_transcoding_router.cpp`; remove its CMake registration; if `tests/transcoding/` is now empty, `git rm tests/transcoding/CMakeLists.txt` and drop the `add_subdirectory(transcoding)` from `tests/CMakeLists.txt`.
- [ ] **Step 2:** Configure + build (no test references the router now). Commit.

```bash
git -C /home/clinton/dev/libkalburator commit -m "test: drop transcoding-router test (Plan 4 Task 7)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

## Task 8: Delete the transcoding machinery

**Files:** Delete `src/transcoding/transcodingregistry.{h,cpp}`, `transcodingrouter.{h,cpp}`, `transcodingplan.{h,cpp}`, `rruletranscoder.{h,cpp}`, `propertytranscoder.{h,cpp}`. Modify `CMakeLists.txt`.

- [ ] **Step 1: Confirm zero references remain.**

```bash
grep -rn "TranscodingRegistry\|TranscodingRouter\|TranscodingPlan\|RRuleTranscoder\|PropertyTranscoder\|transcodingplan.h\|transcodingregistry.h\|transcodingrouter.h\|rruletranscoder.h\|propertytranscoder.h\|executeTranscodingPlan" /home/clinton/dev/libkalburator/src /home/clinton/dev/libkalburator/tests
```
Expected: empty (the `transcodingWarning` *signal* is fine — it has no `Transcoding`-type token; if it matches, confirm it is only the signal name and leave it). Resolve any straggler before deleting.

- [ ] **Step 2: Delete the files.**

```bash
git -C /home/clinton/dev/libkalburator rm src/transcoding/transcodingregistry.h src/transcoding/transcodingregistry.cpp \
  src/transcoding/transcodingrouter.h src/transcoding/transcodingrouter.cpp \
  src/transcoding/transcodingplan.h src/transcoding/transcodingplan.cpp \
  src/transcoding/rruletranscoder.h src/transcoding/rruletranscoder.cpp \
  src/transcoding/propertytranscoder.h src/transcoding/propertytranscoder.cpp
```
`src/transcoding/` is now empty — `rmdir src/transcoding` if git leaves it.

- [ ] **Step 3: `CMakeLists.txt`:** remove the five machinery header/source entries (the ones at the old `:193-194,196-198` headers and `:202-203,205-207` sources, minus the diff files already moved in Task 1) and the `src/transcoding` include-path export (`:590`). 

- [ ] **Step 4: Clean configure + full build + suite.**

```bash
rm -rf /home/clinton/dev/libkalburator/build
cmake -S /home/clinton/dev/libkalburator -B /home/clinton/dev/libkalburator/build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build /home/clinton/dev/libkalburator/build
ctest --test-dir /home/clinton/dev/libkalburator/build
```
Expected: clean build, 111/112 (known flake). STOP on any new failure.

- [ ] **Step 5: Commit.**

```bash
git -C /home/clinton/dev/libkalburator add -A
git -C /home/clinton/dev/libkalburator commit -m "transcoding: delete the retired machinery; src/transcoding gone (Plan 4 Task 8)

One transformation mechanism remains (the shape graph). Campaign invariant 1 satisfied.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

## Task 9: Final verification + STATUS/FINDINGS close-out

**Files:** Modify `docs/campaign/STATUS.md`, `docs/campaign/FINDINGS.md`.

- [ ] **Step 1: Prove the convergence.**

```bash
# Exactly one mechanism: no transcoding dir, RRULE simplification is a shape edge.
test ! -d /home/clinton/dev/libkalburator/src/transcoding && echo "src/transcoding gone ✓"
grep -rn "EncodingId{\"org-ical\"}\|EncodingId{ QStringLiteral(\"org-ical\")" /home/clinton/dev/libkalburator/src/calendar/calendarstockshapes.cpp
grep -rn "transcodingWarning" /home/clinton/dev/libkalburator/src/engine/syncengine.cpp   # signal kept, re-sourced
```
Expected: dir gone; org-ical edge registered; warning signal present (now loss-derived).

- [ ] **Step 2: Full clean build + suite** (as Task 8 Step 4). Record exact counts; re-run the lone failure once to confirm it is the `tst_providerlifecycle` flake.

- [ ] **Step 3: Update STATUS.** Set the Plan-4 row to **Complete**; set the top Status line to "Plans 1–4 complete — campaign converged; `src/transcoding/` retired"; update Last updated. Set Next action to the **downstream port** (PlanStan/WildPalms adopt the no-plan `pushItems`/`startSync` and the injecting `ShapeRegistries` ctor; consume the loss-derived warning) and pushing/merging the branch.

- [ ] **Step 4: FINDINGS.** Move O10 to Resolved (incidencediff/syncdiff now in `src/diff/`; transcoding machinery deleted). Add a new **open** watch item: "Downstream backend port — PlanStan/WildPalms `SyncBackend` subclasses still declare `pushItems(…, TranscodingPlan)`; they must drop the param to compile against this branch (post-merge, with O7's ctor port)." Note that the org backend wiring to `{calendar, org-ical}` is org-on/downstream work (invariant 8) the edge now supports.

- [ ] **Step 5: Commit.**

```bash
git -C /home/clinton/dev/libkalburator add docs/campaign/STATUS.md docs/campaign/FINDINGS.md
git -C /home/clinton/dev/libkalburator commit -m "docs: Plan 4 complete — transcoding retired, campaign converged (Plan 4 Task 9)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Self-review notes (author)

- **Spec coverage (design §11 acceptance):** `src/transcoding/` deleted (Task 8) ✓; RRULE simplification re-expressed as a shape edge with a `LossProfile`, no backend-type routing (Tasks 2–3) ✓; `ApplyContext` no longer carries `TranscodingPlan`, no `CalendarPluginWriter` special-casing (Tasks 5–6) ✓; calendar/contacts/todo all use the "declare peer + register edges to canon" pattern (Plan 3 + Task 3) ✓; canon recurrence stays raw RFC5545 text, parsed only on the structure-needing edge (`canon→org-ical`, invariant 3 — a parse failure is a localized Simplified loss) ✓; loss model distinguishes the four kinds (recurrence = Simplified, X-ORIGINAL carrier = Reversible) ✓; diff stays shape-side, `createCanonicalDiffer()` untouched (invariant 5) ✓.
- **WildPalms invariants (10):** the lossy-sync warning channel is preserved and re-sourced *before* the old source is removed (Task 4 precedes 5–6) — the channel never lapses; X-property round-trip stamping is exactly how org-ical reversibility works; `lossPolicy` now sees the composed path loss including `canon→org-ical`.
- **Scope boundary (invariant 8):** no backend capability-introspection objects, no load-time enforcement; wiring `OrgBackend` to `{calendar, org-ical}` is org-on/downstream work (the default profile doesn't build org). This plan lands the *edge* and the *warning re-sourcing* that org sync consumes — the mechanism, not the unbuilt consumer.
- **Green at each step (P3):** Task 1 is a pure move; 2–3 additive; 4 adds the new warning source before 5–6 remove the old; 7–8 delete only after refs are gone. Each task ends at a building, suite-green tree.
- **Two sources of truth reconciled (invariant 7):** design §10's "delete src/transcoding in full" corrected to "delete the machinery, relocate the diff engines" (FINDINGS O10); the human's two decisions recorded in the header before any code.
- **Convention (KCalendarCore bodies):** the org-ical stage bodies are specified as "reuse `icalcanonstages` + port the named `rruletranscoder.cpp` functions," with the round-trip/loss tests as the executable contract — the same documented convention Plan 3 used; not a placeholder.
- **Known risk:** Task 6 is the widest surface (every backend signature) and the highest-churn; it is mechanical but touches gated `orgbackend.cpp` (compile-checked best-effort in Step 5, fully covered downstream). Task 4's warning re-sourcing is the subtlest — its rewritten test is the proof the channel survives.
