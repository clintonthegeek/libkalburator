#include <QTest>

#include "tododomaindefinition.h"
#include "todostockshapes.h"

using namespace Kalburator::Todo;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyId;

class TestVTodoPlugin : public QObject {
    Q_OBJECT
private slots:
    void canonicalShapeIsTodoIcalVTodo()
    {
        const TodoDomainDefinition def;
        const Kalburator::Shape::Shape expected{ DomainId{"todo"}, EncodingId{"ical-vtodo"} };
        QCOMPARE(def.canonicalShape(), expected);
    }

    void domainIsTodo()
    {
        const TodoDomainDefinition def;
        QCOMPARE(def.domain().toString(), QStringLiteral("todo"));
    }

    void catalogueHasRequiredProperties()
    {
        const TodoDomainDefinition def;
        const auto cat = def.canonicalCatalogue();
        QVERIFY(cat.hasProperty(PropertyId{"uid"}));
        QVERIFY(cat.hasProperty(PropertyId{"summary"}));
        QVERIFY(cat.hasProperty(PropertyId{"due"}));
        QVERIFY(cat.hasProperty(PropertyId{"percentcomplete"}));
    }

    void richnessRankCanonical()
    {
        const TodoDomainDefinition def;
        QCOMPARE(def.richnessRank(def.canonicalShape()), 10);
    }

    void richnessRankTodoTxtLower()
    {
        const TodoDomainDefinition def;
        const Kalburator::Shape::Shape todotxt{ DomainId{"todo"}, EncodingId{"todotxt"} };
        QVERIFY(def.richnessRank(todotxt) < 10);
    }

    void stockShapesHasThreeEdges()
    {
        const TodoStockShapes shapes;
        // identity + ical→todotxt + todotxt→ical
        QCOMPARE(shapes.edges().size(), 3);
    }

    void stockShapesPeerContainsTodoTxt()
    {
        const TodoStockShapes shapes;
        const Kalburator::Shape::Shape todotxt{ DomainId{"todo"}, EncodingId{"todotxt"} };
        const auto peers = shapes.peerShapes();
        QVERIFY(std::any_of(peers.begin(), peers.end(),
            [&](const auto &p) { return p.first == todotxt; }));
    }
};

QTEST_GUILESS_MAIN(TestVTodoPlugin)
#include "tst_vtodo_plugin.moc"
