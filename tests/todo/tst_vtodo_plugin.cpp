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
    void canonicalShapeIsTodoCanon()
    {
        const TodoDomainDefinition def;
        const Kalburator::Shape::Shape expected{ DomainId{"todo"}, EncodingId{"canon"} };
        QCOMPARE(def.canonicalShape(), expected);
    }

    void domainIsTodo()
    {
        const TodoDomainDefinition def;
        QCOMPARE(def.domain().toString(), QStringLiteral("todo"));
    }

    void canonicalCatalogueHasCanonProperties()
    {
        const TodoDomainDefinition def;
        const auto cat = def.canonicalCatalogue();
        // Canon catalogue fields (schema doc §4)
        QVERIFY(cat.hasProperty(PropertyId{"uid"}));
        QVERIFY(cat.hasProperty(PropertyId{"summary"}));
        QVERIFY(cat.hasProperty(PropertyId{"due"}));
        QVERIFY(cat.hasProperty(PropertyId{"percentComplete"}));
        // Plan 3 Task B1: MS To-Do / Google Tasks fields also present in canon
        QVERIFY(cat.hasProperty(PropertyId{"checklistItems"}));
        QVERIFY(cat.hasProperty(PropertyId{"parentUid"}));
    }

    void richnessRankCanonical()
    {
        const TodoDomainDefinition def;
        // Canon head should have the highest richness rank
        QCOMPARE(def.richnessRank(def.canonicalShape()), 100);
    }

    void richnessRankTodoTxtLower()
    {
        const TodoDomainDefinition def;
        const Kalburator::Shape::Shape todotxt{ DomainId{"todo"}, EncodingId{"todotxt"} };
        QVERIFY(def.richnessRank(todotxt) < 100);
    }

    void stockShapesHasFiveEdges()
    {
        const TodoStockShapes shapes;
        // canon-identity + vtodo→canon + canon→vtodo + vtodo→todotxt + todotxt→vtodo
        QCOMPARE(shapes.edges().size(), 5);
    }

    void stockShapesPeerContainsTodoTxt()
    {
        const TodoStockShapes shapes;
        const Kalburator::Shape::Shape todotxt{ DomainId{"todo"}, EncodingId{"todotxt"} };
        const auto peers = shapes.peerShapes();
        QVERIFY(std::any_of(peers.begin(), peers.end(),
            [&](const auto &p) { return p.first == todotxt; }));
    }

    void stockShapesPeerContainsVtodo()
    {
        const TodoStockShapes shapes;
        const Kalburator::Shape::Shape vtodo{ DomainId{"todo"}, EncodingId{"ical-vtodo"} };
        const auto peers = shapes.peerShapes();
        QVERIFY(std::any_of(peers.begin(), peers.end(),
            [&](const auto &p) { return p.first == vtodo; }));
    }
};

QTEST_GUILESS_MAIN(TestVTodoPlugin)
#include "tst_vtodo_plugin.moc"
