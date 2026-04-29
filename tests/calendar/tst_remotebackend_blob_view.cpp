// tests/calendar/tst_remotebackend_blob_view.cpp
// Phase D Task 13 — compile-only / cast-only smoke test for RemoteBackend IBlobBackend.
//
// A live CalDAV test server is NOT available in the default libkalburator build
// profile, so this test is limited to verifying:
//   1. RemoteBackend compiles with the IBlobBackend overrides.
//   2. A RemoteBackend* can be successfully upcast to IBlobBackend*.
//   3. Identity methods (backendId, displayName, isAvailable) return non-trivial
//      values without touching the network.
//   4. availableCollections() returns empty (no calendars registered — no network).
//
// Full round-trip tests against a live CalDAV server are gated on
// KALBURATOR_ENABLE_CALDAV_TESTS=ON (a future addition mirroring PlanStan's
// pattern).  The library-code change (IBlobBackend overrides) is the
// load-bearing part of Task 13; the test proves the cast and static shape.

#include <QtTest>

#include "remotebackend.h"
#include "iblobbackend.h"

using namespace Kalburator::Sync;

class TestRemoteBackendBlobView : public QObject
{
    Q_OBJECT

private slots:
    void castSucceeds();
    void identityMethods_returnNonEmpty();
    void availableCollections_emptyWithoutRegisteredCalendars();
};

void TestRemoteBackendBlobView::castSucceeds()
{
    RemoteBackend backend(QUrl(QStringLiteral("https://caldav.example.com/")),
                          QStringLiteral("user"),
                          QStringLiteral("pass"));
    auto *blob = static_cast<IBlobBackend *>(&backend);
    QVERIFY(blob != nullptr);
}

void TestRemoteBackendBlobView::identityMethods_returnNonEmpty()
{
    RemoteBackend backend(QUrl(QStringLiteral("https://caldav.example.com/")),
                          QStringLiteral("user"),
                          QStringLiteral("pass"));
    auto *blob = static_cast<IBlobBackend *>(&backend);

    QVERIFY(!blob->backendId().isEmpty());
    QVERIFY(!blob->displayName().isEmpty());
    // isAvailable() returns true when the URL is valid and non-empty
    QVERIFY(blob->isAvailable());
}

void TestRemoteBackendBlobView::availableCollections_emptyWithoutRegisteredCalendars()
{
    RemoteBackend backend(QUrl(QStringLiteral("https://caldav.example.com/")),
                          QStringLiteral("user"),
                          QStringLiteral("pass"));
    auto *blob = static_cast<IBlobBackend *>(&backend);

    // No calendars discovered / registered yet — must return empty list.
    QVERIFY(blob->availableCollections().isEmpty());
}

QTEST_GUILESS_MAIN(TestRemoteBackendBlobView)
#include "tst_remotebackend_blob_view.moc"
