// tests/calendar/tst_akonadibackend_blob_view.cpp
// Phase D Task 15 — IBlobBackend smoke test for AkonadiBackend.
//
// This test is compiled only when KALBURATOR_HAVE_AKONADI=ON (controlled by
// CMakeLists.txt). In the default build profile (AKONADI=OFF) the test target
// is not defined and the test is silently skipped.
//
// AkonadiBackend Phase D stubs do not require a running Akonadi server for the
// smoke test. The test verifies:
//   1. AkonadiBackend* casts to IBlobBackend* successfully.
//   2. backendId() and displayName() return non-empty strings.
//   3. isAvailable() returns false when constructed without a live session.
//   4. availableCollections() returns empty (no collections loaded in-process).

#include <QtTest>

#include "akonadibackend.h"
#include "iblobbackend.h"

#include <Akonadi/ServerManager>

using namespace Kalburator::Sync;

class TestAkonadiBackendBlobView : public QObject
{
    Q_OBJECT

private slots:
    void castSucceeds();
    void identityMethods_returnNonEmpty();
    void isAvailable_falseWithoutLiveSession();
    void availableCollections_emptyWithoutLoadedCollections();
};

void TestAkonadiBackendBlobView::castSucceeds()
{
    AkonadiBackend backend;
    auto *blob = static_cast<IBlobBackend *>(&backend);
    QVERIFY(blob != nullptr);
}

void TestAkonadiBackendBlobView::identityMethods_returnNonEmpty()
{
    AkonadiBackend backend;
    auto *blob = static_cast<IBlobBackend *>(&backend);

    QVERIFY(!blob->backendId().isEmpty());
    QVERIFY(!blob->displayName().isEmpty());
}

void TestAkonadiBackendBlobView::isAvailable_falseWithoutLiveSession()
{
    // This test only makes sense when Akonadi is not running. If the server is
    // active (e.g. on a KDE desktop), isAvailable() will correctly return true
    // and the "unavailable without live session" assertion cannot be tested.
    if (Akonadi::ServerManager::isRunning())
        QSKIP("Akonadi server is running — cannot test unavailable state");

    AkonadiBackend backend;
    auto *blob = static_cast<IBlobBackend *>(&backend);

    // No Akonadi server running — should report unavailable.
    QVERIFY(!blob->isAvailable());
}

void TestAkonadiBackendBlobView::availableCollections_emptyWithoutLoadedCollections()
{
    AkonadiBackend backend;
    auto *blob = static_cast<IBlobBackend *>(&backend);

    QVERIFY(blob->availableCollections().isEmpty());
}

QTEST_GUILESS_MAIN(TestAkonadiBackendBlobView)
#include "tst_akonadibackend_blob_view.moc"
