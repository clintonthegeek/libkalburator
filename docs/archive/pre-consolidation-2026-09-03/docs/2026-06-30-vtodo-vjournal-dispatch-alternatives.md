# Analysis: calendar shape pipeline drops VTODO/VJOURNAL — alternatives evaluation

> **SUPERSEDED (2026-07-19).** This parallel analysis independently recommended
> "Alternative A" (kind dispatch inside the calendar stages), which is exactly
> the design that had already shipped as **v0.80** (envelope `kind` discriminator
> + shared VTODO helpers + first-class VJOURNAL + fail-loud guard; commits
> `5498804` → `d89a863` → `eae5bb1`, handoff resolved `7e8b5e6`). Kept for
> provenance only. See `docs/2026-07-19-consumer-coordination-status.md` §2 and
> the shipped spec/plan under `docs/superpowers/{specs,plans}/2026-06-28-calendar-per-kind-canon-dispatch*`.

**Date:** 2026-06-30  
**Context:** Response to `docs/2026-06-28-calendar-vtodo-vjournal-shape-dispatch-handoff.md`

## Summary of the defect
The `{calendar,ical}` ↔ `{calendar,canon}` edges are implemented solely by `ICalToCanonStage` / `CanonToICalStage` which perform a hard `dynamicCast<Event>()` (and construct only `Event` on the reverse path). Any VTODO or VJOURNAL record therefore produces an empty canon blob, resulting in silent data loss (0-byte `.ics` files) on hybrid CalDAV calendars.

The correct VTODO implementation already exists in the separate `todo` domain (`VTodoToCanonStage` / `CanonToVTodoStage`), but is unreachable from a calendar mapping because:
- `compile({calendar,ical}, {calendar,canon})` only consults calendar-domain edges,
- no cross-domain edges are registered (explicit v1 design decision in `TransformationRegistry`),
- the engine's `dispatchSync` selects pipelines once per mapping, not per record.

## Requirements restated (from handoff)
Must:
1. Hybrid calendar round-trips VEVENT + VTODO + VJOURNAL losslessly.
2. Dispatch keyed on incidence kind of each record, not on backend/mapping domain.
3. Reuse existing todo canon stages rather than duplicate logic.
4. Preserve existing VEVENT behavior and loss profiles.

Should:
5. Decide/document how the canonical-spine/loss-profile model extends to multiple kinds inside `calendar`.
6. Fail loud (not silent empty bytes) when a kind has no transform.

Non-goals (explicitly left open):
- Whether dispatch lives in the engine or inside the calendar stages.
- VJOURNAL first-class canon vs. passthrough.
- Whether the `todo` domain should remain separate.

## Alternative designs evaluated

### A. Incidence-kind dispatch inside the calendar stages (recommended)
- `ICalToCanonStage::transform` inspects the first component kind (`BEGIN:VEVENT|VTODO|VJOURNAL`) and delegates to a thin wrapper around the appropriate existing stage (or a new `VJournalToCanonStage` when added).
- Reverse direction (`CanonToICalStage`) likewise branches on a discriminator field in the canon envelope (or on the concrete incidence type after deserialization).
- The `{calendar,ical}` native shape contract is unchanged; PlanStan continues to declare a single shape per calendar binding.
- Reuses `VTodoToCanonStage` verbatim (requirement 3).
- VJOURNAL can be added incrementally; until then the stage can either emit a loud error or a reversible passthrough (satisfies "fail loud").
- No engine changes, no cross-domain edges, no new canonical shapes.
- Loss profiles remain per-edge and can be extended with a per-kind table if needed later.

**Fit to requirements:** Meets all Must/Should items. Minimal surface area. Preserves the existing domain separation.

### B. Per-kind canonical sub-shapes (`calendar/canon-event`, `calendar/canon-todo`, …)
- The calendar domain would register multiple canonical hubs. The engine (or a pre-classification step) would choose the right target canonical per record.
- Requires changes to `TransformationRegistry::compile`, the canonical-spine model, and every consumer that constructs `SyncRequest` or inspects results.
- Violates requirement 2 (PlanStan would have to reason about sub-shapes) and is overkill for the narrow bug.

**Fit:** Technically viable but violates the "keyed on incidence kind, not domain" constraint and increases cognitive load for consumers.

### C. Cross-domain edges + engine per-record dispatch
- Register edges `{calendar,ical}` → `{todo,canon}` (and vice-versa). Modify `dispatchSync` (or the worker) to inspect each record and select the appropriate canonical target.
- Directly contradicts the documented v1 invariant ("no cross-domain edges") and the handoff's explicit non-goal ("whether the todo domain should remain separate").
- Forces every calendar mapping to know about the todo domain even when no todos are present.

**Fit:** Works but violates architectural invariants and increases coupling.

### D. Fold the todo domain into calendar entirely
- Merge `todo/*` sources, shapes, and tests into `calendar/*`. `{calendar,ical-vtodo}` becomes a peer encoding.
- Large refactor; breaks existing pure-todo users (Todo.txt, etc.); contradicts the current clean separation that already works for non-calendar todo backends.

**Fit:** Not justified by the stated requirements.

## Recommended path forward
Adopt **Alternative A** (kind dispatch inside the calendar stages). It is the only design that simultaneously satisfies:
- all hard requirements,
- the "no cross-domain in v1" rule,
- the desire to keep the `todo` domain as a first-class concept for non-calendar consumers,
- the principle of failing loud rather than producing empty blobs.

### Concrete sketch (implementation notes)
1. Add a small helper `IncidenceKind detectIncidenceKind(const QByteArray&)` in `icalcanonstages.cpp` (parse only the first `BEGIN:` line).
2. `ICalToCanonStage::transform`:
   - VEVENT → existing `parseEvent` + canon construction (unchanged).
   - VTODO → delegate to `VTodoToCanonStage` (or a thin wrapper that reuses its implementation).
   - VJOURNAL → either (a) delegate to a new `VJournalToCanonStage` (when written) or (b) return an error envelope that the engine turns into a mapping failure.
3. `CanonToICalStage::transform`:
   - Inspect a discriminator field in the canon JSON (e.g. `"kind":"event"|"todo"|"journal"`) or deserialize to the concrete `Incidence::Ptr` type.
   - Route to the corresponding `CanonTo*Stage`.
4. The existing `CanonEnvelope` / loss-profile machinery can carry a per-kind loss table if finer granularity is later desired; for now the per-edge profiles already declared on the calendar stock shapes remain valid.
5. Add the two round-trip tests from the handoff (§6) plus an engine-level hybrid-calendar reconcile test. The suite must stay green.

This change is localized to `src/calendar/icalcanonstages.{h,cpp}` (and a possible new `journalcanonstages.{h,cpp}` later), touches no public API, and requires zero modifications in PlanStan or any other consumer.

## Open questions left to the implementer (as requested by the handoff)
- Exact discriminator field name in the calendar canon envelope (or whether we rely on the concrete incidence type after deserialization).
- Whether VJOURNAL receives a first-class canon stage in the same patch or starts as a loud-error / passthrough.
- Whether the loss-profile for calendar should eventually expose a `LossProfile perKind(Incidence::Type)` helper.

## Coordination note
Once implemented and the §6 test + hybrid-engine test pass, the handoff can be closed and PlanStan can bump its pin from v0.79. No further coordination is required beyond the normal release process.