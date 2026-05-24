#include <QTest>
#include <QColor>
#include <QVariantMap>

#include "calendardomaindefinition.h"
#include "calendarstockshapes.h"
#include "calendardomainoperations.h"
#include "propertycatalogue.h"
#include "shape.h"
#include "syncbackend.h"

using Kalburator::Calendar::CalendarDomainDefinition;
using Kalburator::Calendar::CalendarStockShapes;
using Kalburator::Calendar::CalendarDomainOperations;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyId;

// ---------------------------------------------------------------------------
// Minimal inline backend for collection-property tests.
// Only overrides calendarColor, calendarDescription, and updateCalendar.
// ---------------------------------------------------------------------------
namespace {

class MinimalBackend : public Kalburator::Sync::SyncBackend
{
    Q_OBJECT
public:
    // Test-setup helpers
    void setCalendarColor(const QString &id, const QColor &c)       { m_colors[id] = c; }
    void setCalendarDescription(const QString &id, const QString &d) { m_descs[id] = d; }

    // State inspectors
    struct UpdateCalendarCall {
        QString collectionId;
        QString calendarId;
        QVariantMap properties;
    };
    QList<UpdateCalendarCall> updateCalendarCalls() const { return m_updateCalls; }

    // SyncBackend overrides — only the minimum required for these tests
    QString backendType() const override { return QStringLiteral("minimal-test"); }
    QString backendId()   const override { return QStringLiteral("minimal"); }
    QString displayName() const override { return QStringLiteral("minimal"); }
    bool    isAvailable() const override { return true; }

    QColor  calendarColor(const QString &id) const override       { return m_colors.value(id); }
    QString calendarDescription(const QString &id) const override { return m_descs.value(id); }

    bool updateCalendar(const QString &collectionId,
                        const QString &calendarId,
                        const QVariantMap &properties) override
    {
        m_updateCalls.append({collectionId, calendarId, properties});
        return true;
    }

    // Pure virtuals that must be implemented but are unused in these tests
    void loadCalendars(const QString &) override {}
    void storeCalendars(const QString &,
                        const QList<KCalendarCore::MemoryCalendar*> &) override {}
    void startSync(const QString &, KCalendarCore::MemoryCalendar *,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &) override {}
    void removeItem(const QString &, const QString &) override {}

    Kalburator::Sync::FetchOperation  *fetchItems(const QString &) override { return nullptr; }
    Kalburator::Sync::PushOperation   *pushItems(const QString &,
                                                  const QList<KCalendarCore::Incidence::Ptr> &) override { return nullptr; }
    Kalburator::Sync::DeleteOperation *deleteItems(const QString &, const QStringList &) override { return nullptr; }

    QList<Kalburator::Shape::Shape> nativeShapes() const override { return {}; }

    // IBlobBackend pure virtuals
    QList<Kalburator::Sync::CollectionInfo> availableCollections() override { return {}; }
    Kalburator::Sync::CollectionInfo collectionInfo(const QString &) override { return {}; }
    QString createCollection(const Kalburator::Sync::CollectionInfo &) override { return {}; }
    QList<Kalburator::Sync::BackendRecord> loadRecords(const QString &) override { return {}; }
    std::optional<Kalburator::Sync::BackendRecord> loadRecord(const QString &) override { return {}; }
    QString createRecord(const QString &, const Kalburator::Sync::BackendRecord &) override { return {}; }
    bool updateRecord(const Kalburator::Sync::BackendRecord &) override { return false; }
    bool deleteRecord(const QString &) override { return false; }
    QList<Kalburator::Sync::BackendRecord> modifiedSince(const QString &, const QDateTime &) override { return {}; }
    QStringList deletedSince(const QString &, const QDateTime &) override { return {}; }

private:
    QHash<QString, QColor>  m_colors;
    QHash<QString, QString> m_descs;
    QList<UpdateCalendarCall> m_updateCalls;
};

} // namespace

class TestCalendarPlugin : public QObject {
    Q_OBJECT
private slots:
    void canonicalShapeIsCalendarCanon()
    {
        const CalendarDomainDefinition def;
        const Kalburator::Shape::Shape expected{ DomainId{"calendar"}, EncodingId{"canon"} };
        QCOMPARE(def.canonicalShape(), expected);
    }

    void domainIsCalendar()
    {
        const CalendarDomainDefinition def;
        QCOMPARE(def.domain().toString(), QString{"calendar"});
    }

    void canonicalCatalogueHasUid()
    {
        const CalendarDomainDefinition def;
        const auto cat = def.canonicalCatalogue();
        QVERIFY(cat.hasProperty(PropertyId{"uid"}));
    }

    void canonicalCatalogueHasCanonProperties()
    {
        const CalendarDomainDefinition def;
        const auto cat = def.canonicalCatalogue();
        // Core canon properties (schema doc §2)
        QVERIFY(cat.hasProperty(PropertyId{"summary"}));
        QVERIFY(cat.hasProperty(PropertyId{"attendees"}));
        // Plan 3 Task C1: Google/MS-only fields also present in canon
        QVERIFY(cat.hasProperty(PropertyId{"onlineMeeting"}));
        QVERIFY(cat.hasProperty(PropertyId{"guestsCanModify"}));
    }

    void stockShapesHasFiveEdges()
    {
        // Plan 3 Task C5: canon-identity + ical→canon + canon→ical (3)
        // Plan 4 Task 3: + org-ical→canon + canon→org-ical (2) = 5
        const CalendarStockShapes shapes;
        QCOMPARE(shapes.edges().size(), 5);
    }

    void stockShapesPeerContainsOrgIcal()
    {
        // Plan 4 Task 3: org-ical is registered as a less-capable peer encoding.
        const CalendarStockShapes shapes;
        const Kalburator::Shape::Shape orgIcal{ DomainId{"calendar"}, EncodingId{"org-ical"} };
        const auto peers = shapes.peerShapes();
        QVERIFY(std::any_of(peers.begin(), peers.end(),
            [&](const auto &p) { return p.first == orgIcal; }));
    }

    void stockShapesPeerContainsIcal()
    {
        const CalendarStockShapes shapes;
        const Kalburator::Shape::Shape ical{ DomainId{"calendar"}, EncodingId{"ical"} };
        const auto peers = shapes.peerShapes();
        QVERIFY(std::any_of(peers.begin(), peers.end(),
            [&](const auto &p) { return p.first == ical; }));
    }

    void richnessRankCanonical()
    {
        const CalendarDomainDefinition def;
        // Canon head should have the highest richness rank
        QCOMPARE(def.richnessRank(def.canonicalShape()), 100);
    }

    void richnessRankUnknownShapeIsZero()
    {
        const CalendarDomainDefinition def;
        const Kalburator::Shape::Shape other{ DomainId{"calendar"}, EncodingId{"other"} };
        QCOMPARE(def.richnessRank(other), 0);
    }

    // ------------------------------------------------------------------
    // Collection-property hooks — via CalendarDomainOperations
    // ------------------------------------------------------------------

    void collectionPropertiesReadsColorAndDescription()
    {
        const CalendarDomainOperations ops;
        MinimalBackend backend;
        backend.setCalendarColor(QStringLiteral("cal-1"), QColor(Qt::blue));
        backend.setCalendarDescription(QStringLiteral("cal-1"), QStringLiteral("My calendar"));

        const QVariantMap props = ops.collectionProperties(&backend, QStringLiteral("cal-1"));

        QVERIFY(props.contains(QStringLiteral("color")));
        QVERIFY(props.contains(QStringLiteral("description")));
        QCOMPARE(props.value(QStringLiteral("color")).value<QColor>(), QColor(Qt::blue));
        QCOMPARE(props.value(QStringLiteral("description")).toString(),
                 QStringLiteral("My calendar"));
    }

    void collectionPropertiesOmitsUnsetKeys()
    {
        // Backend returns invalid color and empty description for unknown ids
        const CalendarDomainOperations ops;
        MinimalBackend backend;

        const QVariantMap props = ops.collectionProperties(&backend, QStringLiteral("cal-none"));

        QVERIFY(!props.contains(QStringLiteral("color")));
        QVERIFY(!props.contains(QStringLiteral("description")));
    }

    void collectionPropertiesHandlesNullBackend()
    {
        const CalendarDomainOperations ops;
        const QVariantMap props = ops.collectionProperties(nullptr, QStringLiteral("cal-1"));
        QVERIFY(props.isEmpty());
    }

    void applyCollectionPropertiesCallsUpdateCalendar()
    {
        const CalendarDomainOperations ops;
        MinimalBackend backend;

        const QVariantMap props{
            {QStringLiteral("color"),       QColor(Qt::red)},
            {QStringLiteral("description"), QStringLiteral("x")},
        };
        ops.applyCollectionProperties(&backend, QStringLiteral("cal-1"), props);

        const auto calls = backend.updateCalendarCalls();
        QCOMPARE(calls.size(), 1);
        QCOMPARE(calls.first().collectionId, QStringLiteral("cal-1"));
        QCOMPARE(calls.first().calendarId,   QStringLiteral("cal-1"));
        QCOMPARE(calls.first().properties,   props);
    }

    void applyCollectionPropertiesSkipsNullBackend()
    {
        // Should not crash; no-op
        const CalendarDomainOperations ops;
        ops.applyCollectionProperties(nullptr, QStringLiteral("cal-1"),
                                      {{QStringLiteral("color"), QColor(Qt::red)}});
        // If we reach here without crash, test passes
        QVERIFY(true);
    }

    void applyCollectionPropertiesSkipsEmptyProps()
    {
        const CalendarDomainOperations ops;
        MinimalBackend backend;
        ops.applyCollectionProperties(&backend, QStringLiteral("cal-1"), {});
        QCOMPARE(backend.updateCalendarCalls().size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestCalendarPlugin)
#include "tst_calendar_plugin.moc"
