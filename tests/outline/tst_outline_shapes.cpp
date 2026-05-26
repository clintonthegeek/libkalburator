#include <QtTest>
#include "outlinestockshapes.h"
#include "outlinedomaindefinition.h"
#include "transformationregistry.h"

using namespace Kalburator::Outline;
using namespace Kalburator::Shape;

class TestOutlineShapes : public QObject {
    Q_OBJECT

    TransformationRegistry buildRegistry() {
        TransformationRegistry reg;
        OutlineDomainDefinition def;
        reg.registerShape(def.canonicalShape(), def.canonicalCatalogue());
        reg.declareCanonical(def.domain(), def.canonicalShape());
        OutlineStockShapes stock;
        for (const auto& [shape, cat] : stock.peerShapes())
            reg.registerShape(shape, cat);
        for (const auto& edge : stock.edges())
            reg.registerEdge(edge);
        return reg;
    }

private slots:
    void registersCanonAndPeers() {
        auto reg = buildRegistry();
        const Shape canon{ DomainId{"outline"}, EncodingId{"canon"} };
        const Shape org  { DomainId{"outline"}, EncodingId{"org"} };
        const Shape opml { DomainId{"outline"}, EncodingId{"opml"} };
        QCOMPARE(reg.canonicalFor(DomainId{"outline"}), canon);
        QVERIFY(reg.compile(org, canon).has_value());
        QVERIFY(reg.compile(opml, canon).has_value());
    }

    void richnessOrdersOrgAboveOpml() {
        OutlineDomainDefinition def;
        const Shape org  { DomainId{"outline"}, EncodingId{"org"} };
        const Shape opml { DomainId{"outline"}, EncodingId{"opml"} };
        QVERIFY(def.richnessRank(def.canonicalShape()) > def.richnessRank(org));
        QVERIFY(def.richnessRank(org) > def.richnessRank(opml));
    }

    void canonToOpmlDropsTaskFields() {
        auto reg = buildRegistry();
        const Shape canon{ DomainId{"outline"}, EncodingId{"canon"} };
        const Shape opml { DomainId{"outline"}, EncodingId{"opml"} };
        const LossProfile loss = reg.compile(canon, opml)->composedLoss();
        QCOMPARE(loss.affected.value(PropertyId{"done"}),      LossKind::Dropped);
        QCOMPARE(loss.affected.value(PropertyId{"status"}),    LossKind::Dropped);
        QCOMPARE(loss.affected.value(PropertyId{"priority"}),  LossKind::Dropped);
        QCOMPARE(loss.affected.value(PropertyId{"progress"}),  LossKind::Dropped);
        QCOMPARE(loss.affected.value(PropertyId{"start"}),     LossKind::Dropped);
        QCOMPARE(loss.affected.value(PropertyId{"due"}),       LossKind::Dropped);
        QCOMPARE(loss.affected.value(PropertyId{"completed"}), LossKind::Dropped);
        QCOMPARE(loss.affected.value(PropertyId{"note"}),      LossKind::Reversible);
    }

    void opmlToCanonIsLossless() {
        auto reg = buildRegistry();
        const Shape canon{ DomainId{"outline"}, EncodingId{"canon"} };
        const Shape opml { DomainId{"outline"}, EncodingId{"opml"} };
        const LossProfile loss = reg.compile(opml, canon)->composedLoss();
        QVERIFY(loss.affected.isEmpty());
    }

    void orgCanonAttributesReversible() {
        auto reg = buildRegistry();
        const Shape org  { DomainId{"outline"}, EncodingId{"org"} };
        const Shape canon{ DomainId{"outline"}, EncodingId{"canon"} };
        const LossProfile loss = reg.compile(org, canon)->composedLoss();
        QCOMPARE(loss.affected.value(PropertyId{"attributes"}), LossKind::Reversible);
        const LossProfile reverseL = reg.compile(canon, org)->composedLoss();
        QCOMPARE(reverseL.affected.value(PropertyId{"attributes"}), LossKind::Reversible);
    }
};

QTEST_GUILESS_MAIN(TestOutlineShapes)
#include "tst_outline_shapes.moc"
