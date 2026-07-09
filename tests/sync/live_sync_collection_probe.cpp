// Live probe — NOT a CI test. Drives RemoteCalendarBackend directly against a
// real Radicale server to confirm E7/O36's RFC 6578 sync-collection REPORT
// path actually fires on real (not faked) server traffic. Read/write.
//
// Env overrides: RADICALE_URL, RADICALE_USER, RADICALE_PASS.
// Expects the target calendar collection to already exist and be empty (the
// runner script MKCOLs a scratch collection before invoking this).

#include <QCoreApplication>
#include <QEventLoop>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUuid>

#include "remotecalendarbackend.h"
#include "iblobbackend.h"
#include "backendrecord.h"

using namespace Kalburator::Sync;

static QTextStream out(stdout);

static QByteArray makeEventIcs(const QString &uid, const QString &summary)
{
    return QStringLiteral(
               "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
               "BEGIN:VEVENT\r\nUID:%1\r\n"
               "SUMMARY:%2\r\nDTSTART:20260601T120000Z\r\n"
               "DTEND:20260601T130000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n")
        .arg(uid, summary)
        .toUtf8();
}

static void marker(const QString &label)
{
    out << "MARKER " << label << " "
        << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) << "\n";
    out.flush();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString url  = qEnvironmentVariable("RADICALE_URL",  "http://127.0.0.1:5233/testuser1/e7cal/");
    const QString user = qEnvironmentVariable("RADICALE_USER", "testuser1");
    const QString pass = qEnvironmentVariable("RADICALE_PASS", "password1");

    QTemporaryDir cacheDir, dbDir;
    if (!cacheDir.isValid() || !dbDir.isValid()) {
        out << "RESULT tempdir_failed\n";
        return 2;
    }
    const QString dbPath = dbDir.filePath(QStringLiteral("sync.db"));

    RemoteCalendarBackend backend(QUrl(url), user, pass);
    backend.setDbPath(dbPath);
    backend.setCacheDir(cacheDir.path());
    backend.registerCalendarUrl(QStringLiteral("E7Cal"), url);

    marker("LOAD_CALENDARS_START");
    QSignalSpy loadSpy(&backend, SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend.loadCalendars(QStringLiteral("E7Cal"));
    {
        QEventLoop loop;
        QTimer::singleShot(15000, &loop, &QEventLoop::quit);
        QObject::connect(&backend, SIGNAL(loadCalendarsFinished(QString, bool, QString)),
                         &loop, SLOT(quit()));
        if (loadSpy.count() == 0) loop.exec();
    }
    marker("LOAD_CALENDARS_DONE");
    if (loadSpy.count() == 0 || !loadSpy.first().at(1).toBool()) {
        out << "RESULT load_calendars_failed\n";
        return 2;
    }

    auto *blob = static_cast<IBlobBackend *>(&backend);

    // Seed three events via the backend's own write path.
    const QString uid0 = QStringLiteral("e7-probe-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString uid1 = QStringLiteral("e7-probe-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString uid2 = QStringLiteral("e7-probe-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (const auto &uid : {uid0, uid1, uid2}) {
        BackendRecord rec;
        rec.id = uid;
        rec.data = makeEventIcs(uid, QStringLiteral("E7 probe event"));
        backend.createRecord(QStringLiteral("E7Cal"), rec);
    }

    marker("FIRST_FETCH_START");
    const auto first = blob->loadRecords(QStringLiteral("E7Cal"));
    marker("FIRST_FETCH_DONE");
    out << "FIRST_FETCH count=" << first.size() << "\n";
    out.flush();

    // One more write — the delta this cycle's sync-collection REPORT should
    // report as the sole change.
    marker("EDIT_START");
    BackendRecord edited;
    edited.id = uid0;
    edited.data = makeEventIcs(uid0, QStringLiteral("E7 probe event EDITED"));
    backend.updateRecord(edited);
    marker("EDIT_DONE");

    marker("SECOND_FETCH_START");
    const auto second = blob->loadRecords(QStringLiteral("E7Cal"));
    marker("SECOND_FETCH_DONE");
    out << "SECOND_FETCH count=" << second.size() << "\n";
    out.flush();

    out << "RESULT ok\n";
    out.flush();
    return (first.size() == 3 && second.size() == 3) ? 0 : 1;
}
