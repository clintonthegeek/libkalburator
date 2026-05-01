#include <QTest>

#include "calendardomainplugin.h"
#include "domainregistry.h"
#include "propertycatalogue.h"
#include "shape.h"
#include "transformationregistry.h"

using namespace Kalburator::Calendar;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::DomainRegistry;
using Kalburator::Shape::TransformationRegistry;

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
        const KalburatorDomainCalendar plugin;
        const Kalburator::Shape::Shape expected{ DomainId{"calendar"}, EncodingId{"ical"} };
        QCOMPARE(plugin.canonicalShape(), expected);
    }

    void domainIsCalendar()
    {
        const KalburatorDomainCalendar plugin;
        QCOMPARE(plugin.domain().toString(), QString{"calendar"});
    }

    void canonicalCatalogueHasUid()
    {
        const KalburatorDomainCalendar plugin;
        const auto cat = plugin.canonicalCatalogue();
        QVERIFY(cat.hasProperty(PropertyId{"uid"}));
    }

    void canonicalCatalogueHasSummary()
    {
        const KalburatorDomainCalendar plugin;
        const auto cat = plugin.canonicalCatalogue();
        QVERIFY(cat.hasProperty(PropertyId{"summary"}));
    }

    void registerEdgesPopulatesRegistry()
    {
        KalburatorDomainCalendar plugin;
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
        const KalburatorDomainCalendar plugin;
        QCOMPARE(plugin.richnessRank(plugin.canonicalShape()), 10);
    }

    void peerShapesEmpty()
    {
        const KalburatorDomainCalendar plugin;
        QVERIFY(plugin.peerShapes().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestCalendarPlugin)
#include "tst_calendar_plugin.moc"
