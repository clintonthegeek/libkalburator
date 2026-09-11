#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <functional>

#include <KCalendarCore/Event>

#include <kalburator/runtime/collectionruntime.h>
#include <kalburator/calendar/mockbackend.h>

using namespace Kalburator::Runtime;

namespace {

class FakeFactory final : public BackendFactory
{
public:
    QString factoryId() const override { return QStringLiteral("palm-shaped"); }
    bool validate(QString &errorMessage) const override
    {
        errorMessage.clear();
        return true;
    }
    std::unique_ptr<BackendEndpoint> createEndpoint(
        const BackendMaterialization &request, QString &errorMessage) const override
    {
        errorMessage.clear();
        ++createCount;
        lastRequest = request;
        auto backend = std::make_unique<Kalburator::Sync::MockBackend>(request.endpointId);
        if (request.factoryInput.value(QStringLiteral("seed")).toBool()) {
            auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event);
            event->setUid(request.factoryInput.value(QStringLiteral("uid")).toString());
            event->setSummary(request.factoryInput.value(QStringLiteral("summary")).toString());
            backend->addIncidence(QStringLiteral("records"), event);
        }
        backend->setOperationDelay(
            request.factoryInput.value(QStringLiteral("delayMs")).toInt());
        endpoints.insert(request.endpointId, backend.get());
        return std::make_unique<BackendEndpoint>(std::move(backend));
    }

    mutable int createCount = 0;
    mutable BackendMaterialization lastRequest;
    mutable QHash<QString, Kalburator::Sync::MockBackend *> endpoints;
};

class FakeLease final : public ExternalResourceLease
{
public:
    void setLossHandler(std::function<void()> handler) override
    { lossHandler = std::move(handler); }
    bool prepare(QString &errorMessage) override
    {
        order.append(QStringLiteral("prepare"));
        ++prepareCount;
        errorMessage.clear();
        return true;
    }
    bool executePhase(const QString &, QString &errorMessage) override
    {
        order.append(QStringLiteral("execute"));
        ++executeCount;
        if (onExecute) onExecute();
        errorMessage.clear();
        return true;
    }
    bool flush(QString &errorMessage) override
    {
        order.append(QStringLiteral("flush"));
        ++flushCount;
        errorMessage.clear();
        return true;
    }
    void cancel() override { cancelled = true; }
    void finish(const RunResult &result) override
    {
        order.append(QStringLiteral("finish"));
        ++finishCount;
        finished = true;
        lastResult = result;
    }

    bool cancelled = false;
    bool finished = false;
    int finishCount = 0;
    int prepareCount = 0;
    int executeCount = 0;
    int flushCount = 0;
    RunResult lastResult;
    std::function<void()> onExecute;
    std::function<void()> lossHandler;
    QStringList order;

    void lose() { if (lossHandler) lossHandler(); }
};

} // namespace

class TestWildPalmsRuntimeContract : public QObject
{
    Q_OBJECT
private slots:
    void leasedDeviceDefinitionMustConstructWithoutAcquiringLease();
    void providerConnectionCommandCompletesWhenNoProvidersExist();
    void oneRunConvergesPalmHubRemoteAndScopesLease();
    void reverseMirrorAppliesAcrossTheSelectedGraph();
    void linkLossCancelsOnlyResourceBoundWorkAndFinishesOnce();
};

void TestWildPalmsRuntimeContract::leasedDeviceDefinitionMustConstructWithoutAcquiringLease()
{
    RuntimeDefinition definition;
    definition.storagePath = QStringLiteral("wildpalms-test-sync.db");
    auto factory = QSharedPointer<FakeFactory>(new FakeFactory);
    definition.backendFactories.append(factory);
    auto lease = QSharedPointer<FakeLease>(new FakeLease);
    definition.resources.append({QStringLiteral("palm-session"), lease});

    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(
        error.isEmpty() ? QStringLiteral("runtime construction failed") : error));

    const auto topology = runtime->applyTopology({
        {{QStringLiteral("palm"), QStringLiteral("palm-shaped"), {}, {},
          QStringLiteral("palm-session"), {}},
         {QStringLiteral("hub"), QStringLiteral("palm-shaped")},
         {QStringLiteral("remote"), QStringLiteral("palm-shaped")}}, {}});
    QVERIFY(topology.committed);
    QCOMPARE(factory->createCount, 3);
    QCOMPARE(factory->lastRequest.endpointId, QStringLiteral("remote"));
    QCOMPARE(factory->lastRequest.externalResourceId, QString());
    QCOMPARE(lease->prepareCount, 0);
    QCOMPARE(lease->executeCount, 0);
    QCOMPARE(lease->flushCount, 0);
    QCOMPARE(lease->finishCount, 0);
}

void TestWildPalmsRuntimeContract::providerConnectionCommandCompletesWhenNoProvidersExist()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));

    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));
    auto future = runtime->connectProviders();
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
    QVERIFY(future.result());
}

static Kalburator::Sync::SyncMapping mapping(const QString &id,
                                             const QString &source,
                                             const QString &target)
{
    Kalburator::Sync::SyncMapping result;
    result.id = id;
    result.sourceBackend = source;
    result.sourceCalendar = QStringLiteral("records");
    result.targetBackend = target;
    result.targetCalendar = QStringLiteral("records");
    return result;
}

void TestWildPalmsRuntimeContract::oneRunConvergesPalmHubRemoteAndScopesLease()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    auto factory = QSharedPointer<FakeFactory>(new FakeFactory);
    definition.backendFactories.append(factory);
    auto lease = QSharedPointer<FakeLease>(new FakeLease);
    definition.resources.append({QStringLiteral("palm-session"), lease});

    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));
    // Deliberately order hub->remote before palm->hub. The remote can only
    // receive the Palm record if the library-owned fixpoint pass runs again.
    const auto hubRemote = mapping(QStringLiteral("hub-remote"),
                                   QStringLiteral("hub"), QStringLiteral("remote"));
    const auto palmHub = mapping(QStringLiteral("palm-hub"),
                                 QStringLiteral("palm"), QStringLiteral("hub"));
    QVERIFY(runtime->applyTopology({
        {{QStringLiteral("palm"), QStringLiteral("palm-shaped"),
          {{QStringLiteral("seed"), true}, {QStringLiteral("uid"), QStringLiteral("p1")},
           {QStringLiteral("summary"), QStringLiteral("from Palm")}}, {},
          QStringLiteral("palm-session")},
         {QStringLiteral("hub"), QStringLiteral("palm-shaped")},
         {QStringLiteral("remote"), QStringLiteral("palm-shaped")}},
        {hubRemote, palmHub}}).committed);

    auto run = runtime->run({RunSelection::allEnabled(), RunIntent::Normal});
    QTRY_VERIFY_WITH_TIMEOUT(run.isFinished(), 30000);
    QVERIFY2(run.result().success, qPrintable(run.result().errorMessage));
    QVERIFY(factory->endpoints.value(QStringLiteral("remote"))->incidence(
        QStringLiteral("records"), QStringLiteral("p1")));
    QCOMPARE(lease->prepareCount, 1);
    QCOMPARE(lease->executeCount, 1);
    QCOMPARE(lease->flushCount, 1);
    QCOMPARE(lease->finishCount, 1);
    QCOMPARE(lease->order, QStringList({QStringLiteral("prepare"),
                                        QStringLiteral("execute"),
                                        QStringLiteral("flush"),
                                        QStringLiteral("finish")}));
}

void TestWildPalmsRuntimeContract::reverseMirrorAppliesAcrossTheSelectedGraph()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    auto factory = QSharedPointer<FakeFactory>(new FakeFactory);
    definition.backendFactories.append(factory);
    auto lease = QSharedPointer<FakeLease>(new FakeLease);
    definition.resources.append({QStringLiteral("palm-session"), lease});
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));

    const auto palmHub = mapping(QStringLiteral("palm-hub"),
                                 QStringLiteral("palm"), QStringLiteral("hub"));
    const auto hubRemote = mapping(QStringLiteral("hub-remote"),
                                   QStringLiteral("hub"), QStringLiteral("remote"));
    QVERIFY(runtime->applyTopology({
        {{QStringLiteral("palm"), QStringLiteral("palm-shaped"), {}, {},
          QStringLiteral("palm-session")},
         {QStringLiteral("hub"), QStringLiteral("palm-shaped")},
         {QStringLiteral("remote"), QStringLiteral("palm-shaped"),
          {{QStringLiteral("seed"), true}, {QStringLiteral("uid"), QStringLiteral("r1")},
           {QStringLiteral("summary"), QStringLiteral("from remote")}}}},
        {palmHub, hubRemote}}).committed);

    RunRequest request{RunSelection::allEnabled(), RunIntent::Mirror};
    request.mirrorDirection = MirrorDirection::TargetToSource;
    auto run = runtime->run(request);
    QTRY_VERIFY_WITH_TIMEOUT(run.isFinished(), 30000);
    QVERIFY2(run.result().success, qPrintable(run.result().errorMessage));
    QVERIFY(factory->endpoints.value(QStringLiteral("palm"))->incidence(
        QStringLiteral("records"), QStringLiteral("r1")));
    QCOMPARE(lease->finishCount, 1);
}

void TestWildPalmsRuntimeContract::linkLossCancelsOnlyResourceBoundWorkAndFinishesOnce()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    auto factory = QSharedPointer<FakeFactory>(new FakeFactory);
    definition.backendFactories.append(factory);
    auto lease = QSharedPointer<FakeLease>(new FakeLease);
    definition.resources.append({QStringLiteral("palm-session"), lease});
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));

    const auto palmHub = mapping(QStringLiteral("palm-hub"),
                                 QStringLiteral("palm"), QStringLiteral("hub"));
    QVERIFY(runtime->applyTopology({
        {{QStringLiteral("palm"), QStringLiteral("palm-shaped"),
          {{QStringLiteral("seed"), true}, {QStringLiteral("uid"), QStringLiteral("p1")},
           {QStringLiteral("summary"), QStringLiteral("from Palm")},
           {QStringLiteral("delayMs"), 1000}}, {}, QStringLiteral("palm-session")},
         {QStringLiteral("hub"), QStringLiteral("palm-shaped"),
          {{QStringLiteral("delayMs"), 1000}}}},
        {palmHub}}).committed);

    auto run = runtime->run({RunSelection::one(palmHub.id), RunIntent::Normal});
    QTimer::singleShot(20, [lease]() { lease->lose(); });
    QTRY_VERIFY_WITH_TIMEOUT(run.isFinished(), 10000);
    const auto result = run.result();
    QVERIFY(!result.success);
    QVERIFY(result.cancelled);
    QCOMPARE(result.mappings.size(), 1);
    QVERIFY(result.mappings.first().cancelled);
    QCOMPARE(lease->flushCount, 0);
    QCOMPARE(lease->finishCount, 1);
}

QTEST_MAIN(TestWildPalmsRuntimeContract)
#include "tst_wildpalms_runtime_contract.moc"
