// Plan 7b T1 — protective pins for LocalBackend's write paths.
//
// startSync (the AsyncFileWriter path), removeItem, and updateCalendar had no
// default-lane coverage; PlanStan staging reaches startSync through SyncBackend*
// in production. Plan 7b T3/T4 touch their internals; these tests pin the
// externally observable contract first:
//
//   - startSync signal sequence (writeStarted -> progress -> syncCompleted,
//     async via the worker-thread writer) + on-disk effects, deletions applied.
//   - removeItem file deletion + missing-file no-op.
//   - updateCalendar(QVariantMap) VDir metadata round-trip + calendarUpdated.
//   - discoveredWritable's "readonly" marker contract.

#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>

#include "calendarmetadatamanager.h"
#include "localbackend.h"
#include "syncbackend.h"

using namespace Kalburator::Sync;

namespace {

KCalendarCore::Incidence::Ptr makeEvent(const QString &uid, const QString &summary)
{
    KCalendarCore::Event::Ptr event(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(12, 0), QTimeZone::utc()));
    event->setDtEnd(QDateTime(QDate(2026, 6, 1), QTime(13, 0), QTimeZone::utc()));
    return event;
}

} // namespace

class TstLocalBackendWritePaths : public QObject
{
    Q_OBJECT

private slots:
    void startSync_creations_write_files_with_signal_contract();
    void startSync_deletions_remove_files();
    void startSync_empty_stages_completes_immediately();
    void startSync_null_calendar_completes();
    void removeItem_deletes_file();
    void removeItem_missing_file_is_noop();
    void updateCalendar_roundtrips_metadata();
    void discoveredWritable_respects_readonly_marker();
};

void TstLocalBackendWritePaths::startSync_creations_write_files_with_signal_contract()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    LocalBackend backend(root.path());
    const QString calId = QStringLiteral("personal");

    KCalendarCore::MemoryCalendar cal(QTimeZone::utc());
    cal.setId(calId);

    const QList<KCalendarCore::Incidence::Ptr> creations{
        makeEvent(QStringLiteral("lb-create-1"), QStringLiteral("First")),
        makeEvent(QStringLiteral("lb-create-2"), QStringLiteral("Second")),
    };

    QSignalSpy startedSpy(&backend, &LocalBackend::writeStarted);
    QSignalSpy progressSpy(&backend, &LocalBackend::writeProgressChanged);
    QSignalSpy completedSpy(&backend, &LocalBackend::syncCompleted);

    backend.startSync(QStringLiteral("personal-coll"), &cal, creations, {}, {});

    // writeStarted fires synchronously before the async writes run.
    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(startedSpy.first().at(0).toString(), calId);
    QCOMPARE(startedSpy.first().at(1).toInt(), 2);

    // Completion arrives from the worker-thread writer via queued signals.
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 8000);
    QCOMPARE(completedSpy.first().at(0).toString(), QStringLiteral("personal-coll"));
    QVERIFY(progressSpy.count() >= 1);

    const QDir calDir(root.filePath(calId));
    QVERIFY(calDir.exists(QStringLiteral("lb-create-1.ics")));
    QVERIFY(calDir.exists(QStringLiteral("lb-create-2.ics")));

    // Written payloads are real iCal carrying the incidence.
    QFile f(calDir.filePath(QStringLiteral("lb-create-1.ics")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray data = f.readAll();
    QVERIFY(data.contains("BEGIN:VCALENDAR"));
    QVERIFY(data.contains("lb-create-1"));
}

void TstLocalBackendWritePaths::startSync_deletions_remove_files()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    const QString calId = QStringLiteral("personal");
    QDir().mkpath(root.filePath(calId));
    const QString icsPath = root.filePath(calId + QStringLiteral("/lb-del-1.ics"));
    {
        QFile f(icsPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:lb-del-1\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
    }

    LocalBackend backend(root.path());
    KCalendarCore::MemoryCalendar cal(QTimeZone::utc());
    cal.setId(calId);

    QSignalSpy completedSpy(&backend, &LocalBackend::syncCompleted);

    const QMap<QString, QString> deletions{{QStringLiteral("lb-del-1"), QString()}};
    backend.startSync(QStringLiteral("personal-coll"), &cal, {}, {}, deletions);

    // Deletions are synchronous; with no writes staged, completion is immediate.
    QCOMPARE(completedSpy.count(), 1);
    QVERIFY(!QFile::exists(icsPath));
}

void TstLocalBackendWritePaths::startSync_empty_stages_completes_immediately()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    LocalBackend backend(root.path());
    KCalendarCore::MemoryCalendar cal(QTimeZone::utc());
    cal.setId(QStringLiteral("personal"));

    QSignalSpy startedSpy(&backend, &LocalBackend::writeStarted);
    QSignalSpy completedSpy(&backend, &LocalBackend::syncCompleted);

    backend.startSync(QStringLiteral("personal-coll"), &cal, {}, {}, {});

    QCOMPARE(completedSpy.count(), 1);
    QCOMPARE(completedSpy.first().at(0).toString(), QStringLiteral("personal-coll"));
    QCOMPARE(startedSpy.count(), 0);
}

void TstLocalBackendWritePaths::startSync_null_calendar_completes()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    LocalBackend backend(root.path());
    QSignalSpy completedSpy(&backend, &LocalBackend::syncCompleted);

    backend.startSync(QStringLiteral("ghost-coll"), nullptr, {}, {}, {});

    QCOMPARE(completedSpy.count(), 1);
    QCOMPARE(completedSpy.first().at(0).toString(), QStringLiteral("ghost-coll"));
}

void TstLocalBackendWritePaths::removeItem_deletes_file()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    const QString calId = QStringLiteral("personal");
    QDir().mkpath(root.filePath(calId));
    const QString icsPath = root.filePath(calId + QStringLiteral("/lb-rm-1.ics"));
    {
        QFile f(icsPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:lb-rm-1\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
    }

    LocalBackend backend(root.path());
    backend.removeItem(calId, QStringLiteral("lb-rm-1"));
    QVERIFY(!QFile::exists(icsPath));
}

void TstLocalBackendWritePaths::removeItem_missing_file_is_noop()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    const QString calId = QStringLiteral("personal");
    QDir().mkpath(root.filePath(calId));

    LocalBackend backend(root.path());
    // Must not crash, create files, or remove the directory.
    backend.removeItem(calId, QStringLiteral("never-existed"));
    QVERIFY(QDir(root.filePath(calId)).exists());
    QVERIFY(QDir(root.filePath(calId))
                .entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty());
}

void TstLocalBackendWritePaths::updateCalendar_roundtrips_metadata()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    LocalBackend backend(root.path());
    const QString calId = QStringLiteral("projects");
    QVERIFY(backend.createCalendar(QStringLiteral("coll-1"), calId,
                                   QStringLiteral("Projects")));

    QSignalSpy updatedSpy(&backend, &LocalBackend::calendarUpdated);

    const QColor red(Qt::red);
    QVariantMap props;
    props.insert(QStringLiteral("displayName"), QStringLiteral("Renamed"));
    props.insert(QStringLiteral("color"), red);
    props.insert(QStringLiteral("description"), QStringLiteral("All the projects"));
    props.insert(QStringLiteral("displayOrder"), 7);

    QVERIFY(backend.updateCalendar(QStringLiteral("coll-1"), calId, props));
    QCOMPARE(updatedSpy.count(), 1);

    // Public override getters reflect the VDir files...
    QCOMPARE(backend.calendarColor(calId), red);
    QCOMPARE(backend.calendarDescription(calId), QStringLiteral("All the projects"));

    // ...and the on-disk VDirSyncer metadata is what a fresh reader sees.
    CalendarMetadataManager onDisk(root.filePath(calId));
    QCOMPARE(onDisk.displayName(), QStringLiteral("Renamed"));
    QCOMPARE(onDisk.color(), red);
    QCOMPARE(onDisk.description(), QStringLiteral("All the projects"));
    QCOMPARE(onDisk.order(), 7);
}

void TstLocalBackendWritePaths::discoveredWritable_respects_readonly_marker()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    const QString calId = QStringLiteral("personal");
    QDir().mkpath(root.filePath(calId));

    LocalBackend backend(root.path());
    QVERIFY(backend.discoveredWritable(calId));

    QFile marker(root.filePath(calId + QStringLiteral("/ReadOnly")));
    QVERIFY(marker.open(QIODevice::WriteOnly));
    marker.close();

    // Marker match is case-insensitive.
    QVERIFY(!backend.discoveredWritable(calId));
}

QTEST_GUILESS_MAIN(TstLocalBackendWritePaths)
#include "tst_localbackend_writepaths.moc"
