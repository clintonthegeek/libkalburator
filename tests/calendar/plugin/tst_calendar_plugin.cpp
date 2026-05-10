#include <QTest>
#include <QColor>
#include <QVariantMap>

#include "calendardomainplugin.h"
#include "domainregistry.h"
#include "propertycatalogue.h"
#include "shape.h"
#include "syncbackend.h"
#include "transformationregistry.h"

using namespace Kalburator::Calendar;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::DomainRegistry;
using Kalburator::Shape::TransformationRegistry;

// ---------------------------------------------------------------------------
// Minimal inline backend for Task 6 collection-property tests.
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
                   const QMap<QString, QString> &,
                   const Kalburator::Sync::TranscodingPlan &) override {}
    void removeItem(const QString &, const QString &) override {}

    Kalburator::Sync::FetchOperation  *fetchItems(const QString &) override { return nullptr; }
    Kalburator::Sync::PushOperation   *pushItems(const QString &,
                                                  const QList<KCalendarCore::Incidence::Ptr> &,
                                                  const Kalburator::Sync::TranscodingPlan &) override { return nullptr; }
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
    void cleanup()
    {
        DomainRegistry::instance().clear();
        TransformationRegistry::instance().clear();
    }

    void canonicalShapeIsCalendarIcal()
    {
        const CalendarDomainPlugin plugin;
        const Kalburator::Shape::Shape expected{ DomainId{"calendar"}, EncodingId{"ical"} };
        QCOMPARE(plugin.canonicalShape(), expected);
    }

    void domainIsCalendar()
    {
        const CalendarDomainPlugin plugin;
        QCOMPARE(plugin.domain().toString(), QString{"calendar"});
    }

    void canonicalCatalogueHasUid()
    {
        const CalendarDomainPlugin plugin;
        const auto cat = plugin.canonicalCatalogue();
        QVERIFY(cat.hasProperty(PropertyId{"uid"}));
    }

    void canonicalCatalogueHasSummary()
    {
        const CalendarDomainPlugin plugin;
        const auto cat = plugin.canonicalCatalogue();
        QVERIFY(cat.hasProperty(PropertyId{"summary"}));
    }

    void registerEdgesPopulatesRegistry()
    {
        CalendarDomainPlugin plugin;
        auto& reg = TransformationRegistry::instance();
        plugin.registerEdges(reg);

        // Check canonical is declared
        const auto canonical = plugin.canonicalShape();
        QCOMPARE(reg.canonicalFor(DomainId{"calendar"}), canonical);

        // Check identity edge is present
        const auto edges = reg.edgesFrom(canonical);
        QCOMPARE(edges.size(), 1);
        QCOMPARE(edges.first().from, canonical);
        QCOMPARE(edges.first().to, canonical);
    }

    void richnessRankCanonical()
    {
        const CalendarDomainPlugin plugin;
        QCOMPARE(plugin.richnessRank(plugin.canonicalShape()), 10);
    }

    void peerShapesEmpty()
    {
        const CalendarDomainPlugin plugin;
        QVERIFY(plugin.peerShapes().isEmpty());
    }

    // ------------------------------------------------------------------
    // Task 6: collection-property hooks
    // ------------------------------------------------------------------

    void collectionPropertiesReadsColorAndDescription()
    {
        const CalendarDomainPlugin plugin;
        MinimalBackend backend;
        backend.setCalendarColor(QStringLiteral("cal-1"), QColor(Qt::blue));
        backend.setCalendarDescription(QStringLiteral("cal-1"), QStringLiteral("My calendar"));

        const QVariantMap props = plugin.collectionProperties(&backend, QStringLiteral("cal-1"));

        QVERIFY(props.contains(QStringLiteral("color")));
        QVERIFY(props.contains(QStringLiteral("description")));
        QCOMPARE(props.value(QStringLiteral("color")).value<QColor>(), QColor(Qt::blue));
        QCOMPARE(props.value(QStringLiteral("description")).toString(),
                 QStringLiteral("My calendar"));
    }

    void collectionPropertiesOmitsUnsetKeys()
    {
        // Backend returns invalid color and empty description for unknown ids
        const CalendarDomainPlugin plugin;
        MinimalBackend backend;

        const QVariantMap props = plugin.collectionProperties(&backend, QStringLiteral("cal-none"));

        QVERIFY(!props.contains(QStringLiteral("color")));
        QVERIFY(!props.contains(QStringLiteral("description")));
    }

    void collectionPropertiesHandlesNullBackend()
    {
        const CalendarDomainPlugin plugin;
        const QVariantMap props = plugin.collectionProperties(nullptr, QStringLiteral("cal-1"));
        QVERIFY(props.isEmpty());
    }

    void applyCollectionPropertiesCallsUpdateCalendar()
    {
        const CalendarDomainPlugin plugin;
        MinimalBackend backend;

        const QVariantMap props{
            {QStringLiteral("color"),       QColor(Qt::red)},
            {QStringLiteral("description"), QStringLiteral("x")},
        };
        plugin.applyCollectionProperties(&backend, QStringLiteral("cal-1"), props);

        const auto calls = backend.updateCalendarCalls();
        QCOMPARE(calls.size(), 1);
        QCOMPARE(calls.first().collectionId, QStringLiteral("cal-1"));
        QCOMPARE(calls.first().calendarId,   QStringLiteral("cal-1"));
        QCOMPARE(calls.first().properties,   props);
    }

    void applyCollectionPropertiesSkipsNullBackend()
    {
        // Should not crash; no-op
        const CalendarDomainPlugin plugin;
        plugin.applyCollectionProperties(nullptr, QStringLiteral("cal-1"),
                                         {{QStringLiteral("color"), QColor(Qt::red)}});
        // If we reach here without crash, test passes
        QVERIFY(true);
    }

    void applyCollectionPropertiesSkipsEmptyProps()
    {
        const CalendarDomainPlugin plugin;
        MinimalBackend backend;
        plugin.applyCollectionProperties(&backend, QStringLiteral("cal-1"), {});
        QCOMPARE(backend.updateCalendarCalls().size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestCalendarPlugin)
#include "tst_calendar_plugin.moc"
