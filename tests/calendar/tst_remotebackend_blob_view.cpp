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
    void updateRecord_modifies_existing_record();
    void updateRecord_nonexistent_id_returns_error();
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

void TestRemoteBackendBlobView::updateRecord_modifies_existing_record()
{
    // RemoteBackend::updateRecord issues a CalDAV PUT with conditional headers.
    // This requires a live (or fake) CalDAV server responding to item-level
    // verbs, which is not available in the default test profile.
    // Full coverage is gated on KALBURATOR_ENABLE_CALDAV_TESTS=ON — see FINDINGS.md.
    QSKIP("RemoteBackend updateRecord requires item-level CalDAV verbs not yet in FakeCalDavServer — see FINDINGS.md");
}

void TestRemoteBackendBlobView::updateRecord_nonexistent_id_returns_error()
{
    // Same constraint as updateRecord_modifies_existing_record: requires a
    // live CalDAV server to exercise the 404-on-missing-item path.
    QSKIP("RemoteBackend updateRecord requires item-level CalDAV verbs not yet in FakeCalDavServer — see FINDINGS.md");
}

QTEST_GUILESS_MAIN(TestRemoteBackendBlobView)
#include "tst_remotebackend_blob_view.moc"
