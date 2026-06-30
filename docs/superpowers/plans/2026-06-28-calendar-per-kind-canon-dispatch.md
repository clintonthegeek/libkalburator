# Calendar Per-Kind Canon Dispatch — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a `{calendar,ical}`↔`{calendar,canon}` sync round-trip VEVENT, VTODO, and VJOURNAL without data loss, by teaching the two calendar canon stages to dispatch on each record's component kind.

**Architecture:** The engine, differ, merger, and baseline store are already kind-agnostic (they operate on canon JSON keyed by uid and never inspect record content). So the fix is confined to: a `kind` discriminator in the canon envelope; the two calendar stages becoming kind-dispatchers that reuse extracted, shared field-mapping helpers (VTODO logic shared with the existing `todo` domain); a property-catalogue union so todo/journal field changes are detected; and one fail-loud guard so a record can never silently transcode to empty bytes. No engine dispatch change, no mixed canonical shapes, no PlanStan-side change, no baseline migration.

**Tech Stack:** C++17, Qt6, KCalendarCore (KF6), QtTest, CMake. Single static lib `Kalburator::Sync` (alias of target `kalburator`) — all `src/` sources compile into it, so calendar code may call todo helpers directly.

## Global Constraints

- Build: `cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` then `cmake --build build`. Default profile `KALBURATOR_HAVE_ORG_IO=OFF`, `KALBURATOR_HAVE_AKONADI=OFF`.
- Build/test parallelism: `make -j 8` / `ctest -j 8` ONLY (GCC ICEs under all-core load on this machine). Never `--parallel` without `-j 8`.
- Canon envelope schema version stays `kCanonVersion = 1`. Absent `kind` MUST be interpreted as `vevent` (backward compat with on-disk v1 baselines). No migration.
- Reuse, do not duplicate: VTODO field-mapping has exactly ONE definition, called by both the `todo` domain stages and the calendar dispatcher (handoff req #3, repo invariant 1: extend the shape graph, never fork a mechanism).
- A non-empty input record must NEVER transcode to empty bytes silently — fail the mapping loudly instead (handoff req #6).
- Preserve VEVENT behaviour and existing loss-profile semantics; do not regress `tst_calendar_canon_roundtrip`, `tst_orgical_canon_roundtrip`, or `tst_todo_canon_roundtrip`.
- Branch: `feature/calendar-per-kind-canon-dispatch` (already created; spec committed at `4b290a1`).
- Full suite must stay green (151 tests at v0.79 baseline) before shipping as v0.80.

## File Structure

**Created:**
- `src/todo/vtodocanonfields.h` / `.cpp` — envelope-free VTODO ⇄ canon-object field mapping (extracted from `vtodocanonstages.cpp`).
- `src/calendar/eventcanonfields.h` / `.cpp` — envelope-free VEVENT ⇄ canon-object field mapping (extracted from `icalcanonstages.cpp`).
- `src/calendar/journalcanonfields.h` / `.cpp` — VJOURNAL ⇄ canon-object field mapping (new) + `canonToVjournalLoss()`.
- `src/engine/transcodeguard.h` — `transcodeEmptiedRecord(in,out)` predicate (header-only).
- `tests/calendar/tst_calendar_kind_dispatch.cpp` — VTODO/VJOURNAL round-trip through compiled calendar pipelines (the handoff §6 test + analogues) and dispatcher defensive-empty unit tests.
- `tests/calendar/tst_calendar_hybrid_reconcile.cpp` — engine-level reconcile of a mixed event+todo calendar.
- `tests/engine/tst_transcode_guard.cpp` — unit test for the fail-loud predicate.

**Modified:**
- `src/shape/canonenvelope.h` / `.cpp` — add `kind` to the envelope.
- `src/todo/vtodocanonstages.cpp` — re-wire stages onto the extracted helpers.
- `src/calendar/icalcanonstages.h` / `.cpp` — stages become kind-dispatchers.
- `src/calendar/calendarcanonproperties.cpp` — union the property catalogue across kinds.
- `src/engine/syncengine.cpp` — wire the fail-loud guard at the promote + two demote apply sites.
- `CMakeLists.txt` — add the new lib sources.
- `tests/calendar/CMakeLists.txt` / `tests/engine/CMakeLists.txt` — register new tests.

---

### Task 1: Canon envelope `kind` discriminator

**Files:**
- Modify: `src/shape/canonenvelope.h`
- Modify: `src/shape/canonenvelope.cpp:21-33`
- Test: `tests/todo/tst_todo_canon_roundtrip.cpp` (reuse existing test target; add one slot) — OR a tiny standalone is unnecessary, fold into the dispatch test in Task 5. **Use the inline test below in a scratch slot of `tst_calendar_canon_roundtrip.cpp`.**

**Interfaces:**
- Produces:
  - `QString Kalburator::Shape::CanonEnvelope::kindKey();  // "kind"`
  - `void stampEnvelope(QJsonObject& obj, const QString& domain, const QString& uid, const QString& kind = QString());`
  - `QString Kalburator::Shape::CanonEnvelope::kind(const QJsonObject& obj);  // "" if absent`

- [ ] **Step 1: Write the failing test**

Add this slot to `tests/calendar/tst_calendar_canon_roundtrip.cpp` (inside the `private slots:` block), and add `#include <QJsonObject>` is already present:

```cpp
    void envelopeStampsAndReadsKind()
    {
        using namespace Kalburator::Shape::CanonEnvelope;
        QJsonObject obj;
        stampEnvelope(obj, QStringLiteral("calendar"), QStringLiteral("u-1"),
                      QStringLiteral("vtodo"));
        QCOMPARE(kind(obj), QStringLiteral("vtodo"));
        const QJsonObject canon = obj.value(QStringLiteral("_canon")).toObject();
        QCOMPARE(canon.value(QStringLiteral("kind")).toString(),
                 QStringLiteral("vtodo"));

        // Default (no kind) writes no kind key and reads back empty.
        QJsonObject ev;
        stampEnvelope(ev, QStringLiteral("calendar"), QStringLiteral("u-2"));
        QVERIFY(kind(ev).isEmpty());
        QVERIFY(!ev.value(QStringLiteral("_canon")).toObject()
                    .contains(QStringLiteral("kind")));
    }
```

Add the include at the top of the test file (after the existing canon include):

```cpp
#include "canonenvelope.h"   // already present — provides CanonEnvelope::kind, stampEnvelope
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target tst_calendar_canon_roundtrip -j 8
./build/tests/calendar/tst_calendar_canon_roundtrip envelopeStampsAndReadsKind
```
Expected: FAIL to compile — `kind` is not a member of `CanonEnvelope`, and `stampEnvelope` takes 3 args not 4.

- [ ] **Step 3: Declare the new API in the header**

In `src/shape/canonenvelope.h`, after the `providerExtrasKey();` line (line 20), add:

```cpp
QString kindKey();           // "kind"
```

Change the `stampEnvelope` declaration (line 30) to:

```cpp
/// Stamp the envelope: sets _canon={domain,v=kCanonVersion[,kind]} and uid.
/// `kind` is written only when non-empty. Leaves all other keys untouched.
void stampEnvelope(QJsonObject& obj, const QString& domain, const QString& uid,
                   const QString& kind = QString());
```

After the `QString uid(const QJsonObject& obj);` line (line 33), add:

```cpp
/// Read the component kind from _canon.kind (empty if absent ⇒ caller treats
/// as "vevent" for the calendar domain).
QString kind(const QJsonObject& obj);
```

- [ ] **Step 4: Implement in the .cpp**

In `src/shape/canonenvelope.cpp`, after the `providerExtrasKey()` definition (line 9) add:

```cpp
QString kindKey()           { return QStringLiteral("kind"); }
```

Replace the `stampEnvelope` body (lines 21-28) with:

```cpp
void stampEnvelope(QJsonObject& obj, const QString& domain, const QString& uid,
                   const QString& kind)
{
    QJsonObject canon;
    canon.insert(QStringLiteral("domain"), domain);
    canon.insert(QStringLiteral("v"), kCanonVersion);
    if (!kind.isEmpty())
        canon.insert(kindKey(), kind);
    obj.insert(canonKey(), canon);
    obj.insert(uidKey(), uid);
}
```

After the `uid(...)` definition (lines 30-33) add:

```cpp
QString kind(const QJsonObject& obj)
{
    return obj.value(canonKey()).toObject().value(kindKey()).toString();
}
```

- [ ] **Step 5: Run test to verify it passes**

```bash
cmake --build build --target tst_calendar_canon_roundtrip -j 8
./build/tests/calendar/tst_calendar_canon_roundtrip envelopeStampsAndReadsKind
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/shape/canonenvelope.h src/shape/canonenvelope.cpp tests/calendar/tst_calendar_canon_roundtrip.cpp
git commit -m "feat(canon): add optional kind discriminator to the envelope"
```

---

### Task 2: Extract VTODO field helpers (behaviour-preserving refactor)

Move the field-mapping bodies out of the `todo` stages into shared free functions, so the calendar dispatcher (Task 5) can reuse the exact same VTODO logic. The `{todo,canon}` stages keep working — `tst_todo_canon_roundtrip` is the regression proof.

**Files:**
- Create: `src/todo/vtodocanonfields.h`
- Create: `src/todo/vtodocanonfields.cpp`
- Modify: `src/todo/vtodocanonstages.cpp`
- Modify: `CMakeLists.txt` (add the two new files)
- Test: `tests/todo/tst_todo_canon_roundtrip.cpp` (existing — must stay green)

**Interfaces:**
- Produces (namespace `Kalburator::Todo`):
  - `QJsonObject todoFieldsToCanon(const KCalendarCore::Todo::Ptr& todo, const QByteArray& originalBytes);` — all canon fields EXCEPT the envelope (`_canon`/`uid`). Needs `originalBytes` for verbatim recurrence-line extraction.
  - `QByteArray canonObjectToVtodoBytes(const QJsonObject& obj);` — full VTODO iCal bytes incl. recurrence injection. Reads `uid` from `obj`.

- [ ] **Step 1: Create the helper header**

Create `src/todo/vtodocanonfields.h`:

```cpp
#pragma once

#include <KCalendarCore/Todo>
#include <QByteArray>
#include <QJsonObject>

namespace Kalburator::Todo {

/// Map a parsed VTODO to canon JSON fields (NO envelope). `originalBytes` is the
/// source iCal, used to extract RRULE/RDATE/EXDATE verbatim (invariant 3).
QJsonObject todoFieldsToCanon(const KCalendarCore::Todo::Ptr& todo,
                              const QByteArray& originalBytes);

/// Build full VTODO iCal bytes from canon JSON (envelope-aware: reads "uid" etc.).
QByteArray canonObjectToVtodoBytes(const QJsonObject& obj);

}  // namespace Kalburator::Todo
```

- [ ] **Step 2: Create the helper .cpp by moving the existing bodies**

Create `src/todo/vtodocanonfields.cpp`. Move into it, **verbatim**, the entire anonymous-namespace helper block from `src/todo/vtodocanonstages.cpp:14-125` (the `parseTodo`, `serializeTodo`, `dateTimeToJson`, `jsonToDateTime`, `statusToString`, `statusFromString`, `extractRecurrenceLines` helpers and their `using` declarations). Then add the two public functions whose bodies are the existing stage bodies minus the envelope/parse boilerplate:

```cpp
#include "vtodocanonfields.h"

#include "canonenvelope.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include <QJsonArray>
#include <QJsonDocument>
#include <QTimeZone>

namespace {
// >>> MOVE here, verbatim, the anonymous-namespace block from
// >>> vtodocanonstages.cpp lines 14-125 (parseTodo, serializeTodo,
// >>> dateTimeToJson, jsonToDateTime, statusToString, statusFromString,
// >>> extractRecurrenceLines, and the four `using` lines at 16-19).
// >>> parseTodo is no longer used by the field helpers (the caller passes a
// >>> Todo::Ptr) but serializeTodo IS used; keep both — parseTodo is still
// >>> referenced by canonObjectToVtodoBytes? No: remove parseTodo if it causes
// >>> an unused-function warning, OR keep it (it is used by the stage in Task-2
// >>> step 4). Keep it.
} // namespace

namespace Kalburator::Todo {

QJsonObject todoFieldsToCanon(const KCalendarCore::Todo::Ptr& todo,
                              const QByteArray& originalBytes)
{
    QJsonObject obj;
    // >>> MOVE here the body of VTodoToCanonStage::transform from
    // >>> vtodocanonstages.cpp lines 142-297 (everything AFTER the
    // >>> `const auto todo = parseTodo(...)` guard and BEFORE the
    // >>> `stampEnvelope(...)` / `return serialize(...)` lines), with two edits:
    // >>>   * the local `const QString uid = todo->uid();` line (145) — DELETE
    // >>>     it here (the envelope is stamped by the caller).
    // >>>   * every `extractRecurrenceLines(vtodoBytes)` → use `originalBytes`.
    return obj;
}

QByteArray canonObjectToVtodoBytes(const QJsonObject& obj)
{
    if (obj.isEmpty())
        return {};
    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo);
    // >>> MOVE here the body of CanonToVTodoStage::transform from
    // >>> vtodocanonstages.cpp lines 320-544 (everything from the `// ---- uid`
    // >>> block through the recurrence-injection block), UNCHANGED. The function
    // >>> already reads `obj.value("uid")` etc. Keep the trailing
    // >>> `return icalBytes;`.
    QByteArray icalBytes;  // (placeholder — replaced by the moved body)
    return icalBytes;
}

}  // namespace Kalburator::Todo
```

> Implementer note: this is a mechanical cut. The moved promote body uses a local `obj` and the parsed `todo`; the moved demote body uses a local `obj` (the parameter) and builds `todo`. Do not rename variables — the names above match the originals so the moved code compiles unedited apart from the two promote edits called out.

- [ ] **Step 3: Re-wire the stages onto the helpers**

In `src/todo/vtodocanonstages.cpp`, add `#include "vtodocanonfields.h"` near the top. Replace `VTodoToCanonStage::transform` (lines 133-303) with:

```cpp
QByteArray VTodoToCanonStage::transform(const QByteArray& vtodoBytes) const
{
    if (vtodoBytes.isEmpty())
        return {};
    const auto todo = parseTodo(vtodoBytes);
    if (!todo)
        return {};
    QJsonObject obj = todoFieldsToCanon(todo, vtodoBytes);
    stampEnvelope(obj, QStringLiteral("todo"), todo->uid());
    return serialize(obj);
}
```

Replace `CanonToVTodoStage::transform` (lines 309-547) with:

```cpp
QByteArray CanonToVTodoStage::transform(const QByteArray& canonBytes) const
{
    if (canonBytes.isEmpty())
        return {};
    return canonObjectToVtodoBytes(parse(canonBytes));
}
```

The anonymous-namespace helpers in `vtodocanonstages.cpp` that are now ONLY used by these two thin wrappers are `parseTodo`, `serializeTodo` (no longer used here — moved), and `parse`/`serialize`/`stampEnvelope` `using`s. Keep `parseTodo` (used above) and the `using` lines for `parse`, `serialize`, `stampEnvelope`. **Delete** the now-unused `serializeTodo`, `dateTimeToJson`, `jsonToDateTime`, `statusToString`, `statusFromString`, `extractRecurrenceLines` from `vtodocanonstages.cpp` (they live in `vtodocanonfields.cpp` now) to avoid duplicate-symbol/unused warnings. Keep `canonToVtodoLoss()` where it is.

- [ ] **Step 4: Register the new files in CMake**

In `CMakeLists.txt`, in the lib source list, add next to line 365-378 (the todo block):

```cmake
    src/todo/vtodocanonfields.h
```
(in the headers section, near `src/todo/vtodocanonstages.h`) and
```cmake
    src/todo/vtodocanonfields.cpp
```
(in the sources section, near `src/todo/vtodocanonstages.cpp`).

- [ ] **Step 5: Build and run the regression test**

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --target tst_todo_canon_roundtrip -j 8
ctest --test-dir build -R tst_todo_canon_roundtrip -j 8 --output-on-failure
```
Expected: PASS (refactor is behaviour-preserving — same canon output, same `{todo,canon}` envelope).

- [ ] **Step 6: Commit**

```bash
git add src/todo/vtodocanonfields.h src/todo/vtodocanonfields.cpp src/todo/vtodocanonstages.cpp CMakeLists.txt
git commit -m "refactor(todo): extract VTODO field mapping into shared helpers"
```

---

### Task 3: Extract VEVENT field helpers (behaviour-preserving refactor)

Same pattern as Task 2, for events. `tst_calendar_canon_roundtrip` is the regression proof. The calendar stages stay VEVENT-only after this task — dispatch arrives in Task 5.

**Files:**
- Create: `src/calendar/eventcanonfields.h`
- Create: `src/calendar/eventcanonfields.cpp`
- Modify: `src/calendar/icalcanonstages.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/calendar/tst_calendar_canon_roundtrip.cpp` (existing — must stay green)

**Interfaces:**
- Produces (namespace `Kalburator::Calendar`):
  - `QJsonObject eventFieldsToCanon(const KCalendarCore::Event::Ptr& event, const QByteArray& originalBytes);`
  - `QByteArray canonObjectToEventBytes(const QJsonObject& obj);`

- [ ] **Step 1: Create the helper header**

Create `src/calendar/eventcanonfields.h`:

```cpp
#pragma once

#include <KCalendarCore/Event>
#include <QByteArray>
#include <QJsonObject>

namespace Kalburator::Calendar {

/// Map a parsed VEVENT to canon JSON fields (NO envelope). `originalBytes` is the
/// source iCal, used to extract RRULE/RDATE/EXDATE verbatim (invariant 3).
QJsonObject eventFieldsToCanon(const KCalendarCore::Event::Ptr& event,
                               const QByteArray& originalBytes);

/// Build full VEVENT iCal bytes from canon JSON (reads "uid" etc.).
QByteArray canonObjectToEventBytes(const QJsonObject& obj);

}  // namespace Kalburator::Calendar
```

- [ ] **Step 2: Create the helper .cpp by moving the existing bodies**

Create `src/calendar/eventcanonfields.cpp`. Move into its anonymous namespace, **verbatim**, the helper block from `src/calendar/icalcanonstages.cpp:15-169` (`parseEvent`, `serializeEvent`, `dateTimeToJson`, `jsonToDateTime`, `statusToString`, `statusFromString`, `partStatToString`, `partStatFromString`, `roleToString`, `roleFromString`, `extractRecurrenceLines`, and the four `using` lines 17-20). Then add the two public functions:

```cpp
#include "eventcanonfields.h"

#include "canonenvelope.h"

#include <KCalendarCore/Attendee>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Incidence>

#include <QJsonArray>
#include <QJsonDocument>
#include <QTimeZone>

namespace {
// >>> MOVE here, verbatim, icalcanonstages.cpp lines 15-169.
} // namespace

namespace Kalburator::Calendar {

QJsonObject eventFieldsToCanon(const KCalendarCore::Event::Ptr& event,
                               const QByteArray& originalBytes)
{
    QJsonObject obj;
    // >>> MOVE here the body of ICalToCanonStage::transform from
    // >>> icalcanonstages.cpp lines 186-435 (everything AFTER the
    // >>> `const auto event = parseEvent(...)` guard and BEFORE the
    // >>> `stampEnvelope(...)` / `return serialize(...)` lines), with one edit:
    // >>>   * `extractRecurrenceLines(icalBytes)` → `extractRecurrenceLines(originalBytes)`.
    // >>> Note: the local `const QString uid = event->uid();` (line 189) is only
    // >>> used by stampEnvelope in the original; DELETE it here.
    return obj;
}

QByteArray canonObjectToEventBytes(const QJsonObject& obj)
{
    if (obj.isEmpty())
        return {};
    KCalendarCore::Event::Ptr event(new KCalendarCore::Event);
    // >>> MOVE here the body of CanonToICalStage::transform from
    // >>> icalcanonstages.cpp lines 458-732 (from the `// ---- uid` block
    // >>> through the recurrence-injection block ending `return icalBytes;`),
    // >>> UNCHANGED.
    QByteArray icalBytes;  // (placeholder — replaced by the moved body)
    return icalBytes;
}

}  // namespace Kalburator::Calendar
```

- [ ] **Step 3: Re-wire the calendar stages onto the helpers (still VEVENT-only)**

In `src/calendar/icalcanonstages.cpp`, add `#include "eventcanonfields.h"`. Replace `ICalToCanonStage::transform` (lines 177-441) with:

```cpp
QByteArray ICalToCanonStage::transform(const QByteArray& icalBytes) const
{
    if (icalBytes.isEmpty())
        return {};
    const auto event = parseEvent(icalBytes);
    if (!event)
        return {};
    QJsonObject obj = eventFieldsToCanon(event, icalBytes);
    stampEnvelope(obj, QStringLiteral("calendar"), event->uid());
    return serialize(obj);
}
```

Replace `CanonToICalStage::transform` (lines 447-733) with:

```cpp
QByteArray CanonToICalStage::transform(const QByteArray& canonBytes) const
{
    if (canonBytes.isEmpty())
        return {};
    return canonObjectToEventBytes(parse(canonBytes));
}
```

Keep `parseEvent` (still used above) and the `using` lines for `parse`, `serialize`, `stampEnvelope`. Delete from `icalcanonstages.cpp` the helpers now living in `eventcanonfields.cpp` (`serializeEvent`, `dateTimeToJson`, `jsonToDateTime`, the status/partstat/role converters, `extractRecurrenceLines`). Keep `canonToIcalLoss()` where it is.

- [ ] **Step 4: Register the new files in CMake**

In `CMakeLists.txt`, near lines 162-210 (calendar block) add `src/calendar/eventcanonfields.h` to headers and `src/calendar/eventcanonfields.cpp` to sources.

- [ ] **Step 5: Build and run the regression test**

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --target tst_calendar_canon_roundtrip -j 8
ctest --test-dir build -R tst_calendar_canon_roundtrip -j 8 --output-on-failure
```
Expected: PASS (behaviour-preserving).

- [ ] **Step 6: Commit**

```bash
git add src/calendar/eventcanonfields.h src/calendar/eventcanonfields.cpp src/calendar/icalcanonstages.cpp CMakeLists.txt
git commit -m "refactor(calendar): extract VEVENT field mapping into shared helpers"
```

---

### Task 4: VJOURNAL field helpers (new) + loss profile

**Files:**
- Create: `src/calendar/journalcanonfields.h`
- Create: `src/calendar/journalcanonfields.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/calendar/tst_calendar_kind_dispatch.cpp` (created here; first slot)
- Modify: `tests/calendar/CMakeLists.txt`

**Interfaces:**
- Produces (namespace `Kalburator::Calendar`):
  - `QJsonObject journalFieldsToCanon(const KCalendarCore::Journal::Ptr& journal, const QByteArray& originalBytes);`
  - `QByteArray canonObjectToJournalBytes(const QJsonObject& obj);`
  - `Kalburator::Shape::LossProfile canonToVjournalLoss();`

- [ ] **Step 1: Write the failing test**

Create `tests/calendar/tst_calendar_kind_dispatch.cpp`:

```cpp
#include <QTest>
#include <QJsonObject>

#include "journalcanonfields.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Journal>

using Kalburator::Calendar::journalFieldsToCanon;
using Kalburator::Calendar::canonObjectToJournalBytes;

namespace {
KCalendarCore::Journal::Ptr parseJournal(const QByteArray &bytes)
{
    KCalendarCore::ICalFormat fmt;
    return fmt.fromString(QString::fromUtf8(bytes))
        .dynamicCast<KCalendarCore::Journal>();
}

const QByteArray kJournal =
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\n"
    "BEGIN:VJOURNAL\r\nUID:journal-1\r\nSUMMARY:Trip notes\r\n"
    "DESCRIPTION:Saw the sea\r\nDTSTART:20260601T090000Z\r\n"
    "CATEGORIES:Travel\r\nEND:VJOURNAL\r\nEND:VCALENDAR\r\n";
} // namespace

class TestCalendarKindDispatch : public QObject {
    Q_OBJECT
private slots:
    void vjournalFieldsRoundTrip()
    {
        const auto journal = parseJournal(kJournal);
        QVERIFY(journal);
        QJsonObject obj = journalFieldsToCanon(journal, kJournal);
        obj.insert(QStringLiteral("uid"), journal->uid());

        QCOMPARE(obj.value(QStringLiteral("summary")).toString(),
                 QStringLiteral("Trip notes"));
        QCOMPARE(obj.value(QStringLiteral("description")).toString(),
                 QStringLiteral("Saw the sea"));
        QVERIFY(obj.contains(QStringLiteral("start")));

        const QByteArray out = canonObjectToJournalBytes(obj);
        QVERIFY2(out.contains("VJOURNAL"), "must serialize back to a VJOURNAL");
        const auto outJournal = parseJournal(out);
        QVERIFY(outJournal);
        QCOMPARE(outJournal->summary(),     journal->summary());
        QCOMPARE(outJournal->description(),  journal->description());
        QCOMPARE(outJournal->dtStart().date(), journal->dtStart().date());
    }
};

QTEST_GUILESS_MAIN(TestCalendarKindDispatch)
#include "tst_calendar_kind_dispatch.moc"
```

Register it in `tests/calendar/CMakeLists.txt` after line 153 (`kalburator_add_calendar_test(tst_calendar_canon_roundtrip)`):

```cmake
kalburator_add_calendar_test(tst_calendar_kind_dispatch)
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --target tst_calendar_kind_dispatch -j 8
```
Expected: FAIL to compile — `journalcanonfields.h` does not exist.

- [ ] **Step 3: Create the header**

Create `src/calendar/journalcanonfields.h`:

```cpp
#pragma once

#include "lossprofile.h"

#include <KCalendarCore/Journal>
#include <QByteArray>
#include <QJsonObject>

namespace Kalburator::Calendar {

/// Map a parsed VJOURNAL to canon JSON fields (NO envelope). `originalBytes` is
/// the source iCal, used to preserve unmapped X- properties verbatim.
QJsonObject journalFieldsToCanon(const KCalendarCore::Journal::Ptr& journal,
                                 const QByteArray& originalBytes);

/// Build full VJOURNAL iCal bytes from canon JSON (reads "uid" etc.).
QByteArray canonObjectToJournalBytes(const QJsonObject& obj);

/// LossProfile for the canon → vjournal demote direction.
Kalburator::Shape::LossProfile canonToVjournalLoss();

}  // namespace Kalburator::Calendar
```

- [ ] **Step 4: Implement the .cpp**

Create `src/calendar/journalcanonfields.cpp`. VJOURNAL has: uid, summary, description, dtStart (no end), allDay, categories, status, classification, color, url, created, lastModified, sequence, and a providerExtras X- catch-all. Reuse the date/status string conventions from the event helpers by duplicating the two tiny converters locally (they are private to each translation unit; this is not the field mapping — it is shared scalar formatting):

```cpp
#include "journalcanonfields.h"

#include "canonenvelope.h"

#include <KCalendarCore/ICalFormat>

#include <QJsonArray>
#include <QTimeZone>

using Kalburator::Shape::CanonEnvelope::providerExtrasKey;

namespace {

QString journalStatusToString(KCalendarCore::Incidence::Status s)
{
    switch (s) {
    case KCalendarCore::Incidence::StatusDraft:      return QStringLiteral("draft");
    case KCalendarCore::Incidence::StatusFinal:      return QStringLiteral("final");
    case KCalendarCore::Incidence::StatusCanceled:   return QStringLiteral("cancelled");
    default:                                         return {};
    }
}

KCalendarCore::Incidence::Status journalStatusFromString(const QString &s)
{
    if (s == QStringLiteral("draft"))     return KCalendarCore::Incidence::StatusDraft;
    if (s == QStringLiteral("final"))     return KCalendarCore::Incidence::StatusFinal;
    if (s == QStringLiteral("cancelled")) return KCalendarCore::Incidence::StatusCanceled;
    return KCalendarCore::Incidence::StatusNone;
}

QString classToString(KCalendarCore::Incidence::Secrecy cls)
{
    switch (cls) {
    case KCalendarCore::Incidence::SecrecyPrivate:      return QStringLiteral("private");
    case KCalendarCore::Incidence::SecrecyConfidential: return QStringLiteral("confidential");
    default:                                            return QStringLiteral("public");
    }
}

} // namespace

namespace Kalburator::Calendar {

QJsonObject journalFieldsToCanon(const KCalendarCore::Journal::Ptr& journal,
                                 const QByteArray& /*originalBytes*/)
{
    QJsonObject obj;
    if (journal->created().isValid())
        obj.insert(QStringLiteral("created"),
                   journal->created().toUTC().toString(Qt::ISODate));
    if (journal->lastModified().isValid())
        obj.insert(QStringLiteral("lastModified"),
                   journal->lastModified().toUTC().toString(Qt::ISODate));
    if (journal->revision() > 0)
        obj.insert(QStringLiteral("sequence"), journal->revision());
    if (!journal->summary().isEmpty())
        obj.insert(QStringLiteral("summary"), journal->summary());
    if (!journal->description().isEmpty())
        obj.insert(QStringLiteral("description"), journal->description());
    if (journal->dtStart().isValid()) {
        QJsonObject start;
        const bool allDay = journal->allDay();
        if (allDay) {
            start.insert(QStringLiteral("date"),
                         journal->dtStart().date().toString(Qt::ISODate));
            start.insert(QStringLiteral("allDay"), true);
            obj.insert(QStringLiteral("allDay"), true);
        } else {
            start.insert(QStringLiteral("dateTime"),
                         journal->dtStart().toUTC().toString(Qt::ISODate));
            start.insert(QStringLiteral("floating"),
                         journal->dtStart().timeSpec() == Qt::LocalTime);
        }
        obj.insert(QStringLiteral("start"), start);
    }
    {
        const QString st = journalStatusToString(journal->status());
        if (!st.isEmpty())
            obj.insert(QStringLiteral("status"), st);
    }
    obj.insert(QStringLiteral("classification"), classToString(journal->secrecy()));
    if (!journal->color().isEmpty())
        obj.insert(QStringLiteral("color"), journal->color());
    if (journal->url().isValid())
        obj.insert(QStringLiteral("url"), journal->url().toString());
    if (!journal->categories().isEmpty()) {
        QJsonArray arr;
        for (const auto& c : journal->categories())
            arr.append(c);
        obj.insert(QStringLiteral("categories"), arr);
    }
    // providerExtras["x-ical"] — unmapped X- custom properties.
    const auto customProps = journal->customProperties();
    if (!customProps.isEmpty()) {
        QJsonObject xical;
        for (auto it = customProps.constBegin(); it != customProps.constEnd(); ++it)
            xical.insert(QString::fromLatin1(it.key()), it.value());
        if (!xical.isEmpty()) {
            QJsonObject extras;
            extras.insert(QStringLiteral("x-ical"), xical);
            obj.insert(providerExtrasKey(), extras);
        }
    }
    return obj;
}

QByteArray canonObjectToJournalBytes(const QJsonObject& obj)
{
    if (obj.isEmpty())
        return {};
    KCalendarCore::Journal::Ptr journal(new KCalendarCore::Journal);
    const QString uid = obj.value(QStringLiteral("uid")).toString();
    if (!uid.isEmpty())
        journal->setUid(uid);
    {
        const QString created = obj.value(QStringLiteral("created")).toString();
        if (!created.isEmpty())
            journal->setCreated(QDateTime::fromString(created, Qt::ISODate));
        const QString lastMod = obj.value(QStringLiteral("lastModified")).toString();
        if (!lastMod.isEmpty())
            journal->setLastModified(QDateTime::fromString(lastMod, Qt::ISODate));
    }
    if (const QJsonValue seq = obj.value(QStringLiteral("sequence")); !seq.isUndefined())
        journal->setRevision(seq.toInt());
    if (const QString s = obj.value(QStringLiteral("summary")).toString(); !s.isEmpty())
        journal->setSummary(s);
    if (const QString d = obj.value(QStringLiteral("description")).toString(); !d.isEmpty())
        journal->setDescription(d);
    {
        const QJsonObject start = obj.value(QStringLiteral("start")).toObject();
        const bool allDay = obj.value(QStringLiteral("allDay")).toBool();
        if (!start.isEmpty()) {
            if (start.contains(QStringLiteral("date"))) {
                const QDate dd = QDate::fromString(
                    start.value(QStringLiteral("date")).toString(), Qt::ISODate);
                if (dd.isValid()) {
                    journal->setDtStart(QDateTime(dd, QTime(0,0,0), QTimeZone::utc()));
                    journal->setAllDay(true);
                }
            } else {
                const QDateTime dt = QDateTime::fromString(
                    start.value(QStringLiteral("dateTime")).toString(), Qt::ISODate);
                if (dt.isValid()) {
                    journal->setDtStart(dt);
                    journal->setAllDay(allDay);
                }
            }
        }
    }
    if (const QString st = obj.value(QStringLiteral("status")).toString(); !st.isEmpty()) {
        const auto status = journalStatusFromString(st);
        if (status != KCalendarCore::Incidence::StatusNone)
            journal->setStatus(status);
    }
    {
        const QString cls = obj.value(QStringLiteral("classification")).toString();
        if (cls == QStringLiteral("private"))
            journal->setSecrecy(KCalendarCore::Incidence::SecrecyPrivate);
        else if (cls == QStringLiteral("confidential"))
            journal->setSecrecy(KCalendarCore::Incidence::SecrecyConfidential);
        else
            journal->setSecrecy(KCalendarCore::Incidence::SecrecyPublic);
    }
    if (const QString c = obj.value(QStringLiteral("color")).toString(); !c.isEmpty())
        journal->setColor(c);
    if (const QString u = obj.value(QStringLiteral("url")).toString(); !u.isEmpty())
        journal->setUrl(QUrl(u));
    {
        const QJsonArray cats = obj.value(QStringLiteral("categories")).toArray();
        if (!cats.isEmpty()) {
            QStringList catList;
            for (const auto& c : cats)
                catList << c.toString();
            journal->setCategories(catList);
        }
    }
    {
        const QJsonObject extras = obj.value(providerExtrasKey()).toObject();
        const QJsonObject xical  = extras.value(QStringLiteral("x-ical")).toObject();
        for (auto it = xical.constBegin(); it != xical.constEnd(); ++it)
            journal->setNonKDECustomProperty(it.key().toLatin1(), it.value().toString());
    }
    KCalendarCore::ICalFormat fmt;
    return fmt.toICalString(journal).toUtf8();
}

Kalburator::Shape::LossProfile canonToVjournalLoss()
{
    // VJOURNAL maps its full field-set; no non-reversible loss to declare.
    return Kalburator::Shape::LossProfile{};
}

}  // namespace Kalburator::Calendar
```

Register the files in `CMakeLists.txt` (calendar block, near lines 162-210): add `src/calendar/journalcanonfields.h` and `src/calendar/journalcanonfields.cpp`.

- [ ] **Step 5: Run to verify it passes**

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --target tst_calendar_kind_dispatch -j 8
ctest --test-dir build -R tst_calendar_kind_dispatch -j 8 --output-on-failure
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/calendar/journalcanonfields.h src/calendar/journalcanonfields.cpp CMakeLists.txt tests/calendar/tst_calendar_kind_dispatch.cpp tests/calendar/CMakeLists.txt
git commit -m "feat(calendar): first-class VJOURNAL canon field mapping"
```

---

### Task 5: Calendar stages dispatch on component kind

This is the core fix and the handoff §6 RED→GREEN. The two calendar stages stop assuming VEVENT and route per kind, reusing Task 2/3/4 helpers.

**Files:**
- Modify: `src/calendar/icalcanonstages.cpp`
- Test: `tests/calendar/tst_calendar_kind_dispatch.cpp` (add slots)

**Interfaces:**
- Consumes: `eventFieldsToCanon` / `canonObjectToEventBytes` (Task 3), `Todo::todoFieldsToCanon` / `Todo::canonObjectToVtodoBytes` (Task 2), `journalFieldsToCanon` / `canonObjectToJournalBytes` (Task 4), `CanonEnvelope::kind` / 4-arg `stampEnvelope` (Task 1).

- [ ] **Step 1: Write the failing tests**

Add to `tests/calendar/tst_calendar_kind_dispatch.cpp`: the include block at top gains the pipeline machinery (mirror `tst_calendar_canon_roundtrip.cpp`'s `makeCalendarRegistries`). Add these includes after the existing ones:

```cpp
#include "canonenvelope.h"
#include "icalcanonstages.h"
#include "calendardomaindefinition.h"
#include "calendarstockshapes.h"
#include "shaperegistries.h"
```

Add the `makeCalendarRegistries()` helper (copy verbatim from `tst_calendar_canon_roundtrip.cpp` lines 29-54) into the anonymous namespace, plus these constants:

```cpp
const QByteArray kVtodo =
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\n"
    "BEGIN:VTODO\r\nUID:todo-handoff-1\r\nSUMMARY:Buy milk\r\n"
    "STATUS:NEEDS-ACTION\r\nPERCENT-COMPLETE:0\r\nEND:VTODO\r\nEND:VCALENDAR\r\n";
```

Add these slots (the handoff §6 test + analogues + the defensive-empty unit tests):

```cpp
    void vtodoSurvivesIcalCanonRoundTrip()   // handoff §6
    {
        using Kalburator::Shape::DomainId;
        using Kalburator::Shape::EncodingId;
        using Kalburator::Shape::Shape;
        const auto regs = makeCalendarRegistries();
        const Shape ical { DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("ical")} };
        const Shape canon{ DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("canon")} };

        const auto toCanon = regs.transformation.compile(ical, canon);
        const auto toIcal  = regs.transformation.compile(canon, ical);
        QVERIFY(toCanon.has_value() && toIcal.has_value());

        const QByteArray canonBytes = toCanon->apply(kVtodo);
        QVERIFY2(!canonBytes.isEmpty(),
                 "VTODO must promote to non-empty canon");
        QCOMPARE(Kalburator::Shape::CanonEnvelope::kind(
                     Kalburator::Shape::CanonEnvelope::parse(canonBytes)),
                 QStringLiteral("vtodo"));

        const QByteArray rt = toIcal->apply(canonBytes);
        QVERIFY2(rt.contains("VTODO"), "VTODO must survive ical->canon->ical");
        QVERIFY2(rt.contains("UID:todo-handoff-1"), "VTODO uid must survive");
        QVERIFY2(rt.contains("Buy milk"), "VTODO summary must survive");
    }

    void vjournalSurvivesIcalCanonRoundTrip()
    {
        using Kalburator::Shape::DomainId;
        using Kalburator::Shape::EncodingId;
        using Kalburator::Shape::Shape;
        const auto regs = makeCalendarRegistries();
        const Shape ical { DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("ical")} };
        const Shape canon{ DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("canon")} };
        const auto toCanon = regs.transformation.compile(ical, canon);
        const auto toIcal  = regs.transformation.compile(canon, ical);
        QVERIFY(toCanon.has_value() && toIcal.has_value());

        const QByteArray canonBytes = toCanon->apply(kJournal);
        QVERIFY2(!canonBytes.isEmpty(), "VJOURNAL must promote to non-empty canon");
        QCOMPARE(Kalburator::Shape::CanonEnvelope::kind(
                     Kalburator::Shape::CanonEnvelope::parse(canonBytes)),
                 QStringLiteral("vjournal"));
        const QByteArray rt = toIcal->apply(canonBytes);
        QVERIFY2(rt.contains("VJOURNAL"), "VJOURNAL must survive round trip");
        QVERIFY2(rt.contains("UID:journal-1"), "VJOURNAL uid must survive");
    }

    void veventStillRoundTrips()   // regression guard inside the dispatch test
    {
        using Kalburator::Calendar::ICalToCanonStage;
        using Kalburator::Calendar::CanonToICalStage;
        const QByteArray vevent =
            "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//T//EN\r\n"
            "BEGIN:VEVENT\r\nUID:e-1\r\nSUMMARY:Sync\r\n"
            "DTSTART:20260601T090000Z\r\nDTEND:20260601T100000Z\r\n"
            "END:VEVENT\r\nEND:VCALENDAR\r\n";
        ICalToCanonStage fwd; CanonToICalStage rev;
        const QByteArray canon = fwd.transform(vevent);
        QVERIFY(!canon.isEmpty());
        // Absent or "vevent" kind both mean event.
        const QByteArray out = rev.transform(canon);
        QVERIFY2(out.contains("VEVENT"), "VEVENT must still round-trip");
        QVERIFY2(out.contains("UID:e-1"), "VEVENT uid must survive");
    }

    void unknownKindDemotesToEmpty()   // defensive branch (feeds Task 7 guard)
    {
        using Kalburator::Calendar::CanonToICalStage;
        QJsonObject obj;
        obj.insert(QStringLiteral("uid"), QStringLiteral("x-1"));
        QJsonObject canonMeta;
        canonMeta.insert(QStringLiteral("domain"), QStringLiteral("calendar"));
        canonMeta.insert(QStringLiteral("kind"),   QStringLiteral("vfreebusy"));
        obj.insert(QStringLiteral("_canon"), canonMeta);
        const QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        CanonToICalStage rev;
        QVERIFY2(rev.transform(bytes).isEmpty(),
                 "unknown kind must demote to empty (guarded loudly by the engine)");
    }
```

Add `#include <QJsonDocument>` at the top of the test file.

- [ ] **Step 2: Run to verify the new slots fail**

```bash
cmake --build build --target tst_calendar_kind_dispatch -j 8
./build/tests/calendar/tst_calendar_kind_dispatch vtodoSurvivesIcalCanonRoundTrip
```
Expected: FAIL — `'!canonBytes.isEmpty()' returned FALSE` (VTODO → empty canon, the bug).

- [ ] **Step 3: Make `ICalToCanonStage` dispatch on kind (promote)**

In `src/calendar/icalcanonstages.cpp`, add includes:

```cpp
#include "eventcanonfields.h"      // already added in Task 3
#include "journalcanonfields.h"
#include "vtodocanonfields.h"
#include <KCalendarCore/Journal>
#include <KCalendarCore/Todo>
```

Replace the parse helper and the `ICalToCanonStage::transform` from Task 3 with a kind-dispatching version. First, change the top-level `parseEvent` usage: add a generic incidence parser in the anonymous namespace:

```cpp
KCalendarCore::Incidence::Ptr parseIncidence(const QByteArray &data)
{
    if (data.isEmpty())
        return {};
    KCalendarCore::ICalFormat fmt;
    return fmt.fromString(QString::fromUtf8(data));
}
```

Then:

```cpp
QByteArray ICalToCanonStage::transform(const QByteArray& icalBytes) const
{
    if (icalBytes.isEmpty())
        return {};
    const auto inc = parseIncidence(icalBytes);
    if (!inc)
        return {};

    QJsonObject obj;
    QString kind;
    if (auto ev = inc.dynamicCast<KCalendarCore::Event>()) {
        obj  = eventFieldsToCanon(ev, icalBytes);
        kind = QStringLiteral("vevent");
    } else if (auto td = inc.dynamicCast<KCalendarCore::Todo>()) {
        obj  = Kalburator::Todo::todoFieldsToCanon(td, icalBytes);
        kind = QStringLiteral("vtodo");
    } else if (auto jr = inc.dynamicCast<KCalendarCore::Journal>()) {
        obj  = journalFieldsToCanon(jr, icalBytes);
        kind = QStringLiteral("vjournal");
    } else {
        return {};   // unknown component kind — guarded loudly by the engine.
    }
    // vevent kind is the default; omit it so existing v1 baselines stay byte-stable.
    stampEnvelope(obj, QStringLiteral("calendar"), inc->uid(),
                  kind == QStringLiteral("vevent") ? QString() : kind);
    return serialize(obj);
}
```

> Note: `eventFieldsToCanon` consumes an `Event::Ptr`; pass `ev`. Keep `parseEvent` only if still referenced elsewhere — otherwise delete it in favour of `parseIncidence`.

- [ ] **Step 4: Make `CanonToICalStage` dispatch on kind (demote)**

```cpp
QByteArray CanonToICalStage::transform(const QByteArray& canonBytes) const
{
    if (canonBytes.isEmpty())
        return {};
    const QJsonObject obj = parse(canonBytes);
    if (obj.isEmpty())
        return {};
    const QString kind = Kalburator::Shape::CanonEnvelope::kind(obj);
    if (kind == QStringLiteral("vtodo"))
        return Kalburator::Todo::canonObjectToVtodoBytes(obj);
    if (kind == QStringLiteral("vjournal"))
        return canonObjectToJournalBytes(obj);
    if (kind.isEmpty() || kind == QStringLiteral("vevent"))
        return canonObjectToEventBytes(obj);   // absent kind ⇒ vevent (back-compat)
    return {};   // unknown kind — guarded loudly by the engine.
}
```

Add `using Kalburator::Shape::CanonEnvelope::kind;` is NOT needed — call fully-qualified to avoid clashing with the local `kind` variable in the promote function.

- [ ] **Step 5: Run to verify all dispatch slots pass**

```bash
cmake --build build --target tst_calendar_kind_dispatch -j 8
ctest --test-dir build -R tst_calendar_kind_dispatch -j 8 --output-on-failure
```
Expected: PASS (all five slots). Also re-run the event regression:

```bash
ctest --test-dir build -R "tst_calendar_canon_roundtrip|tst_orgical_canon_roundtrip|tst_todo_canon_roundtrip" -j 8 --output-on-failure
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/calendar/icalcanonstages.cpp tests/calendar/tst_calendar_kind_dispatch.cpp
git commit -m "feat(calendar): dispatch ical<->canon stages on component kind (VTODO/VJOURNAL)"
```

---

### Task 6: Union the calendar canon property catalogue across kinds

So `CanonJsonDiffer` detects changes to todo/journal-specific fields (handoff "correct counts" criterion).

**Files:**
- Modify: `src/calendar/calendarcanonproperties.cpp:79` (before `return cat;`)
- Test: `tests/calendar/tst_calendar_kind_dispatch.cpp` (add a slot)

- [ ] **Step 1: Write the failing test**

Add to `tst_calendar_kind_dispatch.cpp`:

```cpp
    void catalogueIncludesTodoAndJournalFields()
    {
        const auto ids = Kalburator::Calendar::calendarCanonPropertyIds();
        const auto has = [&](const char* k){
            return ids.contains(Kalburator::Shape::PropertyId{QString::fromLatin1(k)});
        };
        QVERIFY2(has("due"),             "catalogue must include todo 'due'");
        QVERIFY2(has("completed"),       "catalogue must include todo 'completed'");
        QVERIFY2(has("percentComplete"), "catalogue must include todo 'percentComplete'");
        QVERIFY2(has("relatedTo"),       "catalogue must include todo 'relatedTo'");
    }
```

Add `#include "calendarcanonproperties.h"` to the test file.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --target tst_calendar_kind_dispatch -j 8
./build/tests/calendar/tst_calendar_kind_dispatch catalogueIncludesTodoAndJournalFields
```
Expected: FAIL — `due` not in catalogue.

- [ ] **Step 3: Add the union fields**

In `src/calendar/calendarcanonproperties.cpp`, immediately before `return cat;` (line 79) add:

```cpp
    // --- Union across iCalendar component kinds (VTODO / VJOURNAL) ---
    // The {calendar,canon} shape carries any of VEVENT/VTODO/VJOURNAL (kind-
    // tagged in the envelope). These fields are absent on events but must be
    // catalogued so CanonJsonDiffer detects changes to todo/journal records.
    cat.addProperty({ PropertyId{"due"},             PropertyKind::Json,    QStringLiteral("Due") });
    cat.addProperty({ PropertyId{"completed"},       PropertyKind::DateTime, QStringLiteral("Completed") });
    cat.addProperty({ PropertyId{"percentComplete"}, PropertyKind::Integer, QStringLiteral("Percent Complete") });
    cat.addProperty({ PropertyId{"relatedTo"},       PropertyKind::Json,    QStringLiteral("Related To") });
    cat.addProperty({ PropertyId{"geo"},             PropertyKind::Json,    QStringLiteral("Geo") });
```

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build build --target tst_calendar_kind_dispatch -j 8
ctest --test-dir build -R tst_calendar_kind_dispatch -j 8 --output-on-failure
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/calendar/calendarcanonproperties.cpp tests/calendar/tst_calendar_kind_dispatch.cpp
git commit -m "feat(calendar): union canon property catalogue across VTODO/VJOURNAL"
```

---

### Task 7: Fail-loud guard — a record may never transcode to empty

**Files:**
- Create: `src/engine/transcodeguard.h`
- Modify: `src/engine/syncengine.cpp` (promote site ~2069; demote sites 2713-2724 and 2735-2746)
- Create: `tests/engine/tst_transcode_guard.cpp`
- Modify: `tests/engine/CMakeLists.txt`

**Interfaces:**
- Produces: `bool Kalburator::Sync::transcodeEmptiedRecord(const QByteArray& before, const QByteArray& after);`

- [ ] **Step 1: Write the failing test**

Create `tests/engine/tst_transcode_guard.cpp`:

```cpp
#include <QTest>
#include "transcodeguard.h"

using Kalburator::Sync::transcodeEmptiedRecord;

class TestTranscodeGuard : public QObject {
    Q_OBJECT
private slots:
    void nonEmptyToEmptyIsFailure()
    {
        QVERIFY(transcodeEmptiedRecord(QByteArray("BEGIN:VCALENDAR"), QByteArray()));
    }
    void emptyInputIsNotFailure()
    {
        QVERIFY(!transcodeEmptiedRecord(QByteArray(), QByteArray()));
    }
    void nonEmptyToNonEmptyIsNotFailure()
    {
        QVERIFY(!transcodeEmptiedRecord(QByteArray("a"), QByteArray("b")));
    }
};

QTEST_GUILESS_MAIN(TestTranscodeGuard)
#include "tst_transcode_guard.moc"
```

Find how `tests/engine/CMakeLists.txt` registers tests (it uses a helper analogous to `kalburator_add_calendar_test`). Register the new test with that helper, e.g.:

```cmake
kalburator_add_engine_test(tst_transcode_guard)
```

> If `tests/engine/CMakeLists.txt` does not exist or uses a different helper name, register it under `tests/calendar/CMakeLists.txt` with `kalburator_add_calendar_test(tst_transcode_guard)` and place the file under `tests/calendar/` instead — either location links `Kalburator::Sync`.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --target tst_transcode_guard -j 8
```
Expected: FAIL to compile — `transcodeguard.h` does not exist.

- [ ] **Step 3: Create the predicate header**

Create `src/engine/transcodeguard.h`:

```cpp
#pragma once

#include <QByteArray>

namespace Kalburator::Sync {

/// True when a non-empty record transcoded to empty bytes — the silent
/// data-loss signature. A legitimate transform never empties a record
/// (deletes are handled out-of-band), so callers must fail the mapping
/// loudly when this returns true (handoff req #6).
inline bool transcodeEmptiedRecord(const QByteArray& before, const QByteArray& after)
{
    return !before.isEmpty() && after.isEmpty();
}

}  // namespace Kalburator::Sync
```

- [ ] **Step 4: Run to verify the predicate test passes**

```bash
cmake --build build --target tst_transcode_guard -j 8
ctest --test-dir build -R tst_transcode_guard -j 8 --output-on-failure
```
Expected: PASS.

- [ ] **Step 5: Wire the guard into the engine — promote site**

In `src/engine/syncengine.cpp`, add `#include "transcodeguard.h"` with the other includes. At the promote loop (currently lines 2069-2073):

```cpp
    if (!srcToCanon->isIdentity()) {
        for (auto &r : sourceRecords) {
            r.data = srcToCanon->apply(r.data);
        }
    }
```

replace with:

```cpp
    if (!srcToCanon->isIdentity()) {
        for (auto &r : sourceRecords) {
            const QByteArray before = r.data;
            r.data = srcToCanon->apply(r.data);
            if (transcodeEmptiedRecord(before, r.data)) {
                m_currentResult.success = false;
                m_currentResult.errorMessage = QStringLiteral(
                    "transcode produced empty bytes for record '%1' "
                    "promoting %2/%3 -> canonical (unhandled component kind?)")
                        .arg(r.id, srcShape.domain.toString(), srcShape.encoding.toString());
                m_currentResult.endTime = QDateTime::currentDateTime();
                emit syncCompleted(mappingId, m_currentResult);
                return true;
            }
        }
    }
```

- [ ] **Step 6: Wire the guard into the engine — demote sites**

At the target demote loop (lines 2713-2724), inside the `if (!rec.isDeleted)` block, after `rec.data = canonToTgt->apply(rec.data);` (line 2721) add:

```cpp
                    if (transcodeEmptiedRecord(/*before=*/QByteArray(), rec.data)) {
                        // guard placeholder — see correct form below
                    }
```

> Correct form: capture the pre-apply bytes. Replace lines 2718-2721 with:

```cpp
                    const QStringList lost = materializedLoss(*canonToTgt, rec.data);
                    if (!lost.isEmpty())
                        emit transcodingWarning(tgtColId, rec.id, { lost.join(QStringLiteral(", ")) });
                    const QByteArray before = rec.data;
                    rec.data = canonToTgt->apply(rec.data);
                    if (transcodeEmptiedRecord(before, rec.data)) {
                        writeFailed = true;
                        writeError = QStringLiteral(
                            "transcode produced empty bytes for record '%1' "
                            "demoting canonical -> %2/%3 (unhandled component kind?)")
                                .arg(rec.id, tgtShape.domain.toString(), tgtShape.encoding.toString());
                    }
```

Apply the symmetric change at the source demote loop (lines 2740-2743), using `canonToSrc`, `srcColId`, and `srcShape`:

```cpp
                    const QStringList lost = materializedLoss(*canonToSrc, rec.data);
                    if (!lost.isEmpty())
                        emit transcodingWarning(srcColId, rec.id, { lost.join(QStringLiteral(", ")) });
                    const QByteArray before = rec.data;
                    rec.data = canonToSrc->apply(rec.data);
                    if (transcodeEmptiedRecord(before, rec.data)) {
                        writeFailed = true;
                        writeError = QStringLiteral(
                            "transcode produced empty bytes for record '%1' "
                            "demoting canonical -> %2/%3 (unhandled component kind?)")
                                .arg(rec.id, srcShape.domain.toString(), srcShape.encoding.toString());
                    }
```

> Verify `tgtShape` / `srcShape` are in scope at lines 2710-2746 (they are computed at the top of `dispatchSync`, line 1873-1874, but `unifiedContinueAfterConflicts` is a separate method — confirm via the method signature; if not in scope, recompute them there with `tgtBackend->shapeFor(tgtColId)` / `srcBackend->shapeFor(srcColId)`, which are already available locals in that method).

- [ ] **Step 7: Build and run the full guard + dispatch suite**

```bash
cmake --build build -j 8
ctest --test-dir build -R "tst_transcode_guard|tst_calendar_kind_dispatch" -j 8 --output-on-failure
```
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add src/engine/transcodeguard.h src/engine/syncengine.cpp tests/engine/tst_transcode_guard.cpp tests/engine/CMakeLists.txt
git commit -m "feat(engine): fail mapping loudly when a record transcodes to empty"
```

---

### Task 8: Engine-level hybrid reconcile (acceptance test)

A one-way upload of a calendar holding BOTH a VEVENT and a VTODO propagates both to the target — the end-to-end proof that the bug (todos demoting to empty, `+0`) is gone.

**Files:**
- Create: `tests/calendar/tst_calendar_hybrid_reconcile.cpp`
- Modify: `tests/calendar/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/calendar/tst_calendar_hybrid_reconcile.cpp` modelled on `tst_calendar_sync_oneway.cpp` (copy its includes, the `init()`/`cleanup()`/`runOneSync()` harness, and the `makeOneWayUploadMapping()` helper verbatim — they are reusable). Replace the test slots with:

```cpp
    void hybridCalendarPropagatesEventsAndTodos()
    {
        // Source holds one VEVENT and one VTODO in the same calendar.
        auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
        event->setUid(QStringLiteral("evt-1"));
        event->setSummary(QStringLiteral("Standup"));
        event->setDtStart(QDateTime::currentDateTimeUtc());
        m_source->addIncidence(QString::fromLatin1(kCalendarId), event);

        auto todo = KCalendarCore::Todo::Ptr(new KCalendarCore::Todo());
        todo->setUid(QStringLiteral("todo-1"));
        todo->setSummary(QStringLiteral("Buy milk"));
        todo->setStatus(KCalendarCore::Incidence::StatusNeedsAction);
        m_source->addIncidence(QString::fromLatin1(kCalendarId), todo);

        QCOMPARE(sourceUids().size(), 2);
        QVERIFY(targetUids().isEmpty());

        QVERIFY(runOneSync());

        // BOTH must land on the target — the VTODO must not vanish (the bug).
        QCOMPARE(targetUids().size(), 2);
        QVERIFY2(targetUids().contains(QStringLiteral("evt-1")),  "event must propagate");
        QVERIFY2(targetUids().contains(QStringLiteral("todo-1")), "todo must propagate (was lost)");
    }
```

Add `#include <KCalendarCore/Todo>` to the includes. Register in `tests/calendar/CMakeLists.txt`:

```cmake
kalburator_add_calendar_test(tst_calendar_hybrid_reconcile)
```

> `m_target->allUids()` returns the uids the target calendar holds after sync. With the pre-fix bug the VTODO demotes to empty bytes and is never written (or fails to parse), so `todo-1` is absent — this test is RED on v0.79 logic and GREEN after Task 5. (If the Task 7 guard is active and the todo path were still broken, the sync would instead FAIL loudly — also caught by `runOneSync()` returning false. After Task 5 the todo path works, so the sync succeeds and both uids appear.)

- [ ] **Step 2: Run to verify it passes (with Tasks 1-7 applied)**

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --target tst_calendar_hybrid_reconcile -j 8
ctest --test-dir build -R tst_calendar_hybrid_reconcile -j 8 --output-on-failure
```
Expected: PASS. (To witness RED, temporarily stash Task 5's `icalcanonstages.cpp` changes — optional sanity check.)

- [ ] **Step 3: Commit**

```bash
git add tests/calendar/tst_calendar_hybrid_reconcile.cpp tests/calendar/CMakeLists.txt
git commit -m "test(calendar): engine-level hybrid event+todo reconcile (acceptance)"
```

---

### Task 9: Full-suite verification, docs, version bump

**Files:**
- Modify: `docs/2026-06-28-calendar-vtodo-vjournal-shape-dispatch-handoff.md` (resolution note) OR add `docs/2026-06-28-...-resolution.md`
- Modify: CLAUDE.md / phase status doc if one tracks the canon work
- Tag: `v0.80`

- [ ] **Step 1: Build everything and run the full suite**

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j 8
ctest --test-dir build -j 8 --output-on-failure
```
Expected: all tests PASS. Count must be ≥ 151 + the new tests (`tst_calendar_kind_dispatch`, `tst_calendar_hybrid_reconcile`, `tst_transcode_guard`). No regressions in `tst_calendar_canon_roundtrip`, `tst_orgical_canon_roundtrip`, `tst_todo_canon_roundtrip`, `tst_contacts_canon_roundtrip`.

- [ ] **Step 2: Append a resolution note to the handoff doc**

Add a `## 9. Resolution (2026-06-28, v0.80)` section to `docs/2026-06-28-calendar-vtodo-vjournal-shape-dispatch-handoff.md` summarizing: kind discriminator in the envelope; kind-dispatching calendar stages reusing shared VTODO helpers; first-class VJOURNAL; property-catalogue union; fail-loud guard; the §6 test now GREEN; and the acceptance reconcile test. State that no engine dispatch / no PlanStan change / no baseline migration was needed.

- [ ] **Step 3: Commit docs**

```bash
git add docs/2026-06-28-calendar-vtodo-vjournal-shape-dispatch-handoff.md
git commit -m "docs(handoff): resolve VTODO/VJOURNAL canon dispatch (v0.80)"
```

- [ ] **Step 4: Run the PlanStan pretest gate (per memory: PlanStan builds libkalburator)**

Coordinate per the project's PlanStan gate practice before tagging. If green, tag:

```bash
git tag v0.80
```

- [ ] **Step 5: Merge to main and push**

Use `superpowers:finishing-a-development-branch` to merge `feature/calendar-per-kind-canon-dispatch` to `main` and push (and the tag). PlanStan then bumps its pin from v0.79 to v0.80 and re-verifies end-to-end against the live Nextcloud account.

---

## Self-Review

**Spec coverage:**
- §3.1 envelope `kind` → Task 1. ✓
- §4 dispatcher stages → Task 5. ✓
- §5 reuse via extracted helpers → Tasks 2 (vtodo), 3 (event), wired in 5. ✓
- §6 first-class VJOURNAL + loss profile → Task 4. ✓
- §7 property catalogue union → Task 6. ✓
- §8 fail-loud guard at promote + 2 demote sites → Task 7. ✓
- §9 tests (vtodo §6, vjournal, event/todo/orgical regression, hybrid reconcile, guard) → Tasks 4,5,6,7,8 + Task 9 full suite. ✓
- §10 non-goals (no engine dispatch, no domain fold, no version bump, org-ical untouched) → respected; org-ical edges never modified. ✓

**Placeholder scan:** The `>>> MOVE here` markers in Tasks 2 and 3 are explicit mechanical-extraction instructions with exact source line ranges and the precise edits to make — not vague placeholders. All genuinely new code (Tasks 1, 4, 5, 6, 7) is shown in full. The Task 7 Step 6 "guard placeholder" is immediately followed by the "Correct form" block; the implementer writes the correct form.

**Type consistency:** Helper names are consistent across tasks: `todoFieldsToCanon`/`canonObjectToVtodoBytes` (Task 2, used in Task 5), `eventFieldsToCanon`/`canonObjectToEventBytes` (Task 3, used in Task 5), `journalFieldsToCanon`/`canonObjectToJournalBytes`/`canonToVjournalLoss` (Task 4, used in Task 5), `CanonEnvelope::kind`/`kindKey`/4-arg `stampEnvelope` (Task 1, used in Tasks 2,3,5), `transcodeEmptiedRecord` (Task 7). Kind strings `vevent`/`vtodo`/`vjournal` are uniform across stages and tests.

**Open verification flagged for implementer:** Task 7 Step 6 notes that `srcShape`/`tgtShape` scope in `unifiedContinueAfterConflicts` must be confirmed (recompute from `shapeFor` if needed). Task 7 Step 1 notes the engine-test CMake helper name must be confirmed (fallback: register under `tests/calendar/`).
