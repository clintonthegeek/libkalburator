#include <QtTest>
#include "notestockshapes.h"
#include "notedomaindefinition.h"
#include "transformationregistry.h"

using namespace Kalburator::Note;
using namespace Kalburator::Shape;

class TestNoteShapes : public QObject {
    Q_OBJECT

    TransformationRegistry buildRegistry() {
        TransformationRegistry reg;
        NoteDomainDefinition def;
        // Single-node spine: declareCanonical(note, canon).
        reg.declareCanonical(def.domain(), def.canonicalShape());
        NoteStockShapes stock;
        for (const auto& [shape, cat] : stock.peerShapes())
            reg.registerShape(shape, cat);
        for (const auto& edge : stock.edges())
            reg.registerEdge(edge);
        return reg;
    }

private slots:
    void compilesMarkdownToCanon() {
        auto reg = buildRegistry();
        const Shape md{ DomainId{"note"}, EncodingId{"markdown"} };
        const Shape canon{ DomainId{"note"}, EncodingId{"canon"} };
        QVERIFY(reg.compile(md, canon).has_value());
        QVERIFY(reg.compile(canon, md).has_value());
    }

    void frontmatterIsReversible() {
        auto reg = buildRegistry();
        const Shape md{ DomainId{"note"}, EncodingId{"markdown"} };
        const Shape canon{ DomainId{"note"}, EncodingId{"canon"} };
        const LossProfile loss = reg.compile(canon, md)->composedLoss();
        QCOMPARE(loss.affected.value(PropertyId{"frontmatter"}), LossKind::Reversible);
        QVERIFY(loss.droppedProperties().isEmpty());   // nothing dropped
    }

    void routesRecordThroughCanonAndBack() {
        auto reg = buildRegistry();
        const Shape md{ DomainId{"note"}, EncodingId{"markdown"} };
        const Shape canon{ DomainId{"note"}, EncodingId{"canon"} };
        const QByteArray input = "---\nid: 9\ncategory: 1\n---\n\nhello\n";
        const QByteArray inCanon = reg.compile(md, canon)->apply(input);
        const QByteArray back     = reg.compile(canon, md)->apply(inCanon);
        QCOMPARE(back, input);
    }
};

QTEST_GUILESS_MAIN(TestNoteShapes)
#include "tst_note_shapes.moc"
