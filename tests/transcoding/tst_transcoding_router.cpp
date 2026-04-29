// tests/transcoding/tst_transcoding_router.cpp
#include <QtTest>
#include <KCalendarCore/Event>

#include "transcodingrouter.h"
#include "transcodingregistry.h"
#include "propertytranscoder.h"

using namespace Kalburator::Sync;

namespace {

class FakeTranscoder : public PropertyTranscoder
{
public:
    FakeTranscoder(QString src, QString tgt, TranscodingFidelity fid)
        : m_source(std::move(src)), m_target(std::move(tgt)), m_fidelity(fid) {}

    QString propertyName() const override { return QStringLiteral("FAKE"); }
    QString sourceBackendType() const override { return m_source; }
    QString targetBackendType() const override { return m_target; }
    TranscodingFidelity fidelity() const override { return m_fidelity; }
    bool transcode(KCalendarCore::Incidence::Ptr&) const override { return true; }
    QString description() const override { return QStringLiteral("fake transcoder"); }

private:
    QString m_source;
    QString m_target;
    TranscodingFidelity m_fidelity;
};

} // namespace

class TestTranscodingRouter : public QObject
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        // Singleton hygiene per FINDINGS 2026-04-28; Phase E new code uses
        // a stack registry where possible, but tests of the router with
        // the singleton still need this.
        TranscodingRegistry::instance().clear();
    }

    void emptySourceType_returnsEmptyPlan()
    {
        TranscodingRouter router(TranscodingRegistry::instance());
        const auto plan = router.plan(QString(), QStringLiteral("orgmode"));
        QVERIFY(plan.isEmpty());
    }

    void emptyTargetType_returnsEmptyPlan()
    {
        TranscodingRouter router(TranscodingRegistry::instance());
        const auto plan = router.plan(QStringLiteral("local"), QString());
        QVERIFY(plan.isEmpty());
    }

    void equalTypes_returnsEmptyPlan()
    {
        TranscodingRegistry::instance().registerTranscoder(
            std::make_unique<FakeTranscoder>(QStringLiteral("*"),
                                             QStringLiteral("*"),
                                             TranscodingFidelity::Lossy));
        TranscodingRouter router(TranscodingRegistry::instance());
        const auto plan = router.plan(QStringLiteral("local"),
                                      QStringLiteral("local"));
        QVERIFY(plan.isEmpty());
    }

    void differingTypes_noMatchingTranscoder_returnsEmptyPlan()
    {
        TranscodingRegistry::instance().registerTranscoder(
            std::make_unique<FakeTranscoder>(QStringLiteral("orgmode"),
                                             QStringLiteral("orgmode"),
                                             TranscodingFidelity::Lossy));
        TranscodingRouter router(TranscodingRegistry::instance());
        const auto plan = router.plan(QStringLiteral("local"),
                                      QStringLiteral("caldav"));
        QVERIFY(plan.isEmpty());
    }

    void differingTypes_matchingTranscoder_returnsPopulatedPlan()
    {
        TranscodingRegistry::instance().registerTranscoder(
            std::make_unique<FakeTranscoder>(QStringLiteral("*"),
                                             QStringLiteral("orgmode"),
                                             TranscodingFidelity::Lossy));
        TranscodingRouter router(TranscodingRegistry::instance());
        const auto plan = router.plan(QStringLiteral("local"),
                                      QStringLiteral("orgmode"));
        QVERIFY(!plan.isEmpty());
        QCOMPARE(plan.transcoders.size(), 1);
        QVERIFY(plan.routingDecision.contains(QStringLiteral("source=local")));
        QVERIFY(plan.routingDecision.contains(QStringLiteral("target=orgmode")));
    }
};

QTEST_GUILESS_MAIN(TestTranscodingRouter)
#include "tst_transcoding_router.moc"
