// Live primer probe — NOT a CI test. Drives CalDavProvider against a real
// Radicale server (PlanStan's localhost:5232 test instance) to demonstrate the
// v0.63 discovery primer on a large, real calendar set. Read-only.
//
// Pairs with journalctl -u radicale PROPFIND counting in the runner script:
// it prints UTC markers so the wrapper can attribute server-side PROPFINDs to
// the connect-walk phase vs the (should-be-zero) per-backend loadCalendars loop.
//
// Env overrides: RADICALE_URL, RADICALE_USER, RADICALE_PASS.

#include <QCoreApplication>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QDateTime>
#include <QSignalSpy>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include "backendconfiguration.h"
#include "caldavprovider.h"
#include "collectioninfo.h"
#include "remotecalendarbackend.h"
#include "syncbackend.h"

using namespace Kalburator::Sync;

static QTextStream out(stdout);

static QString marker(const QString &label)
{
    const QString ts = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    out << "MARKER " << label << " " << ts << "\n";
    out.flush();
    return ts;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString url  = qEnvironmentVariable("RADICALE_URL",  "http://127.0.0.1:5232/testuser1/");
    const QString user = qEnvironmentVariable("RADICALE_USER", "testuser1");
    const QString pass = qEnvironmentVariable("RADICALE_PASS", "password1");

    BackendConfiguration cfg;
    cfg.id = QStringLiteral("live-radicale");
    cfg.type = QStringLiteral("caldav");
    cfg.connectionParams.insert(QStringLiteral("url"), url);
    cfg.connectionParams.insert(QStringLiteral("username"), user);
    cfg.connectionParams.insert(QStringLiteral("password"), pass);

    CalDavProvider provider;
    provider.load(cfg);

    marker("CONNECT_START");
    QFuture<bool> fut = provider.connect();
    {
        QFutureWatcher<bool> w;
        QEventLoop loop;
        QObject::connect(&w, &QFutureWatcher<bool>::finished, &loop, &QEventLoop::quit);
        w.setFuture(fut);
        if (!fut.isFinished()) loop.exec();
    }
    marker("CONNECT_DONE");

    if (!fut.result()) {
        out << "RESULT connect_failed\n";
        return 2;
    }

    const auto cols = provider.collections();
    out << "M_CALENDARS " << cols.size() << "\n";
    out.flush();

    // Settle so the connect-walk PROPFINDs are clearly timestamped before the loop.
    QThread::msleep(1200);

    // ---- The measured phase: open a backend for EVERY calendar and load it. ----
    marker("PRIMED_LOOP_START");
    int loaded = 0;
    int discovered = 0;
    int finishedOk = 0;
    for (const auto &c : cols) {
        auto backend = provider.createBackend(c.id);
        if (!backend) continue;
        auto *remote = dynamic_cast<RemoteCalendarBackend *>(backend.get());
        if (!remote) continue;

        QSignalSpy discSpy(remote, SIGNAL(calendarDiscovered(QString, QString)));
        QSignalSpy finSpy(remote, SIGNAL(loadCalendarsFinished(QString, bool, QString)));
        remote->loadCalendars(c.id);

        // Primed path is synchronous. Give a tiny spin only as a safety net; if a
        // network walk were (wrongly) taken, this would NOT complete synchronously
        // and the journal would show the PROPFIND.
        ++loaded;
        discovered += discSpy.count();
        if (finSpy.count() == 1 && finSpy.first().at(1).toBool()) ++finishedOk;
    }
    marker("PRIMED_LOOP_DONE");

    out << "PRIMED loaded=" << loaded
        << " discovered=" << discovered
        << " finishedOk=" << finishedOk << "\n";
    out.flush();

    // ---- Contrast: ONE unprimed backend loadCalendars (the old per-backend cost). ----
    // Proves the fallback still works live AND shows what each of N backends used
    // to do: a single loadCalendars re-enumerates the WHOLE server.
    marker("UNPRIMED_ONE_START");
    {
        RemoteCalendarBackend raw(QUrl(url), user, pass);
        if (!cols.isEmpty())
            raw.registerCalendarUrl(cols.first().id, url + cols.first().id + QStringLiteral("/"));
        QSignalSpy discSpy(&raw, SIGNAL(calendarDiscovered(QString, QString)));
        QSignalSpy finSpy(&raw, SIGNAL(loadCalendarsFinished(QString, bool, QString)));
        raw.loadCalendars(cols.isEmpty() ? QStringLiteral("x") : cols.first().id);
        QEventLoop loop;
        QTimer::singleShot(8000, &loop, &QEventLoop::quit);
        QObject::connect(&raw, SIGNAL(loadCalendarsFinished(QString, bool, QString)),
                         &loop, SLOT(quit()));
        if (finSpy.count() == 0) loop.exec();
        out << "UNPRIMED one_loadCalendars discovered=" << discSpy.count() << "\n";
    }
    marker("UNPRIMED_ONE_DONE");
    out.flush();

    return 0;
}
