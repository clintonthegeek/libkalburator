// tests/calendar/differs/tst_ical_record_merger.cpp
#include <QTest>
#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>

#include "icalrecordmerger.h"
#include "canonicalrecord.h"
#include "shape.h"
#include "conflictpolicy.h"

using namespace Kalburator::Calendar;
using namespace Kalburator::Sync::QSyncCore;
using Kalburator::Shape::CanonicalRecord;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;

namespace {

static QByteArray makeIcal(const QString& uid, const QString& summary,
                            const QString& description = {})
{
    auto ev = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    ev->setUid(uid);
    ev->setSummary(summary);
    if (!description.isEmpty())
        ev->setDescription(description);
    ev->setDtStart(QDateTime(QDate(2026, 1, 1), QTime(9, 0), Qt::UTC));
    KCalendarCore::ICalFormat fmt;
    return fmt.toICalString(ev).toUtf8();
}

static CanonicalRecord makeRecord(const QString& uid, const QString& summary,
                                   const QString& description = {})
{
    CanonicalRecord rec;
    rec.shape    = Kalburator::Shape::Shape{DomainId{"calendar"}, EncodingId{"ical"}};
    rec.data     = makeIcal(uid, summary, description);
    rec.recordId = uid;
    return rec;
}

static KCalendarCore::Incidence::Ptr parseResult(const CanonicalRecord& r)
{
    KCalendarCore::ICalFormat fmt;
    return fmt.fromString(QString::fromUtf8(r.data));
}

} // namespace

class TestICalRecordMerger : public QObject
{
    Q_OBJECT

private slots:

    /// source == target == baseline → result preserves baseline value
    void bothUnchanged()
    {
        const QString uid = QStringLiteral("uid-unchanged");
        const auto base   = makeRecord(uid, QStringLiteral("Meeting"));
        const auto src    = makeRecord(uid, QStringLiteral("Meeting"));
        const auto tgt    = makeRecord(uid, QStringLiteral("Meeting"));

        IRecordMergerICal merger;
        const auto result = merger.merge(src, tgt, base, ConflictPolicy::autoSourceWins());

        const auto inc = parseResult(result);
        QVERIFY(inc);
        QCOMPARE(inc->summary(), QStringLiteral("Meeting"));
    }

    /// source changed summary, target same as baseline → result has source summary
    void sourceOnlyChange()
    {
        const QString uid = QStringLiteral("uid-src-only");
        const auto base   = makeRecord(uid, QStringLiteral("Original"));
        const auto src    = makeRecord(uid, QStringLiteral("Source Updated"));
        const auto tgt    = makeRecord(uid, QStringLiteral("Original"));

        IRecordMergerICal merger;
        const auto result = merger.merge(src, tgt, base, ConflictPolicy::autoSourceWins());

        const auto inc = parseResult(result);
        QVERIFY(inc);
        QCOMPARE(inc->summary(), QStringLiteral("Source Updated"));
    }

    /// target changed summary, source same as baseline → result has target summary
    void targetOnlyChange()
    {
        const QString uid = QStringLiteral("uid-tgt-only");
        const auto base   = makeRecord(uid, QStringLiteral("Original"));
        const auto src    = makeRecord(uid, QStringLiteral("Original"));
        const auto tgt    = makeRecord(uid, QStringLiteral("Target Updated"));

        IRecordMergerICal merger;
        const auto result = merger.merge(src, tgt, base, ConflictPolicy::autoSourceWins());

        const auto inc = parseResult(result);
        QVERIFY(inc);
        QCOMPARE(inc->summary(), QStringLiteral("Target Updated"));
    }

    /// both changed summary differently, policy autoSourceWins → result has source summary
    void bothChangedSourceWins()
    {
        const QString uid = QStringLiteral("uid-conflict-src");
        const auto base   = makeRecord(uid, QStringLiteral("Original"));
        const auto src    = makeRecord(uid, QStringLiteral("Source Version"));
        const auto tgt    = makeRecord(uid, QStringLiteral("Target Version"));

        IRecordMergerICal merger;
        const auto result = merger.merge(src, tgt, base, ConflictPolicy::autoSourceWins());

        const auto inc = parseResult(result);
        QVERIFY(inc);
        QCOMPARE(inc->summary(), QStringLiteral("Source Version"));
    }

    /// both changed summary differently, policy autoTargetWins → result has target summary
    void bothChangedTargetWins()
    {
        const QString uid = QStringLiteral("uid-conflict-tgt");
        const auto base   = makeRecord(uid, QStringLiteral("Original"));
        const auto src    = makeRecord(uid, QStringLiteral("Source Version"));
        const auto tgt    = makeRecord(uid, QStringLiteral("Target Version"));

        IRecordMergerICal merger;
        const auto result = merger.merge(src, tgt, base, ConflictPolicy::autoTargetWins());

        const auto inc = parseResult(result);
        QVERIFY(inc);
        QCOMPARE(inc->summary(), QStringLiteral("Target Version"));
    }

    /// source changed summary, target changed description → result has both changes
    void crossPropertyMerge()
    {
        const QString uid  = QStringLiteral("uid-cross");
        const auto base    = makeRecord(uid, QStringLiteral("Original"), QStringLiteral("Base desc"));
        const auto src     = makeRecord(uid, QStringLiteral("New Summary"), QStringLiteral("Base desc"));
        const auto tgt     = makeRecord(uid, QStringLiteral("Original"),   QStringLiteral("New desc"));

        IRecordMergerICal merger;
        const auto result = merger.merge(src, tgt, base, ConflictPolicy::autoSourceWins());

        const auto inc = parseResult(result);
        QVERIFY(inc);
        QCOMPARE(inc->summary(),     QStringLiteral("New Summary"));
        QCOMPARE(inc->description(), QStringLiteral("New desc"));
    }
};

QTEST_GUILESS_MAIN(TestICalRecordMerger)
#include "tst_ical_record_merger.moc"
