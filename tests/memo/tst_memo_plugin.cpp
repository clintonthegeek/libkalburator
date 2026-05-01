#include <QTest>

#include "memodomainplugin.h"
#include "domainregistry.h"
#include "transformationregistry.h"

using namespace Kalburator::Memo;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::DomainRegistry;
using Kalburator::Shape::TransformationRegistry;

class TestMemoPlugin : public QObject {
    Q_OBJECT
private slots:
    void cleanup()
    {
        DomainRegistry::instance().clear();
        TransformationRegistry::instance().clear();
    }

    void canonicalShapeIsMemoText()
    {
        const KalburatorDomainMemo plugin;
        const Kalburator::Shape::Shape expected{ DomainId{"memo"}, EncodingId{"text"} };
        QCOMPARE(plugin.canonicalShape(), expected);
    }

    void domainIsMemo()
    {
        const KalburatorDomainMemo plugin;
        QCOMPARE(plugin.domain().toString(), QStringLiteral("memo"));
    }

    void catalogueHasBodyAndCategories()
    {
        const KalburatorDomainMemo plugin;
        const auto cat = plugin.canonicalCatalogue();
        QVERIFY(cat.hasProperty(PropertyId{"body"}));
        QVERIFY(cat.hasProperty(PropertyId{"categories"}));
        QVERIFY(cat.hasProperty(PropertyId{"lastmodified"}));
    }

    void peerShapesEmpty()
    {
        const KalburatorDomainMemo plugin;
        QVERIFY(plugin.peerShapes().isEmpty());
    }

    void registerEdgesDeclaresCanonical()
    {
        KalburatorDomainMemo plugin;
        auto& reg = TransformationRegistry::instance();
        plugin.registerEdges(reg);
        QCOMPARE(reg.canonicalFor(DomainId{"memo"}), plugin.canonicalShape());
    }

    void richnessRankCanonical()
    {
        const KalburatorDomainMemo plugin;
        QCOMPARE(plugin.richnessRank(plugin.canonicalShape()), 10);
    }
};

QTEST_GUILESS_MAIN(TestMemoPlugin)
#include "tst_memo_plugin.moc"
