#include <QTest>

#include "pipeline.h"
#include "transformationedge.h"

using namespace Kalburator::Shape;

namespace {

class PrefixStage : public TransformationStage {
public:
    explicit PrefixStage(QByteArray prefix) : m_prefix(std::move(prefix)) {}
    QByteArray transform(const QByteArray& sourceBytes) const override {
        return m_prefix + sourceBytes;
    }

private:
    QByteArray m_prefix;
};

LossProfile lossy(const QString& dropped) {
    LossProfile p;
    p.level = LossLevel::IntraDomainLossy;
    p.dropped.insert(PropertyId{dropped});
    return p;
}

Shape calIcal()  { return { DomainId{"calendar"}, EncodingId{"ical"} }; }
Shape calOrg()   { return { DomainId{"calendar"}, EncodingId{"org"} }; }
Shape calPalm()  { return { DomainId{"calendar"}, EncodingId{"palm-datebook"} }; }

}  // namespace

class TestPipeline : public QObject {
    Q_OBJECT
private slots:
    void identityPipeline() {
        Pipeline p{calIcal()};
        QCOMPARE(p.inputShape(), calIcal());
        QCOMPARE(p.outputShape(), calIcal());
        QVERIFY(p.isIdentity());
        QCOMPARE(p.composedLoss().level, LossLevel::Lossless);
        QCOMPARE(p.apply("payload"), QByteArray("payload"));
    }

    void singleEdgePipeline() {
        TransformationEdge e{ calIcal(), calOrg(), lossy(QStringLiteral("attendees")),
                              std::make_shared<PrefixStage>("OrgEncoded:") };
        Pipeline p{ {e} };
        QCOMPARE(p.inputShape(), calIcal());
        QCOMPARE(p.outputShape(), calOrg());
        QVERIFY(!p.isIdentity());
        QCOMPARE(p.apply("foo"), QByteArray("OrgEncoded:foo"));
        QCOMPARE(p.composedLoss().level, LossLevel::IntraDomainLossy);
        QVERIFY(p.composedLoss().dropped.contains(PropertyId{QStringLiteral("attendees")}));
    }

    void twoEdgePipelineChainsAndComposes() {
        TransformationEdge e1{ calIcal(), calOrg(), lossy(QStringLiteral("attachments")),
                               std::make_shared<PrefixStage>("Org-") };
        TransformationEdge e2{ calOrg(), calPalm(), lossy(QStringLiteral("description")),
                               std::make_shared<PrefixStage>("Palm-") };
        Pipeline p{ {e1, e2} };
        QCOMPARE(p.inputShape(), calIcal());
        QCOMPARE(p.outputShape(), calPalm());
        QCOMPARE(p.apply("payload"), QByteArray("Palm-Org-payload"));
        // Loss composes: dropped is {attachments, description}.
        const LossProfile loss = p.composedLoss();
        QCOMPARE(loss.level, LossLevel::IntraDomainLossy);
        QCOMPARE(loss.dropped.size(), 2);
        QVERIFY(loss.dropped.contains(PropertyId{QStringLiteral("attachments")}));
        QVERIFY(loss.dropped.contains(PropertyId{QStringLiteral("description")}));
    }

    void nonMatchingEdgeChainThrows() {
        TransformationEdge e1{ calIcal(), calOrg(), {},
                               std::make_shared<IdentityStage>() };
        TransformationEdge e2{ calPalm(), calIcal(), {},
                               std::make_shared<IdentityStage>() };
        bool threw = false;
        try {
            Pipeline p{ {e1, e2} };
            (void)p;
        } catch (const std::logic_error&) {
            threw = true;
        }
        QVERIFY(threw);
    }

    void edgeToStringFormat() {
        TransformationEdge e{ calIcal(), calOrg(),
                              lossy(QStringLiteral("attendees")),
                              std::make_shared<IdentityStage>() };
        const QString s = e.toString();
        QVERIFY(s.contains(QStringLiteral("calendar+ical")));
        QVERIFY(s.contains(QStringLiteral("calendar+org")));
        QVERIFY(s.contains(QStringLiteral("intra-lossy")));
        QVERIFY(s.contains(QStringLiteral("attendees")));
    }
};

QTEST_GUILESS_MAIN(TestPipeline)
#include "tst_pipeline.moc"
