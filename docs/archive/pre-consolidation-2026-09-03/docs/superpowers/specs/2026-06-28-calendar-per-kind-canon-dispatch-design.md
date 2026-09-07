# Design: per-kind canon dispatch for the calendar shape pipeline

**Date:** 2026-06-28
**Status:** Approved design — ready for implementation plan
**Driver:** PlanStan handoff `docs/2026-06-28-calendar-vtodo-vjournal-shape-dispatch-handoff.md`
**Fixes:** silent data loss — VTODO/VJOURNAL records in a `{calendar,ical}` backend
transcode to empty bytes (0-byte `.ics`) because the calendar canon stages are
VEVENT-only.

---

## 1. Problem (one paragraph)

A CalDAV calendar collection legitimately holds a **mix** of VEVENT, VTODO, and
VJOURNAL components (RFC 4791 `supported-calendar-component-set`; Nextcloud +
task apps routinely store VTODOs beside VEVENTs). The sync engine compiles one
set of transform pipelines per mapping and runs **every** record through them.
The calendar stages `ICalToCanonStage` / `CanonToICalStage`
(`src/calendar/icalcanonstages.cpp`) only handle `Event`:
`parseEvent` does `dynamicCast<Event>()` → null for a VTODO/VJOURNAL → returns
`{}`. So a todo promotes to **empty canon** (differ sees it as absent → `+0 ~0 -0`)
and demotes to **empty iCal** (0-byte file written to the local mirror). A correct
todo canon exists (`src/todo/`, domain `"todo"`) but is never reached from a
calendar mapping.

## 2. Key finding: the cut is stage-deep, not engine-deep

The engine and the entire diff/merge/baseline machinery are **already
kind-agnostic** — confirmed by reading the code:

- `SyncEngine::dispatchSync` compiles 4 pipelines per mapping and applies
  `pipeline.apply(r.data)` over every record's bytes
  (`syncengine.cpp:1955-2073` promote; `2713-2743` demote). It never inspects
  the bytes' content.
- `CanonJsonDiffer` (`canonjsondiffer.cpp`) parses canon JSON and compares a
  property list **keyed by uid**. It treats the canon body opaquely; it does not
  know event vs todo vs journal.
- The baseline store persists canon bytes; the merger merges canon JSON. Same.

The **only** code that hard-codes "VEVENT" is the two calendar stages. Therefore
a kind-tagged canon flows through the kind-agnostic engine unchanged, and the fix
requires **no** per-record pipeline dispatch in the engine, **no** mixed
canonical shapes per mapping, and **no** PlanStan-side change (a `{calendar,ical}`
backend keeps declaring its single shape — handoff req #2).

The change is confined to:

1. the canon **envelope** (add a `kind` discriminator),
2. the two `{calendar,ical}` ↔ `{calendar,canon}` **stages** (become
   kind-dispatchers),
3. the calendar canon **property catalogue** (union the field-set across kinds),
4. one **fail-loud guard** in the engine (safety net, not a dispatch mechanism).

The `org-ical` edges and the entire `todo` domain are untouched.

## 3. Canon model: one tagged calendar canon

`{calendar,canon}` becomes **the** canonical iCalendar component, of which there
are three kinds. VEVENT, VTODO, and VJOURNAL all promote into the **calendar**
domain, distinguished by a `kind` field. This matches reality (a VCALENDAR holds
all three) and keeps one canonical shape per mapping.

The `todo` domain (`{todo,canon}`, `{todo,ical-vtodo}`, `{todo,todotxt}`) remains
as-is, for backends that genuinely declare the todo domain (e.g. a todotxt sink).
Reuse between the two domains is achieved by **sharing field-mapping helpers**
(§5), not by cross-domain envelope routing.

### 3.1 Envelope change: `kind`

Extend the `_canon` object from `{"domain","v"}` to `{"domain","v","kind"}`:

```json
{ "_canon": {"domain":"calendar","v":1,"kind":"vtodo"}, "uid":"...", ... }
```

- `CanonEnvelope::stampEnvelope(obj, domain, uid)` gains an optional
  `kind` parameter (default `""` → not written).
- A new `CanonEnvelope::kind(obj) -> QString` reader returns `""` when absent.
- **Absent kind ⇒ treat as `vevent`.** Every existing v1 calendar baseline on
  disk has no `kind` and is interpreted as an event — so there is **no schema
  version bump and no migration**. The change is additive and backward-compatible.
- `kCanonVersion` stays `1`.

Valid `kind` values for the calendar domain: `vevent`, `vtodo`, `vjournal`.

## 4. Dispatcher stages

The registered `{calendar,ical}` ↔ `{calendar,canon}` edges keep their stage
classes (`ICalToCanonStage` / `CanonToICalStage`) but their bodies become thin
dispatchers:

- **Promote (`ical → canon`):** parse the iCal bytes once; `dynamicCast` the
  incidence to `Event` / `Todo` / `Journal`; route to the matching field-mapper
  (§5); stamp the envelope with `domain=calendar` and the corresponding `kind`.
- **Demote (`canon → ical`):** read `kind` from the envelope (absent ⇒ `vevent`);
  route to the matching canon→incidence builder (§5); serialize.

Only these two edges change. `OrgICalToCanonStage` / `CanonToOrgICalStage` stay
event-only (org-mode does not express VTODO/VJOURNAL as iCal in this codebase;
confirmed `dynamicCast<Event>` at `orgicalcanonstages.cpp:128`).

## 5. Reuse, not duplication (handoff req #3, repo invariant 1)

Today the VEVENT field-mapping is inline in the calendar stage body, and the
VTODO logic lives in `Todo::VTodoToCanonStage` / `Todo::CanonToVTodoStage`
producing a `{todo,canon}` envelope. Extract the **envelope-free field bodies**
into shared free functions so there is exactly one VTODO mechanism:

- `eventFieldsToCanon(const Event::Ptr&) -> QJsonObject` /
  `canonToEvent(const QJsonObject&) -> Event::Ptr`
  — lifted verbatim from the current `ICalToCanonStage` / `CanonToICalStage`
  bodies (everything except `stampEnvelope` and the empty-input guard).
- `vtodoFieldsToCanon(const Todo::Ptr&) -> QJsonObject` /
  `canonToVtodo(const QJsonObject&) -> Todo::Ptr`
  — lifted from the existing `VTodoToCanonStage` / `CanonToVTodoStage` bodies.
- `vjournalFieldsToCanon(const Journal::Ptr&) -> QJsonObject` /
  `canonToVjournal(const QJsonObject&) -> Journal::Ptr`
  — **new**, first-class (§6).

Consumers:

- The **calendar dispatcher** calls the matching helper and wraps with
  `stampEnvelope(domain="calendar", kind=...)`.
- The **existing `{todo,canon}` stages** keep working by calling the same
  `vtodoFieldsToCanon` / `canonToVtodo` helper and wrapping with
  `stampEnvelope(domain="todo")` — proving the extraction is behaviour-preserving
  for the todo domain.

The exact landing namespace/file for the shared helpers (calendar vs a neutral
location) is an implementation detail for the plan; the constraint is **one
definition per kind**, called by both domains where applicable.

## 6. VJOURNAL: first-class field mapping

VJOURNAL has no existing canon stage. Map its real fields, symmetric with the
other kinds:

- `uid`, `summary`, `description`, `descriptionHtml` (X-ALT-DESC),
- `dtStart` (journals have a start but no end), `allDay`,
- `categories`, `status`, `classification`, `color`, `url`,
- `created`, `lastModified`, `sequence`,
- `providerExtras["x-ical"]` catch-all for unmapped X- properties (same pattern
  the event stage already uses, so unknown props survive regardless).

A `canon → vjournal` `LossProfile` is added alongside `canonToIcalLoss()` /
`canonToVtodoLoss()` (likely empty or near-empty — journals are simple).

## 7. Property catalogue union (handoff acceptance: correct counts)

`calendarcanonproperties.cpp` currently lists VEVENT fields. Extend it to the
**union** across all three kinds (add e.g. `due`, `completed`,
`percentComplete`, `relatedTo`, and journal-relevant fields). `CanonJsonDiffer`
iterates this list, so without the union it cannot detect changes to
todo/journal-specific fields and updates to them would not propagate (violating
the "correct add/update/delete counts" acceptance criterion). Fields absent on a
given kind simply don't appear in that record's canon, so the union is harmless
to events.

## 8. Fail loud, never empty (handoff req #6)

Add a guard at the existing promote (~`syncengine.cpp:2069`) and demote
(`2713-2743`) apply sites: **a non-empty input record whose transform yields
empty bytes fails the mapping** with an error naming the record uid and the
src/tgt/canonical shapes. Rationale:

- A legitimate transform never empties a record (deletes/tombstones are handled
  separately, not via empty transform output), so the guard has no false
  positives.
- It satisfies req #6 without adding an error channel to the `TransformationStage`
  interface.
- It would have caught the original bug at first sync instead of silently writing
  0-byte files — a permanent backstop against this class of regression.

## 9. Testing

| Test | Purpose | Expectation |
|---|---|---|
| `vtodoSurvivesIcalCanonRoundTrip` (handoff §6) | VTODO through compiled `{calendar,ical}↔{calendar,canon}` pipelines | RED → GREEN |
| `vjournalSurvivesIcalCanonRoundTrip` (new, analogous) | VJOURNAL round-trip | GREEN |
| `tst_calendar_canon_roundtrip` (existing VEVENT) | event behaviour unchanged | stays GREEN |
| `tst_orgical_canon_roundtrip` (existing) | org-ical untouched | stays GREEN |
| `tst_todo_canon_roundtrip` (existing) | `{todo,canon}` output unchanged after helper extraction | stays GREEN |
| Engine-level hybrid reconcile (new) | events + todos in **one** mapping propagate to target | both non-empty; correct add/update/delete counts (not `+0`) |
| Fail-loud guard (new) | record with no transform path | mapping errors, target not emptied |

Full suite stays green (151/151 at v0.79 baseline). Ship as **v0.80**; PlanStan
bumps the pin and re-verifies end-to-end against the live Nextcloud account
(todo-only calendars write non-empty `.ics`; reopen loads tasks from the local
mirror with no parse errors).

## 10. Out of scope / non-goals

- No engine-level per-record pipeline dispatch (§2 makes it unnecessary).
- No fold of the `todo` domain into `calendar` — the domains stay separate and
  share helpers (§5).
- No canon schema version bump / baseline migration (§3.1 makes it unnecessary).
- `org-ical` and contacts/memo/blob domains unchanged.
```
