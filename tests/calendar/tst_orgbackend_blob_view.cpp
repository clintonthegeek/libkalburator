// tests/calendar/tst_orgbackend_blob_view.cpp
// Phase D Task 14 — IBlobBackend smoke test for OrgBackend.
//
// This test is compiled only when KALBURATOR_HAVE_ORG_IO=ON (controlled by
// CMakeLists.txt). In the default build profile (ORG_IO=OFF) the test target
// is not defined and the test is silently skipped.
//
// Tests verify:
//   1. OrgBackend* casts to IBlobBackend* successfully.
//   2. Identity methods return non-empty values.
//   3. availableCollections() is empty when the root has no .org files.
//   (Full round-trip tests require the OrgMode parser and are deferred to
//    the ORG_IO=ON CI job.)

#include <QtTest>

#include "orgbackend.h"
#include "iblobbackend.h"

using namespace Kalburator::Sync;

class TestOrgBackendBlobView : public QObject
{
    Q_OBJECT

private slots:
    void castSucceeds();
    void identityMethods_returnNonEmpty();
    void availableCollections_emptyForEmptyDir();
};

void TestOrgBackendBlobView::castSucceeds()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    OrgBackend backend(root.path());
    auto *blob = static_cast<IBlobBackend *>(&backend);
    QVERIFY(blob != nullptr);
}

void TestOrgBackendBlobView::identityMethods_returnNonEmpty()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    OrgBackend backend(root.path());
    auto *blob = static_cast<IBlobBackend *>(&backend);

    QVERIFY(!blob->backendId().isEmpty());
    QVERIFY(!blob->displayName().isEmpty());
    QVERIFY(blob->isAvailable());  // dir exists
}

void TestOrgBackendBlobView::availableCollections_emptyForEmptyDir()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    OrgBackend backend(root.path());
    auto *blob = static_cast<IBlobBackend *>(&backend);

    // No .org files in empty dir
    QVERIFY(blob->availableCollections().isEmpty());
}

QTEST_GUILESS_MAIN(TestOrgBackendBlobView)
#include "tst_orgbackend_blob_view.moc"
