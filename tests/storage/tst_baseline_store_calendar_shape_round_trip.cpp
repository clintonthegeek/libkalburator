// tests/storage/tst_baseline_store_calendar_shape_round_trip.cpp
//
// Phase K.5 T10: moved from tests/calendar/tst_calendar_baseline_store.cpp.
// Tests the calendar-shape round-trip behavior of the unified
// Storage::BaselineStore using the v3 mapping-keyed API.

#include <QtTest>
#include <QTemporaryDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

#include "baselinestore.h"
#include "canonicalrecord.h"
#include "shape.h"

using Kalburator::Storage::BaselineStore;

namespace {

inline Kalburator::Shape::CanonicalRecord calendarTestRec(const QString &uid,
                                                          const QString &ical)
{
    Kalburator::Shape::CanonicalRecord rec;
    rec.recordId = uid;
    rec.shape    = Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("calendar")},
        Kalburator::Shape::EncodingId{QStringLiteral("ical")}};
    rec.data     = ical.toUtf8();
    return rec;
}

inline QString recData(const std::optional<Kalburator::Shape::CanonicalRecord> &opt)
{
    return opt.has_value() ? QString::fromUtf8(opt->data) : QString{};
}

} // namespace

class TstBaselineStoreCalendarShapeRoundTrip : public QObject
{
    Q_OBJECT
private slots:
    void roundTrip_singleBaseline();
    void bulkSet_returnsAll();
    void removePerMapping_clearsOnlyThatMapping();
    void propertyBaseline_isolatedPerCalendar();
    void hasBaselines_falseWhenEmpty();
    void persistsAcrossReopen();
    void lastSyncTime_roundTrip();

private:
    QTemporaryDir m_dir;
    QString dbPath() const { return m_dir.filePath(QStringLiteral("test.kalburator-sync.db")); }
};

void TstBaselineStoreCalendarShapeRoundTrip::roundTrip_singleBaseline()
{
    BaselineStore store(dbPath());
    QVERIFY(store.isOpen());
    QVERIFY(store.setBaselineV3(QStringLiteral("m1"), calendarTestRec(QStringLiteral("uid-1"), QStringLiteral("ICAL-TEXT"))));
    QCOMPARE(recData(store.baselineV3(QStringLiteral("m1"), QStringLiteral("uid-1"))), QStringLiteral("ICAL-TEXT"));
    QCOMPARE(recData(store.baselineV3(QStringLiteral("m1"), QStringLiteral("uid-missing"))), QString());
}

void TstBaselineStoreCalendarShapeRoundTrip::bulkSet_returnsAll()
{
    BaselineStore store(dbPath());
    QVERIFY(store.isOpen());

    // Use a distinct mappingId to avoid contamination from other test slots
    // that share the same persistent database file (m_dir is class-level).
    const QString mappingId = QStringLiteral("bulk-m1");
    QVERIFY(store.setBaselineV3(mappingId, calendarTestRec(QStringLiteral("u1"), QStringLiteral("ICAL-1"))));
    QVERIFY(store.setBaselineV3(mappingId, calendarTestRec(QStringLiteral("u2"), QStringLiteral("ICAL-2"))));
    QVERIFY(store.setBaselineV3(mappingId, calendarTestRec(QStringLiteral("u3"), QStringLiteral("ICAL-3"))));

    QHash<QString, QString> result;
    for (const auto &rec : store.baselinesForMappingV3(mappingId))
        result.insert(rec.recordId, QString::fromUtf8(rec.data));

    QCOMPARE(result.size(), 3);
    QCOMPARE(result.value(QStringLiteral("u1")), QStringLiteral("ICAL-1"));
    QCOMPARE(result.value(QStringLiteral("u2")), QStringLiteral("ICAL-2"));
    QCOMPARE(result.value(QStringLiteral("u3")), QStringLiteral("ICAL-3"));
}

void TstBaselineStoreCalendarShapeRoundTrip::removePerMapping_clearsOnlyThatMapping()
{
    BaselineStore store(dbPath());
    QVERIFY(store.isOpen());

    QVERIFY(store.setBaselineV3(QStringLiteral("m1"), calendarTestRec(QStringLiteral("uid-a"), QStringLiteral("ICAL-A"))));
    QVERIFY(store.setBaselineV3(QStringLiteral("m2"), calendarTestRec(QStringLiteral("uid-b"), QStringLiteral("ICAL-B"))));

    QVERIFY(store.clearMappingV3(QStringLiteral("m1")));

    // m1 is gone
    QVERIFY(store.baselinesForMappingV3(QStringLiteral("m1")).isEmpty());

    // m2 is intact
    const auto m2recs = store.baselinesForMappingV3(QStringLiteral("m2"));
    QCOMPARE(m2recs.size(), 1);
    QCOMPARE(recData(store.baselineV3(QStringLiteral("m2"), QStringLiteral("uid-b"))), QStringLiteral("ICAL-B"));
}

void TstBaselineStoreCalendarShapeRoundTrip::propertyBaseline_isolatedPerCalendar()
{
    BaselineStore store(dbPath());
    QVERIFY(store.isOpen());

    // Store property baselines as QVariantMap via setCollectionBaseline.
    QVERIFY(store.setCollectionBaseline(QStringLiteral("m1"), QStringLiteral("cal-A"),
                                        QVariantMap{{QStringLiteral("color"), QStringLiteral("red")}}));
    QVERIFY(store.setCollectionBaseline(QStringLiteral("m1"), QStringLiteral("cal-B"),
                                        QVariantMap{{QStringLiteral("color"), QStringLiteral("blue")}}));

    QCOMPARE(store.collectionBaseline(QStringLiteral("m1"), QStringLiteral("cal-A"))
                 .value(QStringLiteral("color")), QStringLiteral("red"));
    QCOMPARE(store.collectionBaseline(QStringLiteral("m1"), QStringLiteral("cal-B"))
                 .value(QStringLiteral("color")), QStringLiteral("blue"));

    // Removing cal-A doesn't affect cal-B
    QVERIFY(store.removeCollectionBaseline(QStringLiteral("m1"), QStringLiteral("cal-A")));
    QVERIFY(store.collectionBaseline(QStringLiteral("m1"), QStringLiteral("cal-A")).isEmpty());
    QCOMPARE(store.collectionBaseline(QStringLiteral("m1"), QStringLiteral("cal-B"))
                 .value(QStringLiteral("color")), QStringLiteral("blue"));
}

void TstBaselineStoreCalendarShapeRoundTrip::hasBaselines_falseWhenEmpty()
{
    BaselineStore store(dbPath());
    QVERIFY(store.isOpen());

    // Fresh store: no baselines for any mapping
    QVERIFY(store.baselinesForMappingV3(QStringLiteral("m1")).isEmpty());

    // After writing one baseline it is non-empty
    QVERIFY(store.setBaselineV3(QStringLiteral("m1"), calendarTestRec(QStringLiteral("uid-1"), QStringLiteral("ICAL-1"))));
    QVERIFY(!store.baselinesForMappingV3(QStringLiteral("m1")).isEmpty());
}

void TstBaselineStoreCalendarShapeRoundTrip::persistsAcrossReopen()
{
    const QString path = dbPath();

    // Write a baseline and let the store go out of scope (destructor closes the connection)
    {
        BaselineStore store(path);
        QVERIFY(store.isOpen());
        QVERIFY(store.setBaselineV3(QStringLiteral("m1"), calendarTestRec(QStringLiteral("uid-persist"), QStringLiteral("ICAL-PERSISTED"))));
    }

    // Open a new store on the same path and verify the data survived
    BaselineStore store2(path);
    QVERIFY(store2.isOpen());
    QCOMPARE(recData(store2.baselineV3(QStringLiteral("m1"), QStringLiteral("uid-persist"))), QStringLiteral("ICAL-PERSISTED"));
    QVERIFY(!store2.baselinesForMappingV3(QStringLiteral("m1")).isEmpty());
}

void TstBaselineStoreCalendarShapeRoundTrip::lastSyncTime_roundTrip()
{
    BaselineStore store(dbPath());
    QVERIFY(store.isOpen());

    // No time set yet — should return invalid datetime
    QVERIFY(!store.lastSyncTime(QStringLiteral("m1")).isValid());

    // Store uses Qt::ISODate which has 1-second precision; truncate to seconds
    const QDateTime before = QDateTime::currentDateTimeUtc();
    QVERIFY(store.setLastSyncTime(QStringLiteral("m1"), before));

    const QDateTime retrieved = store.lastSyncTime(QStringLiteral("m1"));
    QVERIFY(retrieved.isValid());
    // The stored value should be within 1 second of the set value
    // (ISO format truncates sub-second precision)
    QVERIFY(qAbs(retrieved.secsTo(before)) <= 1);
}

QTEST_GUILESS_MAIN(TstBaselineStoreCalendarShapeRoundTrip)
#include "tst_baseline_store_calendar_shape_round_trip.moc"
