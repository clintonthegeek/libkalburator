# Akonadi Full Functionality Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Akonadi calendar and contacts backends fully functional as a PlanStan/WildPalms sync target — real create/update/delete, collection creation, and CalDAV-parity change detection — against a live Akonadi daemon.

**Architecture:** Relocate the real Akonadi item-write logic onto the per-record `IBlobBackend` ops the engine actually calls (`createRecord`/`updateRecord`/`deleteRecord`), bridging the synchronous blob contract to async Akonadi jobs with `KJob::exec()`. Add `Backend::ChangeDetection` via a payload-free id+revision digest persisted in a small revision store, and a `ChangeRecorder` warm-path layered strictly on top of that digest backbone.

**Tech Stack:** C++17, Qt6, KPim6 Akonadi (`KPim6::AkonadiCore`), KCalendarCore, KContacts, QTest. All work compiles only under `KALBURATOR_HAVE_AKONADI=ON` (default OFF).

**Design doc:** `docs/2026-05-26-akonadi-full-functionality-design.md`

---

## Conventions for every task

- **Build:** `cmake -S . -B build-akonadi -DKALBURATOR_HAVE_AKONADI=ON -DCMAKE_MODULE_PATH=/usr/share/ECM/modules -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` then `cmake --build build-akonadi`.
- **Live tests** require a running Akonadi daemon AND the env gate. Every live test begins with:
  ```cpp
  void init() {
      if (qEnvironmentVariableIsEmpty("KALBURATOR_AKONADI_LIVE_TEST"))
          QSKIP("set KALBURATOR_AKONADI_LIVE_TEST=1 and run a local Akonadi to exercise this");
      if (!Akonadi::ServerManager::isRunning())
          QSKIP("Akonadi server not running");
  }
  ```
- **Pure-logic tests** (revision digest, payload (de)serialization, revision store) need no daemon and run in CI.
- Commit after each task with the message shown in its final step.

---

## File structure

**New files:**
- `src/sync/akonadirevisionstore.h` / `.cpp` — persistent `collectionId → token` store (CTagStore analogue), shared by both backends.
- `src/sync/akonadirevisiondigest.h` / `.cpp` — pure `computeRevisionDigest()` helper.
- `tests/sync/tst_akonadirevisionstore.cpp` — pure unit test.
- `tests/sync/tst_akonadirevisiondigest.cpp` — pure unit test.
- `tests/calendar/tst_akonadibackend_live.cpp` — env-gated calendar integration test.
- `tests/contacts/tst_akonadicontactsbackend_live.cpp` — env-gated contacts integration test.

**Modified files:**
- `src/calendar/akonadibackend.h` / `.cpp` — id fix, write ops, createCollection, ChangeDetection, hash memoization, ChangeRecorder.
- `src/contacts/akonadicontactsbackend.h` / `.cpp` — same set, mirrored for vCard.
- `CMakeLists.txt` — register new source files + tests under `KALBURATOR_HAVE_AKONADI`.
- `docs/phase0/04y-phase-l-status.md`, `docs/phase0/04w-deferred-work.md`, `docs/campaign/FINDINGS.md` — doc-honesty corrections.

---

## Task 1: Fix cross-backend record identity (calendar)

`loadRecords` must key `BackendRecord.id` by the iCal UID (matching `RemoteCalendarBackend::loadRecords` `rec.id = uid`), not the Akonadi item id, or the engine cannot match records across backends.

**Files:**
- Modify: `src/calendar/akonadibackend.cpp:888` (the `rec.id` assignment in `loadRecords`)

- [ ] **Step 1: Change the id assignment**

In `AkonadiBackend::loadRecords`, replace:
```cpp
        rec.id          = QString::number(aItem.id());
```
with:
```cpp
        // BackendRecord.id is the cross-backend-stable domain UID (the iCal
        // UID), matching RemoteCalendarBackend. The Akonadi item id is local
        // only and would never match a peer backend's records. The (uid ->
        // Akonadi::Item) cache (m_itemsByCalendar) resolves the local item for
        // write jobs.
        rec.id          = incidence->uid();
```

- [ ] **Step 2: Build**

Run: `cmake --build build-akonadi --target kalburator`
Expected: compiles clean.

- [ ] **Step 3: Commit**

```bash
git add src/calendar/akonadibackend.cpp
git commit -m "fix(akonadi): use iCal UID as BackendRecord.id for cross-backend matching"
```

---

## Task 2: iCal payload (de)serialization helper (calendar, pure unit-testable)

Add `incidenceFromRecord()` — the inverse of what `loadRecords` does — so the write ops can turn `BackendRecord.data` bytes back into a `KCalendarCore::Incidence`.

**Files:**
- Modify: `src/calendar/akonadibackend.h` (declare private helper)
- Modify: `src/calendar/akonadibackend.cpp` (implement)
- Test: `tests/calendar/tst_akonadi_payload.cpp` (new, pure)

- [ ] **Step 1: Write the failing test**

Create `tests/calendar/tst_akonadi_payload.cpp`:
```cpp
#include <QtTest>
#include "akonadibackend.h"
#include <KCalendarCore/Event>

using namespace Kalburator::Sync;

class TestAkonadiPayload : public QObject {
    Q_OBJECT
private slots:
    void roundTripsUid();
};

void TestAkonadiPayload::roundTripsUid() {
    const QByteArray ical =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\n"
        "UID:test-uid-123\r\nSUMMARY:Hello\r\n"
        "DTSTART:20260101T120000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    BackendRecord rec;
    rec.id = "test-uid-123";
    rec.data = ical;

    AkonadiBackend backend;
    auto incidence = backend.incidenceFromRecordForTest(rec);
    QVERIFY(incidence);
    QCOMPARE(incidence->uid(), QStringLiteral("test-uid-123"));
    QCOMPARE(incidence->summary(), QStringLiteral("Hello"));
}

QTEST_MAIN(TestAkonadiPayload)
#include "tst_akonadi_payload.moc"
```

- [ ] **Step 2: Declare the helper**

In `src/calendar/akonadibackend.h`, in the `private:` section after `extractIncidence`:
```cpp
    /// Inverse of loadRecords serialization: parse BackendRecord.data
    /// (iCal bytes) into an Incidence. Returns null on parse failure.
    KCalendarCore::Incidence::Ptr incidenceFromRecord(const BackendRecord &record) const;
```
And in `public:` add a thin test seam (kept public so the pure test needs no daemon):
```cpp
    /// Test-only passthrough to incidenceFromRecord (no Akonadi server needed).
    KCalendarCore::Incidence::Ptr incidenceFromRecordForTest(const BackendRecord &record) const
    { return incidenceFromRecord(record); }
```

- [ ] **Step 3: Implement the helper**

In `src/calendar/akonadibackend.cpp`, near `extractIncidence`:
```cpp
KCalendarCore::Incidence::Ptr
AkonadiBackend::incidenceFromRecord(const BackendRecord &record) const
{
    KCalendarCore::ICalFormat fmt;
    KCalendarCore::MemoryCalendar::Ptr cal(
        new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone()));
    if (!fmt.fromString(cal, QString::fromUtf8(record.data)))
        return {};
    const auto incidences = cal->incidences();
    if (incidences.isEmpty())
        return {};
    return incidences.first();
}
```

- [ ] **Step 4: Register the test in CMake**

In `CMakeLists.txt`, inside the `if(KALBURATOR_HAVE_AKONADI)` test block (created in Task 14 — if not present yet, add it now):
```cmake
    kalburator_add_calendar_integration_test(tst_akonadi_payload
        tests/calendar/tst_akonadi_payload.cpp)
```

- [ ] **Step 5: Run the test**

Run: `cmake --build build-akonadi && ctest --test-dir build-akonadi -R tst_akonadi_payload --output-on-failure`
Expected: PASS (no daemon needed).

- [ ] **Step 6: Commit**

```bash
git add src/calendar/akonadibackend.h src/calendar/akonadibackend.cpp tests/calendar/tst_akonadi_payload.cpp CMakeLists.txt
git commit -m "feat(akonadi): incidenceFromRecord iCal deserialization helper + unit test"
```

---

## Task 3: Calendar write ops via KJob::exec()

Implement `createRecord`/`updateRecord`/`deleteRecord` for real, resolving the cross-backend UID to the cached Akonadi item.

**Files:**
- Modify: `src/calendar/akonadibackend.h` (declare `findCachedItem`)
- Modify: `src/calendar/akonadibackend.cpp:928-948` (replace the three stubs)
- Test: `tests/calendar/tst_akonadibackend_live.cpp` (new, env-gated)

- [ ] **Step 1: Write the failing live test**

Create `tests/calendar/tst_akonadibackend_live.cpp`:
```cpp
#include <QtTest>
#include "akonadibackend.h"
#include <Akonadi/ServerManager>

using namespace Kalburator::Sync;

class TestAkonadiBackendLive : public QObject {
    Q_OBJECT
private slots:
    void init();
    void createUpdateDeleteRoundTrip();
private:
    QString m_collectionId;  // set this to a real local calendar, e.g. "akonadi-<id>"
};

void TestAkonadiBackendLive::init() {
    if (qEnvironmentVariableIsEmpty("KALBURATOR_AKONADI_LIVE_TEST"))
        QSKIP("set KALBURATOR_AKONADI_LIVE_TEST=1 and run a local Akonadi");
    if (!Akonadi::ServerManager::isRunning())
        QSKIP("Akonadi server not running");
    m_collectionId = qEnvironmentVariable("KALBURATOR_AKONADI_CALENDAR_ID");
    if (m_collectionId.isEmpty())
        QSKIP("set KALBURATOR_AKONADI_CALENDAR_ID to a writable calendar id");
}

void TestAkonadiBackendLive::createUpdateDeleteRoundTrip() {
    AkonadiBackend backend;
    backend.loadCalendars(m_collectionId);
    QTest::qWait(500);  // let the monitor/cache populate

    const QByteArray ical =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\n"
        "UID:kalb-live-1\r\nSUMMARY:Created\r\n"
        "DTSTART:20260601T120000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    BackendRecord rec;
    rec.id = "kalb-live-1";
    rec.type = "calendar";
    rec.data = ical;

    const QString newId = backend.createRecord(m_collectionId, rec);
    QCOMPARE(newId, QStringLiteral("kalb-live-1"));

    rec.data.replace("Created", "Updated");
    QVERIFY(backend.updateRecord(rec));

    QVERIFY(backend.deleteRecord("kalb-live-1"));
}

QTEST_MAIN(TestAkonadiBackendLive)
#include "tst_akonadibackend_live.moc"
```

- [ ] **Step 2: Declare the item-resolution helper**

In `src/calendar/akonadibackend.h` `private:`:
```cpp
    /// Resolve a cross-backend record id (iCal UID) to its cached Akonadi
    /// item and owning calendar id. Returns an invalid Item if not found.
    Akonadi::Item findCachedItem(const QString &uid, QString *outCalendarId) const;
```

- [ ] **Step 3: Implement the helper + the three write ops**

In `src/calendar/akonadibackend.cpp`, add the helper near `findItemByUid`:
```cpp
Akonadi::Item AkonadiBackend::findCachedItem(const QString &uid,
                                             QString *outCalendarId) const
{
    for (auto cit = m_itemsByCalendar.constBegin();
         cit != m_itemsByCalendar.constEnd(); ++cit) {
        const auto inner = cit.value();
        if (inner.contains(uid)) {
            if (outCalendarId) *outCalendarId = cit.key();
            return inner.value(uid);
        }
    }
    return {};
}
```
Replace the `createRecord`/`updateRecord`/`deleteRecord` stub bodies (cpp:928-948) with:
```cpp
QString AkonadiBackend::createRecord(const QString &collectionId,
                                     const BackendRecord &record)
{
    auto colIt = m_collections.find(collectionId);
    if (colIt == m_collections.end()) {
        qWarning() << "AkonadiBackend::createRecord: unknown collection" << collectionId;
        return {};
    }
    KCalendarCore::Incidence::Ptr incidence = incidenceFromRecord(record);
    if (!incidence) {
        qWarning() << "AkonadiBackend::createRecord: iCal parse failed for" << record.id;
        return {};
    }
    Akonadi::Item item;
    item.setMimeType(incidence->mimeType());
    item.setPayload<KCalendarCore::Incidence::Ptr>(incidence);
    auto *job = new Akonadi::ItemCreateJob(item, *colIt, m_session);
    if (!job->exec()) {
        qWarning() << "AkonadiBackend::createRecord: ItemCreateJob failed:" << job->errorString();
        return {};
    }
    m_itemsByCalendar[collectionId][incidence->uid()] = job->item();
    return incidence->uid();
}

bool AkonadiBackend::updateRecord(const BackendRecord &record)
{
    KCalendarCore::Incidence::Ptr incidence = incidenceFromRecord(record);
    if (!incidence) {
        qWarning() << "AkonadiBackend::updateRecord: iCal parse failed for" << record.id;
        return false;
    }
    QString calId;
    Akonadi::Item existing = findCachedItem(record.id, &calId);
    if (!existing.isValid()) {
        qWarning() << "AkonadiBackend::updateRecord: no cached item for" << record.id;
        return false;
    }
    existing.setPayload<KCalendarCore::Incidence::Ptr>(incidence);
    auto *job = new Akonadi::ItemModifyJob(existing, m_session);
    if (!job->exec()) {
        qWarning() << "AkonadiBackend::updateRecord: ItemModifyJob failed:" << job->errorString();
        return false;
    }
    m_itemsByCalendar[calId][record.id] = job->item();
    return true;
}

bool AkonadiBackend::deleteRecord(const QString &recordId)
{
    QString calId;
    Akonadi::Item existing = findCachedItem(recordId, &calId);
    if (!existing.isValid()) {
        qWarning() << "AkonadiBackend::deleteRecord: no cached item for" << recordId;
        return false;
    }
    auto *job = new Akonadi::ItemDeleteJob(existing, m_session);
    if (!job->exec()) {
        qWarning() << "AkonadiBackend::deleteRecord: ItemDeleteJob failed:" << job->errorString();
        return false;
    }
    m_itemsByCalendar[calId].remove(recordId);
    return true;
}
```

- [ ] **Step 4: Add includes**

Confirm `src/calendar/akonadibackend.cpp` includes `<Akonadi/ItemCreateJob>`, `<Akonadi/ItemModifyJob>`, `<Akonadi/ItemDeleteJob>` (the vestigial `pushItems` already uses them; add any missing).

- [ ] **Step 5: Register the live test in CMake** (see Task 14 block)
```cmake
    kalburator_add_calendar_integration_test(tst_akonadibackend_live
        tests/calendar/tst_akonadibackend_live.cpp)
```

- [ ] **Step 6: Run (on a box with Akonadi)**

Run: `KALBURATOR_AKONADI_LIVE_TEST=1 KALBURATOR_AKONADI_CALENDAR_ID=akonadi-<id> ctest --test-dir build-akonadi -R tst_akonadibackend_live --output-on-failure`
Expected: PASS (or SKIP where no daemon). In CI: SKIP.

- [ ] **Step 7: Commit**

```bash
git add src/calendar/akonadibackend.h src/calendar/akonadibackend.cpp tests/calendar/tst_akonadibackend_live.cpp CMakeLists.txt
git commit -m "feat(akonadi): real createRecord/updateRecord/deleteRecord via KJob::exec()"
```

---

## Task 4: Calendar createCollection via CollectionCreateJob

**Files:**
- Modify: `src/calendar/akonadibackend.cpp:861` (replace the `createCollection` stub)
- Test: add `createsCollection()` to `tests/calendar/tst_akonadibackend_live.cpp`

- [ ] **Step 1: Add failing live test slot**

In `tst_akonadibackend_live.cpp`, declare `void createsCollection();` and implement:
```cpp
void TestAkonadiBackendLive::createsCollection() {
    const QString parent = qEnvironmentVariable("KALBURATOR_AKONADI_PARENT_ID");
    if (parent.isEmpty())
        QSKIP("set KALBURATOR_AKONADI_PARENT_ID to a writable parent collection id");
    AkonadiBackend backend;
    backend.loadCalendars(parent);
    QTest::qWait(500);

    CollectionInfo info;
    info.name = QStringLiteral("kalb-live-newcal");
    info.type = QStringLiteral("calendar");
    info.path = parent;  // parent collection id carrier (see impl)

    const QString id = backend.createCollection(info);
    QVERIFY(!id.isEmpty());
}
```

- [ ] **Step 2: Implement createCollection**

Replace the stub at `src/calendar/akonadibackend.cpp:861`:
```cpp
QString AkonadiBackend::createCollection(const CollectionInfo &info)
{
    const Akonadi::Collection::Id parentId = collectionIdForCalendar(info.path);
    if (parentId < 0) {
        qWarning() << "AkonadiBackend::createCollection: unknown parent" << info.path;
        return {};
    }
    Akonadi::Collection col;
    col.setParentCollection(Akonadi::Collection(parentId));
    col.setName(info.name);
    col.setContentMimeTypes({
        KCalendarCore::Event::eventMimeType(),
        KCalendarCore::Todo::todoMimeType(),
        KCalendarCore::Journal::journalMimeType()});
    auto *job = new Akonadi::CollectionCreateJob(col, m_session);
    if (!job->exec()) {
        qWarning() << "AkonadiBackend::createCollection failed:" << job->errorString();
        return {};
    }
    return calendarIdForCollection(job->collection().id());
}
```

- [ ] **Step 3: Add include**

Add `#include <Akonadi/CollectionCreateJob>` to `src/calendar/akonadibackend.cpp`.

- [ ] **Step 4: Build + run live test**

Run: `cmake --build build-akonadi && KALBURATOR_AKONADI_LIVE_TEST=1 KALBURATOR_AKONADI_PARENT_ID=akonadi-<id> ctest --test-dir build-akonadi -R tst_akonadibackend_live --output-on-failure`
Expected: PASS / SKIP.

- [ ] **Step 5: Commit**

```bash
git add src/calendar/akonadibackend.cpp tests/calendar/tst_akonadibackend_live.cpp
git commit -m "feat(akonadi): real createCollection via CollectionCreateJob"
```

---

## Task 5: Contacts identity fix + vCard deserialization helper

Mirror Tasks 1–2 for `AkonadiContactsBackend` (vCard / `KContacts::Addressee`).

**Files:**
- Modify: `src/contacts/akonadicontactsbackend.cpp` (`loadRecords` `rec.id`, add `addresseeFromRecord`)
- Modify: `src/contacts/akonadicontactsbackend.h` (declare helper + test seam)
- Test: `tests/contacts/tst_akonadi_contacts_payload.cpp` (new, pure)

- [ ] **Step 1: Write the failing pure test**

Create `tests/contacts/tst_akonadi_contacts_payload.cpp`:
```cpp
#include <QtTest>
#include "akonadicontactsbackend.h"

using namespace Kalburator::Sync;

class TestAkonadiContactsPayload : public QObject {
    Q_OBJECT
private slots:
    void roundTripsUid();
};

void TestAkonadiContactsPayload::roundTripsUid() {
    const QByteArray vcard =
        "BEGIN:VCARD\r\nVERSION:4.0\r\nUID:contact-uid-1\r\n"
        "FN:Jane Doe\r\nEND:VCARD\r\n";
    BackendRecord rec;
    rec.id = "contact-uid-1";
    rec.data = vcard;

    AkonadiContactsBackend backend;
    const auto addressee = backend.addresseeFromRecordForTest(rec);
    QCOMPARE(addressee.uid(), QStringLiteral("contact-uid-1"));
    QCOMPARE(addressee.formattedName(), QStringLiteral("Jane Doe"));
}

QTEST_MAIN(TestAkonadiContactsPayload)
#include "tst_akonadi_contacts_payload.moc"
```

- [ ] **Step 2: Fix the id + add the helper**

In `src/contacts/akonadicontactsbackend.cpp` `loadRecords`, set `rec.id = addressee.uid();` (not the Akonadi item id). Add:
```cpp
KContacts::Addressee
AkonadiContactsBackend::addresseeFromRecord(const BackendRecord &record) const
{
    KContacts::VCardConverter converter;
    return converter.parseVCard(record.data);
}
```
In the header, declare `KContacts::Addressee addresseeFromRecord(const BackendRecord &record) const;` (private) and a public test seam `addresseeFromRecordForTest` returning the same. Add `#include <KContacts/VCardConverter>` and `#include <KContacts/Addressee>`.

- [ ] **Step 3: Register test in CMake** (Task 14 block)
```cmake
    kalburator_add_contacts_integration_test(tst_akonadi_contacts_payload
        tests/contacts/tst_akonadi_contacts_payload.cpp)
```
(If no contacts test helper exists, use `kalburator_add_calendar_integration_test`'s pattern — see Task 14.)

- [ ] **Step 4: Run**

Run: `cmake --build build-akonadi && ctest --test-dir build-akonadi -R tst_akonadi_contacts_payload --output-on-failure`
Expected: PASS (no daemon).

- [ ] **Step 5: Commit**

```bash
git add src/contacts/akonadicontactsbackend.h src/contacts/akonadicontactsbackend.cpp tests/contacts/tst_akonadi_contacts_payload.cpp CMakeLists.txt
git commit -m "fix(akonadi): contacts use vCard UID as id + addresseeFromRecord helper"
```

---

## Task 6: Contacts write ops + createCollection

Mirror Tasks 3–4 for contacts (`ItemCreateJob`/`ItemModifyJob`/`ItemDeleteJob` with `KContacts::Addressee` payload; `CollectionCreateJob` with `KContacts::Addressee::mimeType()`).

**Files:**
- Modify: `src/contacts/akonadicontactsbackend.h` (declare `findCachedItem`)
- Modify: `src/contacts/akonadicontactsbackend.cpp` (replace `createRecord`/`updateRecord`/`deleteRecord`/`createCollection` stubs)
- Test: `tests/contacts/tst_akonadicontactsbackend_live.cpp` (new, env-gated)

- [ ] **Step 1: Write the failing live test**

Create `tests/contacts/tst_akonadicontactsbackend_live.cpp` mirroring Task 3's test, with vCard data and env var `KALBURATOR_AKONADI_CONTACTS_ID`:
```cpp
#include <QtTest>
#include "akonadicontactsbackend.h"
#include <Akonadi/ServerManager>
using namespace Kalburator::Sync;
class TestAkonadiContactsLive : public QObject {
    Q_OBJECT
private slots:
    void init();
    void createUpdateDeleteRoundTrip();
private:
    QString m_collectionId;
};
void TestAkonadiContactsLive::init() {
    if (qEnvironmentVariableIsEmpty("KALBURATOR_AKONADI_LIVE_TEST"))
        QSKIP("set KALBURATOR_AKONADI_LIVE_TEST=1 and run a local Akonadi");
    if (!Akonadi::ServerManager::isRunning())
        QSKIP("Akonadi server not running");
    m_collectionId = qEnvironmentVariable("KALBURATOR_AKONADI_CONTACTS_ID");
    if (m_collectionId.isEmpty())
        QSKIP("set KALBURATOR_AKONADI_CONTACTS_ID to a writable addressbook id");
}
void TestAkonadiContactsLive::createUpdateDeleteRoundTrip() {
    AkonadiContactsBackend backend;
    backend.fetchItems(m_collectionId);
    QTest::qWait(500);
    const QByteArray vcard =
        "BEGIN:VCARD\r\nVERSION:4.0\r\nUID:kalb-contact-1\r\n"
        "FN:Created Person\r\nEND:VCARD\r\n";
    BackendRecord rec; rec.id = "kalb-contact-1"; rec.type = "contact"; rec.data = vcard;
    QCOMPARE(backend.createRecord(m_collectionId, rec), QStringLiteral("kalb-contact-1"));
    rec.data.replace("Created Person", "Updated Person");
    QVERIFY(backend.updateRecord(rec));
    QVERIFY(backend.deleteRecord("kalb-contact-1"));
}
QTEST_MAIN(TestAkonadiContactsLive)
#include "tst_akonadicontactsbackend_live.moc"
```

- [ ] **Step 2: Implement** the contacts `findCachedItem` (iterate `m_itemsByCollection`), and `createRecord`/`updateRecord`/`deleteRecord` exactly as Task 3 but using `addresseeFromRecord(record)`, `item.setMimeType(KContacts::Addressee::mimeType())`, `item.setPayload<KContacts::Addressee>(addressee)`, and the `m_itemsByCollection` cache. Implement `createCollection` as Task 4 but with `col.setContentMimeTypes({KContacts::Addressee::mimeType()})`. Add the Akonadi job includes.

- [ ] **Step 3: Register live test in CMake** (Task 14 block).

- [ ] **Step 4: Build + run**

Run: `cmake --build build-akonadi && KALBURATOR_AKONADI_LIVE_TEST=1 KALBURATOR_AKONADI_CONTACTS_ID=akonadi-contacts-<id> ctest --test-dir build-akonadi -R tst_akonadicontactsbackend_live --output-on-failure`
Expected: PASS / SKIP.

- [ ] **Step 5: Commit**

```bash
git add src/contacts/akonadicontactsbackend.h src/contacts/akonadicontactsbackend.cpp tests/contacts/tst_akonadicontactsbackend_live.cpp CMakeLists.txt
git commit -m "feat(akonadi): real contacts write ops + createCollection"
```

---

## Task 7: Revision digest helper (pure unit-testable)

`computeRevisionDigest()` turns an unordered list of (item id, revision) pairs into a stable token — the CTag analogue.

**Files:**
- Create: `src/sync/akonadirevisiondigest.h` / `.cpp`
- Test: `tests/sync/tst_akonadirevisiondigest.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/sync/tst_akonadirevisiondigest.cpp`:
```cpp
#include <QtTest>
#include "akonadirevisiondigest.h"
using namespace Kalburator::Sync;
class TestRevisionDigest : public QObject {
    Q_OBJECT
private slots:
    void stableRegardlessOfOrder();
    void changesWhenRevisionChanges();
    void emptyIsEmpty();
};
void TestRevisionDigest::stableRegardlessOfOrder() {
    QList<QPair<qint64,int>> a{{1,3},{2,5},{3,1}};
    QList<QPair<qint64,int>> b{{3,1},{1,3},{2,5}};
    QCOMPARE(computeRevisionDigest(a), computeRevisionDigest(b));
    QVERIFY(!computeRevisionDigest(a).isEmpty());
}
void TestRevisionDigest::changesWhenRevisionChanges() {
    QList<QPair<qint64,int>> a{{1,3},{2,5}};
    QList<QPair<qint64,int>> b{{1,3},{2,6}};
    QVERIFY(computeRevisionDigest(a) != computeRevisionDigest(b));
}
void TestRevisionDigest::emptyIsEmpty() {
    QCOMPARE(computeRevisionDigest({}), QString());
}
QTEST_MAIN(TestRevisionDigest)
#include "tst_akonadirevisiondigest.moc"
```

- [ ] **Step 2: Run to verify it fails**

Run: `ctest --test-dir build-akonadi -R tst_akonadirevisiondigest` → FAIL (header not found until built).

- [ ] **Step 3: Implement**

Create `src/sync/akonadirevisiondigest.h`:
```cpp
#ifndef KALBURATOR_SYNC_AKONADIREVISIONDIGEST_H
#define KALBURATOR_SYNC_AKONADIREVISIONDIGEST_H
#include <QList>
#include <QPair>
#include <QString>
namespace Kalburator::Sync {
/// Stable token over (Akonadi item id, revision) pairs. Order-independent.
/// Empty input -> empty string (engine treats as "changed").
QString computeRevisionDigest(QList<QPair<qint64, int>> idRev);
}
#endif
```
Create `src/sync/akonadirevisiondigest.cpp`:
```cpp
#include "akonadirevisiondigest.h"
#include <QCryptographicHash>
#include <algorithm>
namespace Kalburator::Sync {
QString computeRevisionDigest(QList<QPair<qint64, int>> idRev)
{
    if (idRev.isEmpty()) return {};
    std::sort(idRev.begin(), idRev.end());
    QCryptographicHash h(QCryptographicHash::Sha256);
    for (const auto &p : idRev) {
        h.addData(QByteArray::number(p.first));
        h.addData(":");
        h.addData(QByteArray::number(p.second));
        h.addData(";");
    }
    return QString::fromLatin1(h.result().toHex());
}
}
```

- [ ] **Step 4: Register source + test in CMake** — add the `.cpp` to the Akonadi-gated sources (Task 14) and the test to the test block.

- [ ] **Step 5: Run** → PASS.

- [ ] **Step 6: Commit**

```bash
git add src/sync/akonadirevisiondigest.h src/sync/akonadirevisiondigest.cpp tests/sync/tst_akonadirevisiondigest.cpp CMakeLists.txt
git commit -m "feat(akonadi): order-stable revision digest helper + unit test"
```

---

## Task 8: Persistent revision store (pure unit-testable)

`AkonadiRevisionStore` persists `collectionId → token` across runs (CTagStore analogue), backed by a QSettings ini file at a caller-supplied path.

**Files:**
- Create: `src/sync/akonadirevisionstore.h` / `.cpp`
- Test: `tests/sync/tst_akonadirevisionstore.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/sync/tst_akonadirevisionstore.cpp`:
```cpp
#include <QtTest>
#include <QTemporaryDir>
#include "akonadirevisionstore.h"
using namespace Kalburator::Sync;
class TestRevisionStore : public QObject {
    Q_OBJECT
private slots:
    void persistsAcrossInstances();
    void missingIsEmpty();
};
void TestRevisionStore::persistsAcrossInstances() {
    QTemporaryDir dir;
    const QString path = dir.filePath("rev.ini");
    { AkonadiRevisionStore s(path); s.setToken("akonadi-1", "tok-abc"); }
    AkonadiRevisionStore s2(path);
    QCOMPARE(s2.token("akonadi-1"), QStringLiteral("tok-abc"));
}
void TestRevisionStore::missingIsEmpty() {
    QTemporaryDir dir;
    AkonadiRevisionStore s(dir.filePath("rev.ini"));
    QCOMPARE(s.token("nope"), QString());
}
QTEST_MAIN(TestRevisionStore)
#include "tst_akonadirevisionstore.moc"
```

- [ ] **Step 2: Implement**

Create `src/sync/akonadirevisionstore.h`:
```cpp
#ifndef KALBURATOR_SYNC_AKONADIREVISIONSTORE_H
#define KALBURATOR_SYNC_AKONADIREVISIONSTORE_H
#include <QString>
namespace Kalburator::Sync {
/// Persists per-collection revision tokens across process restarts.
/// CTagStore analogue for Akonadi's synthesized change-detection token.
class AkonadiRevisionStore {
public:
    explicit AkonadiRevisionStore(const QString &filePath);
    QString token(const QString &collectionId) const;
    void    setToken(const QString &collectionId, const QString &token);
private:
    QString m_filePath;
};
}
#endif
```
Create `src/sync/akonadirevisionstore.cpp`:
```cpp
#include "akonadirevisionstore.h"
#include <QSettings>
namespace Kalburator::Sync {
AkonadiRevisionStore::AkonadiRevisionStore(const QString &filePath)
    : m_filePath(filePath) {}
QString AkonadiRevisionStore::token(const QString &collectionId) const {
    QSettings s(m_filePath, QSettings::IniFormat);
    s.beginGroup(QStringLiteral("revisions"));
    return s.value(collectionId).toString();
}
void AkonadiRevisionStore::setToken(const QString &collectionId, const QString &token) {
    QSettings s(m_filePath, QSettings::IniFormat);
    s.beginGroup(QStringLiteral("revisions"));
    s.setValue(collectionId, token);
}
}
```

- [ ] **Step 3: Register source + test in CMake** (Task 14).

- [ ] **Step 4: Run** → PASS.

- [ ] **Step 5: Commit**

```bash
git add src/sync/akonadirevisionstore.h src/sync/akonadirevisionstore.cpp tests/sync/tst_akonadirevisionstore.cpp CMakeLists.txt
git commit -m "feat(akonadi): persistent per-collection revision store + unit test"
```

---

## Task 9: ChangeDetection on the calendar backend

Wire the digest + store into the `Backend::ChangeDetection` interface so the engine can skip unchanged collections.

**Files:**
- Modify: `src/calendar/akonadibackend.h` (inherit interface, declare overrides + members)
- Modify: `src/calendar/akonadibackend.cpp` (implement)
- Test: add `changeDetectionSkipsUnchanged()` to `tst_akonadibackend_live.cpp`

- [ ] **Step 1: Write the failing live test slot**
```cpp
void TestAkonadiBackendLive::changeDetectionSkipsUnchanged() {
    AkonadiBackend backend;
    backend.loadCalendars(m_collectionId);
    QTest::qWait(500);
    const QString r1 = backend.collectionRevision(m_collectionId);
    QVERIFY(!r1.isEmpty());
    const QString r2 = backend.collectionRevision(m_collectionId);
    QCOMPARE(r1, r2);  // unchanged collection -> identical token
}
```
Declare it in the test class.

- [ ] **Step 2: Edit the class declaration**

In `src/calendar/akonadibackend.h`: add `#include "changedetection.h"`, change the base list to:
```cpp
class AkonadiBackend : public SyncBackend,
                       public Kalburator::Backend::ChangeDetection
```
In `public:` add:
```cpp
    // === Backend::ChangeDetection ===
    QString collectionRevision(const QString &collectionId) override;
    QString cachedCollectionRevision(const QString &collectionId) const override;
    void    primeRevisionCache(const QMap<QString, QString> &cache) override;
```
In `private:` add:
```cpp
    /// Lazily-constructed persistent revision token store.
    Kalburator::Sync::AkonadiRevisionStore *revisionStore() const;
    mutable std::unique_ptr<Kalburator::Sync::AkonadiRevisionStore> m_revisionStore;
```
Add forward include `#include "akonadirevisionstore.h"` and `#include <memory>`.

- [ ] **Step 3: Implement**

In `src/calendar/akonadibackend.cpp` add `#include "akonadirevisiondigest.h"`, `#include <Akonadi/ItemFetchJob>`, `#include <Akonadi/ItemFetchScope>`, `#include <QStandardPaths>`, `#include <QDir>`:
```cpp
Kalburator::Sync::AkonadiRevisionStore *AkonadiBackend::revisionStore() const
{
    if (!m_revisionStore) {
        const QString dir = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        m_revisionStore = std::make_unique<Kalburator::Sync::AkonadiRevisionStore>(
            dir + QStringLiteral("/akonadi-calendar-revisions.ini"));
    }
    return m_revisionStore.get();
}

QString AkonadiBackend::collectionRevision(const QString &collectionId)
{
    const Akonadi::Collection::Id cid = collectionIdForCalendar(collectionId);
    if (cid < 0) return {};
    auto *job = new Akonadi::ItemFetchJob(Akonadi::Collection(cid), m_session);
    job->fetchScope().fetchFullPayload(false);  // ids + revisions only, no decode
    if (!job->exec()) {
        qWarning() << "AkonadiBackend::collectionRevision: fetch failed:" << job->errorString();
        return {};
    }
    QList<QPair<qint64, int>> idRev;
    const auto items = job->items();
    idRev.reserve(items.size());
    for (const auto &it : items)
        idRev.append({it.id(), it.revision()});
    return computeRevisionDigest(idRev);
}

QString AkonadiBackend::cachedCollectionRevision(const QString &collectionId) const
{
    return revisionStore()->token(collectionId);
}

void AkonadiBackend::primeRevisionCache(const QMap<QString, QString> &cache)
{
    for (auto it = cache.constBegin(); it != cache.constEnd(); ++it)
        revisionStore()->setToken(it.key(), it.value());
}
```

- [ ] **Step 4: Build + run** → PASS / SKIP.

- [ ] **Step 5: Commit**

```bash
git add src/calendar/akonadibackend.h src/calendar/akonadibackend.cpp tests/calendar/tst_akonadibackend_live.cpp
git commit -m "feat(akonadi): calendar Backend::ChangeDetection via id+revision digest"
```

---

## Task 10: ChangeDetection on the contacts backend

Mirror Task 9 for `AkonadiContactsBackend` (store file `akonadi-contacts-revisions.ini`, `akonadiIdForCollection`).

**Files:**
- Modify: `src/contacts/akonadicontactsbackend.h` / `.cpp`
- Test: add `changeDetectionSkipsUnchanged()` to `tst_akonadicontactsbackend_live.cpp`

- [ ] **Step 1–4:** Apply Task 9 verbatim with contacts names (`collectionIdForCalendar` → `akonadiIdForCollection`, calendar store path → contacts path, `m_session`/fetch identical). Add the same live test slot using `m_collectionId` from `KALBURATOR_AKONADI_CONTACTS_ID`.

- [ ] **Step 5: Build + run** → PASS / SKIP.

- [ ] **Step 6: Commit**

```bash
git add src/contacts/akonadicontactsbackend.h src/contacts/akonadicontactsbackend.cpp tests/contacts/tst_akonadicontactsbackend_live.cpp
git commit -m "feat(akonadi): contacts Backend::ChangeDetection via id+revision digest"
```

---

## Task 11: contentHash memoization by Item::revision()

Avoid re-serializing+re-hashing items whose `Item::revision()` is unchanged since the last `loadRecords`.

**Files:**
- Modify: `src/calendar/akonadibackend.h` (add memo member)
- Modify: `src/calendar/akonadibackend.cpp` (`loadRecords`)
- Modify: `src/contacts/akonadicontactsbackend.{h,cpp}` (same)

- [ ] **Step 1: Add the memo member**

In each backend header `private:`:
```cpp
    /// uid -> (Akonadi item revision, cached contentHash). Lets loadRecords
    /// skip re-serializing+re-hashing an item whose revision is unchanged.
    mutable QMap<QString, QPair<int, QString>> m_hashMemo;
```

- [ ] **Step 2: Use it in loadRecords (calendar)**

In `AkonadiBackend::loadRecords`, replace the hash computation block with:
```cpp
        const QString uid = incidence->uid();
        QString hash;
        const auto memo = m_hashMemo.constFind(uid);
        if (memo != m_hashMemo.constEnd() && memo->first == aItem.revision()) {
            hash = memo->second;
        } else {
            hash = QString::fromLatin1(
                QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
            m_hashMemo[uid] = qMakePair(aItem.revision(), hash);
        }
        rec.contentHash = hash;
```
(Remove the old direct `rec.contentHash = ...` line.) Apply the equivalent in the contacts `loadRecords` using `addressee.uid()`.

- [ ] **Step 3: Build** → compiles. (Correctness is covered by the existing round-trip live tests; the memo is a transparent optimization.)

- [ ] **Step 4: Commit**

```bash
git add src/calendar/akonadibackend.h src/calendar/akonadibackend.cpp src/contacts/akonadicontactsbackend.h src/contacts/akonadicontactsbackend.cpp
git commit -m "perf(akonadi): memoize contentHash by Item::revision()"
```

---

## Task 12: ChangeRecorder warm-path layer

Upgrade the live `Akonadi::Monitor` to a persistent `ChangeRecorder`, exposing a "dirty uids since last sync" set used as a fast path on top of the digest backbone. The digest remains the correctness floor — when the recorder is empty but the digest differs (changes during downtime), callers fall back to the full path.

**Files:**
- Modify: `src/calendar/akonadibackend.h` (replace `Akonadi::Monitor*` with `Akonadi::ChangeRecorder*`, add dirty-set API)
- Modify: `src/calendar/akonadibackend.cpp` (`setupMonitor`, recorder config, dirty tracking)
- Modify: `src/contacts/akonadicontactsbackend.{h,cpp}` (same)
- Test: add `recorderTracksDirtyUids()` to each live test

- [ ] **Step 1: Write the failing live test slot (calendar)**
```cpp
void TestAkonadiBackendLive::recorderTracksDirtyUids() {
    AkonadiBackend backend;
    backend.loadCalendars(m_collectionId);
    QTest::qWait(500);
    const QByteArray ical =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\n"
        "UID:kalb-dirty-1\r\nSUMMARY:Dirty\r\n"
        "DTSTART:20260601T120000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    BackendRecord rec; rec.id = "kalb-dirty-1"; rec.data = ical;
    backend.createRecord(m_collectionId, rec);
    QTest::qWait(500);  // let the recorder observe the external-looking change
    // The recorder should have observed at least one change for this collection.
    QVERIFY(backend.hasRecordedChanges(m_collectionId));
    backend.deleteRecord("kalb-dirty-1");
}
```
Declare `bool hasRecordedChanges(const QString &collectionId) const;` in the test target's backend.

- [ ] **Step 2: Swap Monitor for ChangeRecorder**

In `src/calendar/akonadibackend.h`: change the member type to `Akonadi::ChangeRecorder *m_monitor = nullptr;` and add `#include <Akonadi/ChangeRecorder>`. Add public:
```cpp
    /// True if the ChangeRecorder has observed (and not yet consumed) any
    /// change for this collection since the last sync. Warm-path hint only —
    /// callers must still trust the digest as the correctness floor.
    bool hasRecordedChanges(const QString &collectionId) const;
```
Add private member:
```cpp
    QSet<QString> m_recordedDirtyCalendars;  // calendarIds with observed changes
```

- [ ] **Step 3: Configure the recorder in setupMonitor()**

In `AkonadiBackend::setupMonitor()`, construct a `ChangeRecorder` instead of a `Monitor`, give it persistent identity, and record dirty collections in the existing `onItemAdded`/`onItemChanged`/`onItemRemoved` slots:
```cpp
    m_monitor = new Akonadi::ChangeRecorder(this);
    auto *settings = new QSettings(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/akonadi-calendar-recorder.ini"),
        QSettings::IniFormat, m_monitor);
    m_monitor->setConfig(settings);
    m_monitor->setChangeRecordingEnabled(true);
    // ... existing setMimeTypeMonitored / setSession(ignored) / connects unchanged ...
```
In `onItemAdded`/`onItemChanged`/`onItemRemoved`, after the existing cache maintenance, add:
```cpp
    m_recordedDirtyCalendars.insert(calId);  // calId already computed in each slot
```
Implement:
```cpp
bool AkonadiBackend::hasRecordedChanges(const QString &collectionId) const
{ return m_recordedDirtyCalendars.contains(collectionId); }
```
After a successful `collectionRevision()`/sync write-back, clear consumed entries: at the end of `primeRevisionCache`, `for (auto it = cache.constBegin(); ...) m_recordedDirtyCalendars.remove(it.key());`.

- [ ] **Step 4: Mirror for contacts** (`akonadi-contacts-recorder.ini`, `m_recordedDirtyCollections`).

- [ ] **Step 5: Build + run** → PASS / SKIP.

- [ ] **Step 6: Commit**

```bash
git add src/calendar/akonadibackend.h src/calendar/akonadibackend.cpp src/contacts/akonadicontactsbackend.h src/contacts/akonadicontactsbackend.cpp tests/calendar/tst_akonadibackend_live.cpp tests/contacts/tst_akonadicontactsbackend_live.cpp
git commit -m "feat(akonadi): ChangeRecorder warm-path dirty tracking over digest backbone"
```

---

## Task 13: Mark the vestigial pushItems/startSync write path

`pushItems`/`startSync`/`deleteItems` are `SyncBackend` ABI overrides the unified engine never calls. Do **not** delete them (the base class still declares them; deletion belongs to the separate SyncBackend ABI cleanup). Mark them clearly so no one re-wires them.

**Files:**
- Modify: `src/calendar/akonadibackend.cpp` (comment above `pushItems`)
- Modify: `src/contacts/akonadicontactsbackend.cpp` (comment above `pushItems`)

- [ ] **Step 1: Add the marker comment**

Immediately above `AkonadiBackend::pushItems`:
```cpp
// VESTIGIAL (2026-05-26): the unified SyncEngine drives all writes through the
// per-record IBlobBackend ops (createRecord/updateRecord/deleteRecord), never
// pushItems/startSync/deleteItems. These remain only because they are
// SyncBackend ABI overrides; full removal is deferred to the SyncBackend ABI
// cleanup. Do not wire new write logic here — see
// docs/2026-05-26-akonadi-full-functionality-design.md §1.
```
Same above the contacts `pushItems`.

- [ ] **Step 2: Remove the stale "Phase F" stub comments** in both headers/cpps (the "Phase F will wrap these" / "deferred to Phase F" blocks) — replace with a one-line pointer to the design doc.

- [ ] **Step 3: Build** → compiles.

- [ ] **Step 4: Commit**

```bash
git add src/calendar/akonadibackend.cpp src/calendar/akonadibackend.h src/contacts/akonadicontactsbackend.cpp src/contacts/akonadicontactsbackend.h
git commit -m "docs(akonadi): mark vestigial pushItems path, drop stale Phase F comments"
```

---

## Task 14: CMake wiring for new sources and tests

Register the new `src/sync/*` sources in the Akonadi-gated build and add a contacts test helper if missing.

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add the new sources to the gated set**

Find the `KALBURATOR_SYNC_SOURCES`/`KALBURATOR_SYNC_HEADERS` lists and ensure `akonadirevisionstore.{h,cpp}` and `akonadirevisiondigest.{h,cpp}` are included. Because the existing `if(NOT KALBURATOR_HAVE_AKONADI)` block (CMakeLists.txt:609-625) *excludes* akonadi files by regex, add matching exclusions so these compile only when ON:
```cmake
    list(FILTER KALBURATOR_SYNC_SOURCES EXCLUDE REGEX "akonadirevisionstore\\.cpp$")
    list(FILTER KALBURATOR_SYNC_SOURCES EXCLUDE REGEX "akonadirevisiondigest\\.cpp$")
    list(FILTER KALBURATOR_SYNC_HEADERS EXCLUDE REGEX "akonadirevisionstore\\.h$")
    list(FILTER KALBURATOR_SYNC_HEADERS EXCLUDE REGEX "akonadirevisiondigest\\.h$")
```

- [ ] **Step 2: Add a gated test block**

If not already present, add near the other test registration:
```cmake
if(KALBURATOR_HAVE_AKONADI)
    # Pure unit tests (run in CI even without a daemon — they self-contain).
    kalburator_add_calendar_integration_test(tst_akonadirevisiondigest
        tests/sync/tst_akonadirevisiondigest.cpp)
    kalburator_add_calendar_integration_test(tst_akonadirevisionstore
        tests/sync/tst_akonadirevisionstore.cpp)
    # (payload + live tests are registered in their own tasks above)
endif()
```
If `kalburator_add_contacts_integration_test` does not exist, define it by copying `kalburator_add_calendar_integration_test` and linking `kalburator` + `KPim6::AkonadiCore` + `KPim6::Contacts`.

- [ ] **Step 3: Configure + build the whole Akonadi profile**

Run: `cmake -S . -B build-akonadi -DKALBURATOR_HAVE_AKONADI=ON -DCMAKE_MODULE_PATH=/usr/share/ECM/modules && cmake --build build-akonadi`
Expected: clean build.

- [ ] **Step 4: Run the CI-safe (pure) tests**

Run: `ctest --test-dir build-akonadi -R "tst_akonadi.*payload|tst_akonadirevision" --output-on-failure`
Expected: all PASS without a daemon.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt
git commit -m "build(akonadi): register revision store/digest sources + gated tests"
```

---

## Task 15: Doc honesty — correct the lying status docs

**Files:**
- Modify: `docs/phase0/04y-phase-l-status.md`
- Modify: `docs/phase0/04w-deferred-work.md` (entry C.1)
- Modify: `docs/campaign/FINDINGS.md`

- [ ] **Step 1: Correct Phase L status**

In `04y-phase-l-status.md`, under "What landed", change the `AkonadiContactsBackend` line to state plainly that Phase L shipped the contacts *skeleton* (identity + read), and that real write ops + change detection landed 2026-05-26 (this work). Add a short "2026-05-26 follow-up" subsection linking `docs/2026-05-26-akonadi-full-functionality-design.md`.

- [ ] **Step 2: Correct deferred-work C.1**

In `04w-deferred-work.md` C.1, replace the "lands with parity ... push/fetch/delete operations" acceptance bullet (which was not actually met) with an honest split: provider/discovery/read landed in Phase L; write path + collection creation + change detection landed 2026-05-26. Update status line to `✅ landed 2026-05-26` for the write-path portion.

- [ ] **Step 3: Log the inherited engine dependency in FINDINGS**

Append an entry to `docs/campaign/FINDINGS.md`:
> **O13 — baseline-load filters to blob domain (OPEN, inherited).** `syncengine.cpp:2121-2123` loads only `blob`-domain baselines, so baseline-driven deletion detection for calendar/contacts is not yet active for *any* backend (DAV included). Akonadi inherits this. Not Akonadi-specific; tracked here so Akonadi sync deletion behavior is understood. (Seeded 2026-05-26, Akonadi full-functionality work.)

- [ ] **Step 4: Commit**

```bash
git add docs/phase0/04y-phase-l-status.md docs/phase0/04w-deferred-work.md docs/campaign/FINDINGS.md
git commit -m "docs(akonadi): correct Phase L status, log baseline-filter dependency (O13)"
```

---

## Task 16: Full-profile verification + manual smoke

- [ ] **Step 1: Default profile still builds/tests (Akonadi excluded)**

Run: `cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build && ctest --test-dir build --output-on-failure`
Expected: green (Akonadi code excluded; no regressions).

- [ ] **Step 2: Akonadi profile pure tests green**

Run: `ctest --test-dir build-akonadi -R "tst_akonadi.*payload|tst_akonadirevision" --output-on-failure`
Expected: PASS.

- [ ] **Step 3: Live tests on a daemon box**

Run with all env vars set (`KALBURATOR_AKONADI_LIVE_TEST=1`, `KALBURATOR_AKONADI_CALENDAR_ID`, `KALBURATOR_AKONADI_CONTACTS_ID`, `KALBURATOR_AKONADI_PARENT_ID`):
`ctest --test-dir build-akonadi -R "tst_akonadibackend_live|tst_akonadicontactsbackend_live" --output-on-failure`
Expected: PASS.

- [ ] **Step 4: Manual WildPalms smoke** — run a Palm↔Akonadi sync through WildPalms against a live Akonadi calendar + addressbook; confirm create/update/delete propagate and a second sync of unchanged data is skipped (ChangeDetection). Record the result in `04y-phase-l-status.md`.

- [ ] **Step 5: Final commit**

```bash
git add docs/phase0/04y-phase-l-status.md
git commit -m "test(akonadi): record full-profile + live verification results"
```

---

## Self-review notes

- **Spec coverage:** Components 1 (write relocation: Tasks 1–6, 13), 2 (collection creation: Tasks 4, 6), 3 (change-detection parity: Tasks 7–11), 4 (ChangeRecorder: Task 12), 5 (tests + doc honesty: Tasks 2/3/5/6/7/8/9/10/12 tests, 15) all map to tasks. The §7 engine baseline-filter risk is logged in Task 15 (FINDINGS O13).
- **Identity correction** (not in the spec but discovered during planning) is Task 1/5 — required for any cross-backend sync to work.
- **Type consistency:** `incidenceFromRecord`/`addresseeFromRecord`, `findCachedItem`, `computeRevisionDigest`, `AkonadiRevisionStore::token/setToken`, `hasRecordedChanges`, `m_hashMemo`, `m_recordedDirtyCalendars` are used consistently across the tasks that reference them.
- **Deviation from design:** the design said "delete" the vestigial `pushItems`; the plan downgrades this to "mark vestigial" (Task 13) because those methods are `SyncBackend` ABI overrides whose removal belongs to a separate ABI cleanup. Flagged here intentionally.
