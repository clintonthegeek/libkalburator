#include <QTest>
#include <QJsonArray>
#include <QJsonObject>
#include "outlinediffer.h"
#include "outlinemerger.h"
#include "canonenvelope.h"
#include "conflictpolicy.h"

using namespace Kalburator::Shape;
using Kalburator::Outline::OutlineDiffer;
using Kalburator::Outline::OutlineMerger;
using Kalburator::Conflict::ConflictPolicy;

static CanonicalRecord rec(const QJsonObject& body, const QString& id = "r1")
{
    CanonicalRecord r;
    r.shape = Shape{ DomainId{"outline"}, EncodingId{"canon"} };
    r.data = CanonEnvelope::serialize(body);
    r.recordId = id;
    return r;
}

class TestOutlineDiffer : public QObject {
    Q_OBJECT
private slots:
    void equalTreesReportNoDiff();
    void changedChildReportsChildrenProperty();
    void mergerPicksChangedSide();
};

void TestOutlineDiffer::equalTreesReportNoDiff()
{
    QJsonObject body{ {"title","L"}, {"children", QJsonArray{ QJsonObject{{"text","a"}} }} };
    OutlineDiffer d;
    QVERIFY(d.equal(rec(body), rec(body)));
    QVERIFY(d.diff(rec(body), rec(body)).isEmpty());
}

void TestOutlineDiffer::changedChildReportsChildrenProperty()
{
    QJsonObject base{ {"title","L"}, {"children", QJsonArray{ QJsonObject{{"text","a"}} }} };
    QJsonObject mod { {"title","L"}, {"children", QJsonArray{ QJsonObject{{"text","b"}} }} };
    OutlineDiffer d;
    QVERIFY(!d.equal(rec(mod), rec(base)));
    QVERIFY(d.diff(rec(mod), rec(base)).contains(PropertyId{"children"}));
}

void TestOutlineDiffer::mergerPicksChangedSide()
{
    QJsonObject baseBody{ {"title","Root"}, {"children", QJsonArray{ QJsonObject{{"text","x"}} }} };
    QJsonObject srcBody { {"title","Root"}, {"children", QJsonArray{ QJsonObject{{"text","source-changed"}} }} };
    QJsonObject tgtBody { {"title","Root"}, {"children", QJsonArray{ QJsonObject{{"text","target-changed"}} }} };

    const CanonicalRecord baseline = rec(baseBody);
    const CanonicalRecord sourceOnly = rec(srcBody);   // source changed, target == baseline
    const CanonicalRecord targetOnly = rec(tgtBody);   // target changed, source == baseline

    OutlineMerger merger;
    const ConflictPolicy policy{};

    // Only target changed: result should carry target's data.
    CanonicalRecord r1 = merger.merge(baseline, targetOnly, baseline, policy);
    QCOMPARE(r1.data, targetOnly.data);

    // Only source changed: result should carry source's data.
    CanonicalRecord r2 = merger.merge(sourceOnly, baseline, baseline, policy);
    QCOMPARE(r2.data, sourceOnly.data);

    // Both changed (conflict): source wins per deferred-policy first cut.
    CanonicalRecord r3 = merger.merge(sourceOnly, targetOnly, baseline, policy);
    QCOMPARE(r3.data, sourceOnly.data);
}

QTEST_MAIN(TestOutlineDiffer)
#include "tst_outline_differ.moc"
