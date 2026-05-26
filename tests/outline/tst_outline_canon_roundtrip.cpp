#include <QtTest>
#include "outlinedomaindefinition.h"
#include "outlinestockshapes.h"
#include "transformationregistry.h"
#include "lossprofile.h"

using namespace Kalburator::Outline;
using namespace Kalburator::Shape;

class TestOutlineRoundtrip : public QObject {
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

    // org is near-lossless (richnessRank 70): it preserves all task semantics
    // (text, note, status, priority, due, start, completed, tags) through the
    // round-trip. Only progress, created, and id have no thin-adapter org
    // representation and are honestly declared Dropped. Mapping them into
    // :PROPERTIES: for true reversibility is a deferred enhancement.
    void orgPreservesRichFieldsDroppingOnlyThinGaps()
    {
        auto reg = buildRegistry();
        const Shape org  { DomainId{"outline"}, EncodingId{"org"} };
        const Shape canon{ DomainId{"outline"}, EncodingId{"canon"} };

        const auto fwdPipeline = reg.compile(org, canon);
        QVERIFY2(fwdPipeline.has_value(), "compile(org, canon) must succeed");

        const auto revPipeline = reg.compile(canon, org);
        QVERIFY2(revPipeline.has_value(), "compile(canon, org) must succeed");

        // Compose both hop losses: any property touched in either direction.
        const LossProfile composed =
            fwdPipeline->composedLoss().compose(revPipeline->composedLoss());

        // The composed dropped set must be exactly the three known thin-adapter gaps.
        const QSet<PropertyId> dropped = composed.droppedProperties();
        const QSet<PropertyId> expectedDropped {
            PropertyId{QStringLiteral("progress")},
            PropertyId{QStringLiteral("created")},
            PropertyId{QStringLiteral("id")},
        };
        QCOMPARE(dropped, expectedDropped);

        // Rich Tier-1/2 fields must NOT be dropped through the org round-trip.
        for (const char* k : { "text", "note", "status", "priority",
                               "due", "start", "completed", "tags" }) {
            QVERIFY2(!dropped.contains(PropertyId{QString::fromLatin1(k)}),
                     qPrintable(QStringLiteral("org round-trip must not drop: ")
                                + QString::fromLatin1(k)));
        }
    }

    // canon→opml must honestly report the task fields it drops, and must NOT
    // drop `text` (structural content always survives in OPML).
    void opmlDropsTaskFieldsHonestly()
    {
        auto reg = buildRegistry();
        const Shape canon{ DomainId{"outline"}, EncodingId{"canon"} };
        const Shape opml { DomainId{"outline"}, EncodingId{"opml"} };

        const auto pipeline = reg.compile(canon, opml);
        QVERIFY2(pipeline.has_value(), "compile(canon, opml) must succeed");

        const LossProfile loss = pipeline->composedLoss();

        // Task fields that OPML cannot represent must be reported as Dropped.
        QCOMPARE(loss.affected.value(PropertyId{"priority"}), LossKind::Dropped);
        QCOMPARE(loss.affected.value(PropertyId{"status"}),   LossKind::Dropped);
        QCOMPARE(loss.affected.value(PropertyId{"due"}),      LossKind::Dropped);

        // Structural content (`text`) must not appear in the loss profile at all.
        QVERIFY2(!loss.affected.contains(PropertyId{"text"}),
                 "canon->opml must not report 'text' as lost");
    }
};

QTEST_GUILESS_MAIN(TestOutlineRoundtrip)
#include "tst_outline_canon_roundtrip.moc"
