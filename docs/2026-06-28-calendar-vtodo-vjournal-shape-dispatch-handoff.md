# Handoff: calendar shape pipeline drops VTODO/VJOURNAL — need per-incidence-type dispatch

**Date:** 2026-06-28
**From:** PlanStan (consumer; pins libkalburator `v0.79`)
**To:** libkalburator
**Severity:** High — silent data loss for tasks in any calendar↔calendar sync of a
hybrid (VEVENT+VTODO) CalDAV calendar.
**Status:** Root-caused with code + on-disk + content-cache evidence and a
**verified failing test** (snippet below, confirmed RED against `v0.79`).

> Placement note: this repo keeps handoffs flat under `docs/*-handoff.md` (six
> existing), so this lands there rather than a new `docs/handoffs/` subdir, where
> the existing workflow already looks.

---

## 1. One-paragraph summary

A PlanStan "local mirror" of a Nextcloud account writes the local copy of every
**VTODO** as a **0-byte `.ics`**, so reopening the collection loads nothing (the
empty files fail to parse). Root cause: the calendar shape graph's
`{calendar,ical}` ↔ `{calendar,canon}` stages (`ICalToCanonStage` /
`CanonToICalStage`) are **VEVENT-only**. When the sync engine transcodes a
calendar mapping, it compiles those pipelines and runs *every* item in the
calendar through them; a VTODO promotes to **empty canon** (and demotes to empty
iCal), because `ICalToCanonStage::parseEvent` does `dynamicCast<Event>()` →
null → returns `{}`. A separate, correct todo shape graph exists
(`src/todo/…`, domain `"todo"`) but is never consulted for todos that live
inside a `{calendar,ical}` backend. We want the engine to dispatch transforms
**per incidence type** so hybrid calendars round-trip VEVENT + VTODO + VJOURNAL
losslessly.

---

## 2. How PlanStan uses libkalburator (relevant slice)

- A PlanStan **collection** (`.kalb`) holds **LogicalCalendars**, each with one or
  more **CalendarBackendBindings** (a backend id + a per-backend calendar id +
  role). A binding's role is `Primary` (the authoritative local copy) or `Sync1…`
  (sync spokes).
- The new **local-mirror** topology (PlanStan wizard): for each remote CalDAV
  calendar we create a LogicalCalendar with **two** bindings — a `LocalBackend`
  **Primary** and the CalDAV **Sync1** — and set `SyncTopology::Star` (local =
  hub). `Kalburator::Sync::generateMappings` then produces one TwoWay mapping per
  calendar: `[local/<cal>] -> [<provider>/<cal>]`.
- Both `LocalBackend` and `RemoteCalendarBackend` declare a single native shape:
  `{ domain="calendar", encoding="ical" }` (see `nativeShapes()` in each). PlanStan
  does **not** declare the `todo` domain anywhere — a CalDAV "calendar" is one
  logical calendar to us, regardless of whether it holds events, tasks, or both.
- On collection open PlanStan drives `CollectionController::startDiscoveryAndSync`,
  which runs the engine (`SyncEngine::runSync(SyncRequest)`) for each mapping. The
  intent of the mirror is **offline-first**: the initial sync should populate the
  local Primary from the remote so subsequent opens load instantly from disk.
- The multi-protocol provider is created with `calendarsOnly=true`; contacts are
  intentionally excluded. (v0.79 already persists that flag — thank you.)

PlanStan-side context for the broader flow, if useful:
`PlanStan/docs/bugs/wizard-local-mirror-creation-flow.md`.

---

## 3. How the error showed up

User created a local-mirror collection of a real Nextcloud account (12
calendars, mix of event and task calendars; tasks created by the Tasks.org
Android app, `PRODID:+//IDN tasks.org//…`). First session displayed events;
**reopening showed an empty calendar** and re-pulled from the server.

After fixing several PlanStan-side ordering/path bugs (so the mirror is created
and the sync runs correctly), the underlying defect surfaced cleanly. On-disk
evidence from a fixed-build run (`TestJune28.kalb.d/calendars/`):

| Calendar | components | local files | empty (0-byte) | good |
|---|---|---|---|---|
| Acquire | VTODO only | 46 | **46** | 0 |
| Inbox | VTODO only | 8 | **8** | 0 |
| Someday | VTODO only | 9 | **9** | 0 |
| Waiting For | VTODO only | 6 | **6** | 0 |
| ACSW | VEVENT+VTODO | 54 | **31** | 23 (all VEVENT) |
| Next Actions | VEVENT+VTODO | 673 | **58** | 615 (all VEVENT) |
| TBS | VEVENT+VTODO (only events present) | 85 | 0 | 85 |

Pattern: **VEVENT files are correct; VTODO files are 0 bytes.** On reopen,
`LocalBackend::fetchItems` logs `kf.calendarcore: parse error … string=""` for
every empty file → nothing loads.

Ruled out (so you don't re-investigate):
- **Remote fetch is fine.** The CalDAV content cache holds valid VTODO iCal (213
  todos cached; we read one back: a well-formed `BEGIN:VTODO … END:VTODO`).
- **Serialization is fine.** `icalFromIncidence` / `ICalFormat::toString` on a
  `Todo::Ptr` produces valid VTODO bytes — even when the todo is already in
  another `MemoryCalendar` and serialized on a worker thread (`AsyncFileWriter`'s
  exact pattern). So the direct-serialize write path (`LocalBackend::startSync` →
  `AsyncFileWriter`) is **not** the culprit.
- The empty files therefore come from the **blob-transcode** path, i.e. records
  promoted/demoted through the shape pipelines.

Also note: the sync reports `+0 ~0 -0` for every mapping yet the (empty) files get
written — explained in §5.

---

## 4. The nature of CalDAV servers with hybrid calendars

This is core to why the bug matters and why per-type dispatch is the right model:

- A CalDAV calendar collection advertises a `supported-calendar-component-set`
  (RFC 4791 §5.2.3). It commonly contains **more than one** component type:
  `VEVENT`, `VTODO`, and/or `VJOURNAL`. Our capability discovery already reports
  this per calendar (e.g. `Calendar "ACSW" components - VEVENT: true VTODO: true`).
- **Nextcloud and task apps (Tasks.org, Apple Reminders, Thunderbird) routinely
  store VTODOs in the same calendar collection as VEVENTs.** A "calendar" and a
  "task list" are frequently the *same* DAV collection. There is no reliable way
  to model a CalDAV calendar as "events only."
- A single calendar-object resource (one `.ics`) is *usually* one component, but
  the collection as a whole is heterogeneous. A correct CalDAV client must handle
  a collection whose items are a **mix** of VEVENT / VTODO / VJOURNAL.
- Consequence for the shape system: the `{calendar,ical}` encoding is **not**
  synonymous with "VEVENT." Its items are per-resource incidences of *any*
  calendar component kind. Transcoding must branch on the **incidence kind of
  each record**, not on the backend/mapping domain.

---

## 5. Root cause (confirmed)

Engine path (per mapping), `src/engine/syncengine.cpp` ~1954–1958:

```cpp
const auto &reg = m_shape.transformation;
std::optional<Pipeline> srcToCanon = reg.compile(srcShape, canonical);
std::optional<Pipeline> tgtToCanon = reg.compile(tgtShape, canonical);
std::optional<Pipeline> canonToTgt = reg.compile(canonical, tgtShape);
std::optional<Pipeline> canonToSrc = reg.compile(canonical, srcShape);
```

For a mirror mapping `srcShape == tgtShape == {calendar,ical}` and
`canonical == {calendar,canon}`. The compiled pipelines are
`CalendarStockShapes`' calendar edges → `ICalToCanonStage` / `CanonToICalStage`.

`src/calendar/icalcanonstages.cpp`:

```cpp
// :22
KCalendarCore::Event::Ptr parseEvent(const QByteArray &data) {
    ...
    return inc.dynamicCast<KCalendarCore::Event>();   // null for a VTODO/VJOURNAL
}
// :177
QByteArray ICalToCanonStage::transform(const QByteArray& icalBytes) const {
    if (icalBytes.isEmpty()) return {};
    const auto event = parseEvent(icalBytes);
    if (!event) return {};                            // ← VTODO/VJOURNAL → empty canon
    ...
}
// :447,456  CanonToICalStage hardcodes:
KCalendarCore::Event::Ptr event(new KCalendarCore::Event);  // never a Todo/Journal
```

So for a VTODO record:
- `srcToCanon` and `tgtToCanon` both yield **empty canon**, so the differ sees
  source and target todos as equal/absent → **`+0 ~0 -0`** (the puzzling
  zero-count with non-empty calendars).
- the demote (`canonToSrc` / `canonToTgt`) yields **empty iCal** → the record
  written to the local Primary is empty → **0-byte `.ics`**.

The correct todo machinery already exists but in a different domain and is never
reached from a calendar mapping:
- `src/todo/todostockshapes.cpp`: edges `{todo,vtodo}` ↔ `{todo,canon}` via
  `VTodoToCanonStage` / `CanonToVTodoStage`.
- `src/todo/vtodocanonstages.{h,cpp}`; pinned by `tests/todo/tst_todo_canon_roundtrip.cpp`.

(The model still *displays* todos because PlanStan's model population parses iCal
directly via `incidencesFromIcal`, bypassing the shape graph. Only the
sync/transcode path is affected.)

---

## 6. Verified failing test (RED as of v0.79)

Add to `tests/calendar/tst_calendar_canon_roundtrip.cpp` (uses the existing
`makeCalendarRegistries()` and `kTestIcal` helpers in that file). This exercises
the **exact pipelines the engine compiles** (`compile({calendar,ical},
{calendar,canon})` and back) — it is not a stage-only unit test:

```cpp
// A CalDAV calendar legitimately holds VTODO/VJOURNAL alongside VEVENT.
void vtodoSurvivesIcalCanonRoundTrip()
{
    const auto regs = makeCalendarRegistries();
    const Shape ical { DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("ical")} };
    const Shape canon{ DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("canon")} };

    const auto toCanon = regs.transformation.compile(ical, canon);
    const auto toIcal  = regs.transformation.compile(canon, ical);
    QVERIFY(toCanon.has_value() && toIcal.has_value());

    const QByteArray vtodo =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\n"
        "BEGIN:VTODO\r\nUID:todo-handoff-1\r\nSUMMARY:Buy milk\r\n"
        "STATUS:NEEDS-ACTION\r\nPERCENT-COMPLETE:0\r\n"
        "END:VTODO\r\nEND:VCALENDAR\r\n";

    const QByteArray canonBytes = toCanon->apply(vtodo);
    QVERIFY2(!canonBytes.isEmpty(),
             "VTODO must promote to non-empty canon (calendar stage is VEVENT-only)");

    const QByteArray roundTripped = toIcal->apply(canonBytes);
    QVERIFY2(roundTripped.contains("VTODO"), "VTODO must survive ical->canon->ical");
    QVERIFY2(roundTripped.contains("UID:todo-handoff-1"), "VTODO uid must survive");
}
```

Observed failure (v0.79):

```
FAIL!  : TestCalendarCanonRoundtrip::vtodoSurvivesIcalCanonRoundTrip()
         '!canonBytes.isEmpty()' returned FALSE.
         (VTODO must promote to non-empty canon (calendar stage is VEVENT-only))
```

A VEVENT control through the same compiled pipelines passes, proving the pipeline
machinery itself is sound — only the calendar stages' type handling is wrong.

---

## 7. Proposed direction (requirements, not a prescribed design)

PlanStan's ask is **per-incidence-type shape dispatch in the engine** so that a
calendar mapping transcodes each record according to its component kind. We're
deliberately leaving the concrete design to you — you own the shape/canon system
and the canonical-spine/loss-profile model. Requirements and constraints:

**Must:**
1. A calendar↔calendar sync of a hybrid calendar round-trips **VEVENT, VTODO, and
   VJOURNAL** without data loss; no record ever transcodes to empty bytes.
2. The fix is keyed on the **incidence/component kind of each record**, not on the
   backend/mapping domain — a `{calendar,ical}` backend keeps declaring one shape;
   PlanStan should not have to split a calendar into per-type bindings or declare
   the `todo` domain.
3. Reuse the existing, tested todo canon (`VTodoToCanonStage` /
   `CanonToVTodoStage`) rather than duplicating todo logic in the calendar stages,
   if that fits your dispatch model. VJOURNAL needs equivalent handling (a stage
   may not exist yet).
4. Preserve existing VEVENT behavior and the current loss-profile semantics for
   events (don't regress `tst_calendar_canon_roundtrip` /
   `tst_orgical_canon_roundtrip`).

**Should:**
5. Decide and document how the canonical-spine/loss-profile applies across kinds
   within the `calendar` domain (e.g. a per-kind canon, or a kind discriminator in
   the calendar canon envelope). If todos route to the `todo` domain canon, define
   how the engine selects the right `canonical` shape per record.
6. Fail **loud, not silent**: if a record's kind has no transform path, the engine
   should error the mapping (so it can't clobber/empty data), not write empty bytes.

**Non-goals / open questions for you:**
- Whether dispatch lives in the engine (`dispatchSync` selecting per-record
  pipelines) or inside the calendar stages (kind-branching with delegation) — your
  call; requirement #2 only fixes *what*, not *where*.
- Whether VJOURNAL gets a first-class canon now or a lossless passthrough.
- Whether the `todo` domain should remain separate or fold into `calendar`.

**Acceptance criteria:**
- The §6 test passes; an analogous VJOURNAL round-trip passes.
- An engine-level reconcile test of a hybrid calendar (events + todos in one
  mapping) propagates both to the target (non-empty), with correct add/update/
  delete counts (not the current `+0`).
- libkalburator suite stays green (151/151 at v0.79).

---

## 8. Coordination

- PlanStan pins `v0.79`. After this lands (say `v0.80`) we'll bump the pin and
  re-verify end-to-end against the live Nextcloud account: todo-only calendars
  must write non-empty `.ics`, and reopening must load tasks from the local mirror
  with no parse errors.
- Our `docs/bugs/libkalburator-calendar-canon-drops-vtodo.md` mirrors this
  analysis and will be closed when the pin bump verifies the fix.
- Questions / live repro data (the Nextcloud account, cached VTODO samples) are
  available on the PlanStan side — ask.
