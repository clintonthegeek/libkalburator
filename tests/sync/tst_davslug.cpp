// Task 2.1 — pins davSlugFromUrl(), the stable per-account calendar key
// derived from the last path segment of a DAV URL. Pure QString-in/
// QString-out helper, no fixture dependencies.

#include <QtTest>

#include "davslug.h"

using namespace Kalburator::Sync;

class TestDavSlug : public QObject
{
    Q_OBJECT
private slots:
    void testExtractsLastSegment();
};

void TestDavSlug::testExtractsLastSegment()
{
    QCOMPARE(Kalburator::Sync::davSlugFromUrl(
        "https://u@host/remote.php/dav/calendars/user/personal/"), QStringLiteral("personal"));
    QCOMPARE(Kalburator::Sync::davSlugFromUrl(
        "http://localhost:5232/testuser1/acsw"), QStringLiteral("acsw"));
    QCOMPARE(Kalburator::Sync::davSlugFromUrl(""), QString());
}

QTEST_GUILESS_MAIN(TestDavSlug)
#include "tst_davslug.moc"
