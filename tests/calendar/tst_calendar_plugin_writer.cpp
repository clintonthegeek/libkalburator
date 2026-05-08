// tst_calendar_plugin_writer.cpp
//
// Phase Ia.5 Task 5 — exercise CalendarPluginWriter end-to-end.
//
// The writer is the calendar domain's IRecordWriter implementation:
// it parses iCal bytes from BackendRecord::data into Incidence::Ptr
// and drives a SyncTransaction of CreateIncidenceItem /
// UpdateIncidenceItem / DeleteIncidenceItem against the backend on
// the backend's own thread (BlockingQueuedConnection).
//
// Coverage scope (per Task 5 plan): smoke-level — exercise the create
// path with real iCal bytes, plus a delete-of-existing path. Richer
// update/conflict coverage lands once the engine wires the writer in
// (Tasks 13/20).

#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>
#include <QTimeZone>
#include <QtTest/QtTest>

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include "backendrecord.h"
#include "calendarplugin_writer.h"
#include "mockbackend.h"

#include "stubs/stubcalendarcollection.h"

using namespace Kalburator::Sync;
using namespace Kalburator::Sync::Test;
using Kalburator::Calendar::CalendarPluginWriter;

namespace {

constexpr auto kCalendarId = "cal-1";

KCalendarCore::Event::Ptr makeEvent(const QString &uid, const QString &summary)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime::currentDateTimeUtc());
    event->setLastModified(QDateTime::currentDateTimeUtc());
    return event;
}

QByteArray icalBytes(const KCalendarCore::Incidence::Ptr &inc)
{
    KCalendarCore::ICalFormat fmt;
    return fmt.toICalString(inc).toUtf8();
}

/// Run `writer.apply(...)` on a worker thread and wait on the main
/// thread for completion, spinning the main event loop. This is
/// required because the writer commits its SyncTransaction via
/// `QMetaObject::invokeMethod(backend, ..., BlockingQueuedConnection)`
/// — the backend lives on the main thread, so the caller must be on
/// a different thread and the main thread must be processing events
/// for the connection to dispatch.
bool applyOnWorker(CalendarPluginWriter *writer,
                   const QString &collectionId,
                   const QList<BackendRecord> &creates,
                   const QList<BackendRecord> &updates,
                   const QStringList &deletes)
{
    auto fut = QtConcurrent::run([=]() {
        return writer->apply(collectionId, creates, updates, deletes);
    });

    QFutureWatcher<bool> watcher;
    QSignalSpy doneSpy(&watcher, &QFutureWatcher<bool>::finished);
    watcher.setFuture(fut);

    // Spin the main event loop until the worker finishes, so the
    // backend's main-thread slot dispatch can complete.
    if (!doneSpy.wait(5000)) return false;
    return fut.result();
}

} // namespace

class TestCalendarPluginWriter : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    /// apply() returns false when no collection has been set.
    void apply_returnsFalse_whenNoCollection();

    /// apply() returns false when the calendar id can't be resolved
    /// against the collection.
    void apply_returnsFalse_whenCalendarMissing();

    /// Empty batch is a successful no-op.
    void apply_returnsTrue_emptyBatch();

    /// One create with valid iCal bytes lands an incidence in the
    /// MockBackend's storage for the given calendarId.
    void apply_create_landsIncidenceInBackend();

    /// A delete removes a previously-seeded incidence.
    void apply_delete_removesSeededIncidence();

private:
    std::unique_ptr<MockBackend>            m_backend;
    std::unique_ptr<StubCalendarCollection> m_collection;
    KCalendarCore::MemoryCalendar          *m_calendar = nullptr;
};

void TestCalendarPluginWriter::init()
{
    m_backend = std::make_unique<MockBackend>(QStringLiteral("backend-under-test"));
    m_collection = std::make_unique<StubCalendarCollection>(
        QStringLiteral("stub-collection"));

    // Per libkalburator/CLAUDE.md: StubCalendarCollection must hold a
    // MemoryCalendar with setId(calendarId) matching the calendar id
    // the writer is asked to apply to.
    auto *cal = new KCalendarCore::MemoryCalendar(QTimeZone::utc());
    cal->setId(QString::fromLatin1(kCalendarId));
    m_collection->addCalendarWithId(QString::fromLatin1(kCalendarId), cal);
    m_calendar = cal;

    // MockBackend needs the calendarId to exist in its hash so
    // pushItems() has somewhere to land. createCalendar() seeds it.
    QVERIFY(m_backend->createCalendar(QStringLiteral("stub-collection"),
                                      QString::fromLatin1(kCalendarId),
                                      QStringLiteral("Cal 1")));
}

void TestCalendarPluginWriter::cleanup()
{
    m_calendar = nullptr;
    m_collection.reset();
    m_backend.reset();
}

void TestCalendarPluginWriter::apply_returnsFalse_whenNoCollection()
{
    CalendarPluginWriter writer(m_backend.get());
    // intentionally no setCollection()
    auto evt = makeEvent(QStringLiteral("evt-1"), QStringLiteral("E1"));
    BackendRecord r;
    r.id = QStringLiteral("evt-1");
    r.data = icalBytes(evt);

    QVERIFY(!writer.apply(QString::fromLatin1(kCalendarId),
                          {r}, {}, {}));
}

void TestCalendarPluginWriter::apply_returnsFalse_whenCalendarMissing()
{
    CalendarPluginWriter writer(m_backend.get());
    writer.setCollection(m_collection.get());

    auto evt = makeEvent(QStringLiteral("evt-1"), QStringLiteral("E1"));
    BackendRecord r;
    r.id = QStringLiteral("evt-1");
    r.data = icalBytes(evt);

    // Wrong collectionId — the collection has only "cal-1".
    QVERIFY(!writer.apply(QStringLiteral("does-not-exist"),
                          {r}, {}, {}));
}

void TestCalendarPluginWriter::apply_returnsTrue_emptyBatch()
{
    CalendarPluginWriter writer(m_backend.get());
    writer.setCollection(m_collection.get());

    QVERIFY(writer.apply(QString::fromLatin1(kCalendarId), {}, {}, {}));
}

void TestCalendarPluginWriter::apply_create_landsIncidenceInBackend()
{
    CalendarPluginWriter writer(m_backend.get());
    writer.setCollection(m_collection.get());

    const QString uid = QStringLiteral("evt-create-1");
    auto evt = makeEvent(uid, QStringLiteral("Created via writer"));
    BackendRecord r;
    r.id = uid;
    r.type = QStringLiteral("event");
    r.data = icalBytes(evt);

    QVERIFY(applyOnWorker(&writer, QString::fromLatin1(kCalendarId),
                          {r}, {}, {}));

    // The backend should now have an incidence for that calendarId+uid.
    auto stored = m_backend->incidence(QString::fromLatin1(kCalendarId), uid);
    QVERIFY2(stored, "MockBackend should hold the created incidence");
    QCOMPARE(stored->summary(), QStringLiteral("Created via writer"));
}

void TestCalendarPluginWriter::apply_delete_removesSeededIncidence()
{
    // Pre-seed an incidence so delete has something to remove.
    const QString uid = QStringLiteral("evt-delete-1");
    auto seeded = makeEvent(uid, QStringLiteral("Will be deleted"));
    m_backend->addIncidence(QString::fromLatin1(kCalendarId), seeded);
    QVERIFY(m_backend->incidence(QString::fromLatin1(kCalendarId), uid));

    CalendarPluginWriter writer(m_backend.get());
    writer.setCollection(m_collection.get());

    QVERIFY(applyOnWorker(&writer, QString::fromLatin1(kCalendarId),
                          {}, {}, QStringList{uid}));

    // Gone after delete.
    QVERIFY2(!m_backend->incidence(QString::fromLatin1(kCalendarId), uid),
             "Incidence should be gone after delete");
}

QTEST_MAIN(TestCalendarPluginWriter)
#include "tst_calendar_plugin_writer.moc"
