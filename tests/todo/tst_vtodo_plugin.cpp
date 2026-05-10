#include <QTest>

#include "tododomainplugin.h"
#include "domainregistry.h"
#include "transformationregistry.h"

using namespace Kalburator::Todo;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::DomainRegistry;
using Kalburator::Shape::TransformationRegistry;

class TestVTodoPlugin : public QObject {
    Q_OBJECT
private slots:
    void cleanup()
    {
        DomainRegistry::instance().clear();
        TransformationRegistry::instance().clear();
    }

    void canonicalShapeIsTodoIcalVTodo()
    {
        const TodoDomainPlugin plugin;
        const Kalburator::Shape::Shape expected{ DomainId{"todo"}, EncodingId{"ical-vtodo"} };
        QCOMPARE(plugin.canonicalShape(), expected);
    }

    void domainIsTodo()
    {
        const TodoDomainPlugin plugin;
        QCOMPARE(plugin.domain().toString(), QStringLiteral("todo"));
    }

    void catalogueHasRequiredProperties()
    {
        const TodoDomainPlugin plugin;
        const auto cat = plugin.canonicalCatalogue();
        QVERIFY(cat.hasProperty(PropertyId{"uid"}));
        QVERIFY(cat.hasProperty(PropertyId{"summary"}));
        QVERIFY(cat.hasProperty(PropertyId{"due"}));
        QVERIFY(cat.hasProperty(PropertyId{"percentcomplete"}));
    }

    void peerShapesContainsTodoTxt()
    {
        const TodoDomainPlugin plugin;
        const Kalburator::Shape::Shape todotxt{ DomainId{"todo"}, EncodingId{"todotxt"} };
        QVERIFY(plugin.peerShapes().contains(todotxt));
    }

    void registerEdgesPopulatesRegistry()
    {
        TodoDomainPlugin plugin;
        auto& reg = TransformationRegistry::instance();
        plugin.registerEdges(reg);

        const auto canonical = plugin.canonicalShape();
        QCOMPARE(reg.canonicalFor(DomainId{"todo"}), canonical);

        // Identity edge + ical→todotxt + todotxt→ical = 3 edges from canonical
        // (only edges FROM canonical; todotxt→canonical is FROM todotxt)
        const auto edges = reg.edgesFrom(canonical);
        QVERIFY(edges.size() >= 1);
    }

    void richnessRankCanonical()
    {
        const TodoDomainPlugin plugin;
        QCOMPARE(plugin.richnessRank(plugin.canonicalShape()), 10);
    }

    void richnessRankTodoTxtLower()
    {
        const TodoDomainPlugin plugin;
        const Kalburator::Shape::Shape todotxt{ DomainId{"todo"}, EncodingId{"todotxt"} };
        QVERIFY(plugin.richnessRank(todotxt) < 10);
    }
};

QTEST_GUILESS_MAIN(TestVTodoPlugin)
#include "tst_vtodo_plugin.moc"
