// tests/calendar/tst_backend_concurrency_capability.cpp
// Parallel-sync Task 5 — the backend-side concurrency veto.
//
// A backend declares how many operations it can usefully have in flight
// across all its collections. The engine caps per resourceId() using this,
// so a serial-link or rate-limited backend can veto a host's concurrency
// setting downward regardless of what the host asked for.
//
// Lives in tests/calendar/ (not tests/sync/) because RemoteCalendarBackend
// needs Qt6::Network + KF6::CalendarCore, which only this directory's
// kalburator_add_calendar_test() helper links; tests/sync/'s
// kalburator_add_sync_test() links neither. RemoteCalendarBackend has no
// default constructor (it always needs a URL/username/password), but its
// constructor does no network I/O — only local state + a lazily-opened
// content cache — so a dummy URL is safe to construct here without a live
// CalDAV server, matching how tests/calendar/tst_remotecalendarbackend.cpp
// constructs its backend instances.

#include <QtTest/QtTest>
#include <QObject>
#include <QUrl>

#include "mockbackend.h"
#include "remotecalendarbackend.h"

using Kalburator::Sync::MockBackend;
using Kalburator::Sync::RemoteCalendarBackend;

class TestBackendConcurrencyCapability : public QObject
{
    Q_OBJECT

private slots:
    void testDefaultIsUnlimited()
    {
        MockBackend backend;
        QCOMPARE(backend.maxConcurrentOperations(), 0);
    }

    void testRemoteCalendarBackendCapsAtFour()
    {
        // Safely under QNetworkAccessManager's 6-connections-per-host
        // default, leaving headroom for the app's other traffic and for
        // server-side rate limits. No network server needed — the
        // constructor performs no I/O and maxConcurrentOperations() is a
        // trivial override.
        RemoteCalendarBackend backend(QUrl(QStringLiteral("http://127.0.0.1:5232/testuser1/")),
                                       QStringLiteral("testuser1"),
                                       QStringLiteral("password1"));
        QCOMPARE(backend.maxConcurrentOperations(), 4);
    }
};

QTEST_MAIN(TestBackendConcurrencyCapability)
#include "tst_backend_concurrency_capability.moc"
