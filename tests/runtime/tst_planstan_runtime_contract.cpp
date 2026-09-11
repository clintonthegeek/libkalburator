#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QAtomicInt>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <KCalendarCore/Event>

#include <kalburator/runtime/collectionruntime.h>
#include "fakecaldavserver.h"
#include <kalburator/calendar/mockbackend.h>

using namespace Kalburator::Runtime;

namespace {
struct MutationProbe
{
    QAtomicInt creates = 0;
    QAtomicInt deletes = 0;
    QAtomicInt executorThreadCalls = 0;
    QAtomicInt failCreateAfter = -1;
};

class MutationProbeBackend final : public Kalburator::Sync::MockBackend
{
public:
    MutationProbeBackend(const QString &id, std::shared_ptr<MutationProbe> probe)
        : MockBackend(id), m_probe(std::move(probe)) {}

    QString createCollection(const Kalburator::Sync::CollectionInfo &info) override
    {
        const int failAfter = m_probe->failCreateAfter.loadRelaxed();
        if (failAfter >= 0 && m_probe->creates.loadRelaxed() >= failAfter)
            return {};
        const auto created = MockBackend::createCollection(info);
        if (!created.isEmpty())
            ++m_probe->creates;
        if (QThread::currentThread() != QCoreApplication::instance()->thread())
            ++m_probe->executorThreadCalls;
        return created;
    }

    bool deletePhysicalCollection(const QString &collectionId) override
    {
        const bool deleted = MockBackend::deletePhysicalCollection(collectionId);
        if (deleted)
            ++m_probe->deletes;
        if (QThread::currentThread() != QCoreApplication::instance()->thread())
            ++m_probe->executorThreadCalls;
        return deleted;
    }

private:
    std::shared_ptr<MutationProbe> m_probe;
};

class ReadOnlyEndpointBackend final : public Kalburator::Sync::SyncBackendBase
{
public:
    explicit ReadOnlyEndpointBackend(const QString &id)
        : m_id(id) {}
    QString backendType() const override { return QStringLiteral("read-only-test"); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override { return {}; }
    QString backendId() const override { return m_id; }
    QString displayName() const override { return m_id; }
    bool isAvailable() const override { return true; }
private:
    QString m_id;
};

class PlanEndpointFactory final : public BackendFactory
{
public:
    QString factoryId() const override { return QStringLiteral("plan-endpoint"); }
    bool validate(QString &errorMessage) const override
    { errorMessage.clear(); return true; }
    std::unique_ptr<BackendEndpoint> createEndpoint(
        const BackendMaterialization &request, QString &errorMessage) const override
    {
        lastRequest = request;
        ++createCount;
        if (failCreation && request.factoryInput.value(QStringLiteral("kind")).toString()
            == QStringLiteral("fail")) {
            errorMessage = QStringLiteral("injected endpoint materialization failure");
            return {};
        }
        if (request.factoryInput.value(QStringLiteral("kind")).toString()
            == QStringLiteral("read-only")) {
            return std::make_unique<BackendEndpoint>(
                std::make_unique<ReadOnlyEndpointBackend>(request.endpointId));
        }
        if (request.factoryInput.value(QStringLiteral("kind")).toString()
            == QStringLiteral("mutation-probe")) {
            return std::make_unique<BackendEndpoint>(
                std::make_unique<MutationProbeBackend>(request.endpointId, mutationProbe));
        }
        auto backend = std::make_unique<Kalburator::Sync::MockBackend>(request.endpointId);
        if (request.factoryInput.value(QStringLiteral("kind")).toString()
            == QStringLiteral("local")) {
            auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event);
            event->setUid(QStringLiteral("runtime-event"));
            event->setSummary(QStringLiteral("runtime-owned endpoint"));
            backend->addIncidence(QStringLiteral("local-calendar"), event);
        } else if (request.factoryInput.value(QStringLiteral("kind")).toString()
                   == QStringLiteral("remote-seeded")) {
            auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event);
            event->setUid(QStringLiteral("remote-event"));
            event->setSummary(QStringLiteral("canonical runtime event"));
            backend->addIncidence(QStringLiteral("dav-calendar"), event);
        }
        backend->setOperationDelay(
            request.factoryInput.value(QStringLiteral("delayMs")).toInt());
        endpoints.insert(request.endpointId, backend.get());
        errorMessage.clear();
        return std::make_unique<BackendEndpoint>(std::move(backend));
    }
    mutable BackendMaterialization lastRequest;
    mutable int createCount = 0;
    mutable bool failCreation = false;
    mutable QHash<QString, Kalburator::Sync::MockBackend *> endpoints;
    std::shared_ptr<MutationProbe> mutationProbe = std::make_shared<MutationProbe>();
};

class TopologyPersistenceFake final : public TopologyPersistenceParticipant
{
public:
    bool prepare(const TopologyDefinition &, QString &) override
    { ++prepareCount; return !failPrepare; }
    bool commit(QString &error) override
    {
        ++commitCount;
        if (failCommit) error = QStringLiteral("injected persistence failure");
        return !failCommit;
    }
    void rollback() override { ++rollbackCount; }
    int prepareCount = 0;
    int commitCount = 0;
    int rollbackCount = 0;
    bool failPrepare = false;
    bool failCommit = false;
};
}

class TestPlanStanRuntimeContract : public QObject
{
    Q_OBJECT
private slots:
    void longLivedLocalAndDavRuntimeMustConstruct();
    void invalidDefinitionReportsFailure();
    void invalidStorageReportsFailure();
    void resetReopensOwnedStores();
    void failedResetLeavesRuntimeUnavailable();
    void mappingDefinitionsBecomeCommittedTopology();
    void failedMaterializationPreservesPriorTopology();
    void topologyRemovalMustNameExistingEndpoint();
    void idOnlyTopologyCannotReportSuccessfulWork();
    void localDavTopologyRegistersRuntimeOwnedBackends();
    void disconnectedProviderCannotEnterCommittedTopology();
    void providerConnectionOwnsReadyBackendLifecycle();
    void providerSnapshotPublishesTypedStateAndDiscovery();
    void providerMutationIsIncrementalAndInvalidatesStaleTopology();
    void canonicalRecordEventNeedsNoBackendReadOrNativeParse();
    void runTelemetryUsesTypedProgressKinds();
    void runtimePolicyHasValidatedConstructionAndUpdateBoundary();
    void topologyPersistenceCommitsBeforePublication();
    void providerEditsRollbackWithTopologyPersistence();
    void collectionMutationsPublishAfterTopologyCommit();
    void collectionMutationVerbsAndCompensationAreExplicit();
    void unsupportedCollectionMutationIsTypedFailure();
    void failedCollectionMutationDoesNotPublishAndReportsRepair();
    void invalidLaterCollectionMutationHasNoEarlierPhysicalEffect();
    void failedLaterCollectionMutationCompensatesEarlierCreate();
    void reentrantTopologyCommandIsRejectedDuringCommitNotification();
    void reentrantRunAndProviderCommandsAreRejectedDuringTopologyCommit();
    void topologyReplacementIsRejectedWhileRunIsActive();
    void conflictsArePersistedSurfacedAndResolvableThroughRuntime();
    void activeRunDestructionCancelsAndCompletesFuture();
};

void TestPlanStanRuntimeContract::longLivedLocalAndDavRuntimeMustConstruct()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());

    FakeCalDavServer dav;
    QVERIFY(dav.startListening());

    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    Kalburator::Sync::BackendConfiguration provider;
    provider.id = QStringLiteral("dav");
    provider.type = QStringLiteral("caldav");
    provider.displayName = QStringLiteral("Test DAV");
    definition.providers.append(provider);

    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(
        error.isEmpty() ? QStringLiteral("runtime construction failed") : error));

    const auto topology = runtime->applyTopology({{}, {}});
    QVERIFY(topology.committed);

    const auto snapshot = runtime->snapshot();
    QCOMPARE(snapshot.providerIds, QStringList{QStringLiteral("dav")});
    QVERIFY(snapshot.mappingIds.isEmpty());
    const auto result = runtime->run({RunSelection::none(), RunIntent::Normal});
    QVERIFY(result.isStarted());
    QVERIFY(result.result().success);
}

void TestPlanStanRuntimeContract::invalidDefinitionReportsFailure()
{
    QString error;
    auto runtime = CollectionRuntime::create({}, error);
    QVERIFY(runtime == nullptr);
    QCOMPARE(error, QStringLiteral("runtime storage path must not be empty"));
}

void TestPlanStanRuntimeContract::invalidStorageReportsFailure()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    const auto blockerPath = profile.filePath(QStringLiteral("not-a-directory"));
    QFile blocker(blockerPath);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();

    RuntimeDefinition definition;
    definition.storagePath = blockerPath + QStringLiteral("/runtime.db");
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY(runtime == nullptr);
    QVERIFY2(!error.isEmpty(), "an unusable store path must be reported");
}

void TestPlanStanRuntimeContract::resetReopensOwnedStores()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));
    QVERIFY(runtime->applyTopology({}).committed);
    const auto unrelatedPath = profile.filePath(QStringLiteral("keep-me.txt"));
    QFile unrelated(unrelatedPath);
    QVERIFY(unrelated.open(QIODevice::WriteOnly));
    unrelated.close();
    const auto journalDirectory = definition.storagePath + QStringLiteral(".journals");
    QVERIFY(QDir().mkpath(journalDirectory));
    QFile journal(journalDirectory + QStringLiteral("/calendar.calendar.journal"));
    QVERIFY(journal.open(QIODevice::WriteOnly));
    journal.write("{\"op\":\"update\"}\n");
    journal.close();
    runtime->cancel();
    QVERIFY(runtime->reset(error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(runtime->snapshot().generation, quint64(0));
    QVERIFY(QFile::exists(unrelatedPath));
    QVERIFY(QFile::exists(journalDirectory + QStringLiteral("/calendar.calendar.journal")));
    const auto result = runtime->run({RunSelection::allEnabled(), RunIntent::Normal});
    QVERIFY(result.isStarted());
    QVERIFY(result.result().success);
}

void TestPlanStanRuntimeContract::failedResetLeavesRuntimeUnavailable()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));
    QVERIFY(runtime->applyTopology({}).committed);

    QVERIFY(QFile::remove(definition.storagePath));
    QVERIFY(QDir().mkpath(definition.storagePath));

    QVERIFY(!runtime->reset(error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(runtime->snapshot().generation, quint64(0));
    QVERIFY(!runtime->applyTopology({}).committed);
    const auto result = runtime->run({RunSelection::allEnabled(), RunIntent::Normal});
    QVERIFY(result.isStarted());
    QVERIFY(!result.result().success);
    QVERIFY(!result.result().errorMessage.isEmpty());
}

void TestPlanStanRuntimeContract::mappingDefinitionsBecomeCommittedTopology()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));

    Kalburator::Sync::SyncMapping mapping;
    mapping.id = QStringLiteral("local-to-dav");
    mapping.sourceBackend = QStringLiteral("local");
    mapping.sourceCalendar = QStringLiteral("local-calendar");
    mapping.targetBackend = QStringLiteral("dav");
    mapping.targetCalendar = QStringLiteral("dav-calendar");
    const auto topology = runtime->applyTopology({{}, {mapping}});
    QVERIFY(!topology.committed);
    QVERIFY(!topology.errorMessage.isEmpty());
    QVERIFY(runtime->snapshot().mappingIds.isEmpty());
    auto duplicate = mapping;
    duplicate.id = mapping.id;
    const auto rejected = runtime->applyTopology({{}, {mapping, duplicate}});
    QVERIFY(!rejected.committed);
    QVERIFY(!rejected.errorMessage.isEmpty());

    const auto incomplete = runtime->applyTopology({{}, {mapping}});
    QVERIFY(!incomplete.committed);
    QVERIFY(!incomplete.errorMessage.isEmpty());
    const auto duplicateIds = runtime->applyTopology({
        {{QStringLiteral("local-to-dav"), QStringLiteral("missing")},
         {QStringLiteral("local-to-dav"), QStringLiteral("missing")}}, {mapping}});
    QVERIFY(!duplicateIds.committed);
    QVERIFY(!duplicateIds.errorMessage.isEmpty());
}

void TestPlanStanRuntimeContract::failedMaterializationPreservesPriorTopology()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    auto factory = QSharedPointer<PlanEndpointFactory>(new PlanEndpointFactory);
    definition.backendFactories.append(factory);
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));

    const auto initial = runtime->applyTopology({
        {{QStringLiteral("local"), QStringLiteral("plan-endpoint"),
          {{QStringLiteral("kind"), QStringLiteral("local")}}}}, {}});
    QVERIFY2(initial.committed, qPrintable(initial.errorMessage));
    const auto before = runtime->snapshot();

    factory->failCreation = true;
    const auto rejected = runtime->applyTopology({
        {{QStringLiteral("local"), QStringLiteral("plan-endpoint"),
          {{QStringLiteral("kind"), QStringLiteral("local")}}},
         {QStringLiteral("remote"), QStringLiteral("plan-endpoint"),
          {{QStringLiteral("kind"), QStringLiteral("fail")}}}}, {}});
    QVERIFY(!rejected.committed);
    QVERIFY(!rejected.repairRequired);
    QCOMPARE(runtime->snapshot().generation, before.generation);
    QCOMPARE(runtime->snapshot().backendIds, before.backendIds);
}

void TestPlanStanRuntimeContract::topologyRemovalMustNameExistingEndpoint()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    auto factory = QSharedPointer<PlanEndpointFactory>(new PlanEndpointFactory);
    definition.backendFactories.append(factory);
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));

    const auto initial = runtime->applyTopology({
        {{QStringLiteral("local"), QStringLiteral("plan-endpoint"),
          {{QStringLiteral("kind"), QStringLiteral("local")}}}}, {}});
    QVERIFY(initial.committed);
    const auto rejected = runtime->applyTopology({{}, {},
        {QStringLiteral("missing")} });
    QVERIFY(!rejected.committed);
    QVERIFY(rejected.errorMessage.contains(QStringLiteral("unknown endpoint")));
    QCOMPARE(runtime->snapshot().generation, initial.snapshot.generation);

    const auto removed = runtime->applyTopology({{}, {},
        {QStringLiteral("local")} });
    QVERIFY2(removed.committed, qPrintable(removed.errorMessage));
    QVERIFY(!runtime->snapshot().backendIds.contains(QStringLiteral("local")));
}

void TestPlanStanRuntimeContract::idOnlyTopologyCannotReportSuccessfulWork()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));

    const auto topology = runtime->applyTopology({{}, {}});
    QVERIFY(topology.committed);
    QVERIFY(runtime->snapshot().mappingIds.isEmpty());

    const auto run = runtime->run(
        {RunSelection::one(QStringLiteral("not-a-real-mapping")), RunIntent::Normal});
    QVERIFY(run.isFinished());
    QVERIFY(!run.result().success);
    QVERIFY(run.result().mappings.isEmpty());
}

void TestPlanStanRuntimeContract::localDavTopologyRegistersRuntimeOwnedBackends()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    auto factory = QSharedPointer<PlanEndpointFactory>(new PlanEndpointFactory);
    definition.backendFactories.append(factory);
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));

    Kalburator::Sync::SyncMapping mapping;
    mapping.id = QStringLiteral("local-to-dav");
    mapping.sourceBackend = QStringLiteral("local");
    mapping.sourceCalendar = QStringLiteral("local-calendar");
    mapping.targetBackend = QStringLiteral("dav");
    mapping.targetCalendar = QStringLiteral("dav-calendar");
    const auto topology = runtime->applyTopology({
        {{QStringLiteral("local"), QStringLiteral("plan-endpoint"),
          {{QStringLiteral("kind"), QStringLiteral("local")}}},
         {QStringLiteral("dav"), QStringLiteral("plan-endpoint"),
          {{QStringLiteral("kind"), QStringLiteral("dav")}}}}, {mapping}});
    QVERIFY(topology.committed);
    QCOMPARE(factory->lastRequest.endpointId, QStringLiteral("dav"));
    QCOMPARE(factory->lastRequest.factoryInput.value(QStringLiteral("kind")).toString(),
             QStringLiteral("dav"));

    auto run = runtime->run({RunSelection::one(mapping.id), RunIntent::Normal});
    QTRY_VERIFY_WITH_TIMEOUT(run.isFinished(), 30000);
    const auto result = run.result();
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QCOMPARE(result.mappings.size(), 1);
    QVERIFY(result.mappings.first().success);
    QVERIFY(factory->endpoints.value(QStringLiteral("dav"))->incidence(
        QStringLiteral("dav-calendar"), QStringLiteral("runtime-event")));
}

void TestPlanStanRuntimeContract::disconnectedProviderCannotEnterCommittedTopology()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    auto factory = QSharedPointer<PlanEndpointFactory>(new PlanEndpointFactory);
    definition.backendFactories.append(factory);
    Kalburator::Sync::BackendConfiguration provider;
    provider.id = QStringLiteral("dav");
    provider.type = QStringLiteral("caldav");
    definition.providers.append(provider);

    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));
    const auto topology = runtime->applyTopology({
        {{QStringLiteral("local"), QStringLiteral("plan-endpoint"), {},
          QStringLiteral("dav"), {}, {}}}, {}});
    QVERIFY(!topology.committed);
    QVERIFY(topology.errorMessage.contains(QStringLiteral("not connected")));
    QCOMPARE(factory->createCount, 0);
}

void TestPlanStanRuntimeContract::providerConnectionOwnsReadyBackendLifecycle()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    auto factory = QSharedPointer<PlanEndpointFactory>(new PlanEndpointFactory);
    definition.backendFactories.append(factory);
    Kalburator::Sync::BackendConfiguration provider;
    provider.id = QStringLiteral("dav");
    provider.type = QStringLiteral("caldav");
    provider.connectionParams.insert(QStringLiteral("url"), server.baseUrl().toString());
    provider.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    provider.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));
    definition.providers.append(provider);

    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));
    auto future = runtime->connectProviders();
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 10000);
    QVERIFY(future.result());
    QVERIFY(runtime->snapshot().backendIds.contains(QStringLiteral("dav:cal")));
    const auto topology = runtime->applyTopology({
        {{QStringLiteral("dav:cal"), {}, {}, QStringLiteral("dav")}}, {}});
    QVERIFY2(topology.committed, qPrintable(topology.errorMessage));
    QVERIFY(topology.snapshot.backendIds.contains(QStringLiteral("dav:cal")));
    QCOMPARE(topology.snapshot.mappings.size(), 0);

    const auto collision = runtime->applyTopology({
        {{QStringLiteral("dav:cal"), QStringLiteral("plan-endpoint")}}, {}});
    QVERIFY(!collision.committed);
    QVERIFY(collision.errorMessage.contains(QStringLiteral("collides")));
}

void TestPlanStanRuntimeContract::providerSnapshotPublishesTypedStateAndDiscovery()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    Kalburator::Sync::BackendConfiguration provider;
    provider.id = QStringLiteral("dav");
    provider.type = QStringLiteral("caldav");
    provider.displayName = QStringLiteral("Account");
    provider.connectionParams.insert(QStringLiteral("url"), server.baseUrl().toString());
    provider.connectionParams.insert(QStringLiteral("username"), QStringLiteral("user"));
    provider.connectionParams.insert(QStringLiteral("password"), QStringLiteral("secret"));
    definition.providers.append(provider);

    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));
    auto before = runtime->snapshot();
    QCOMPARE(before.providers.size(), 1);
    QCOMPARE(before.providers.first().id, QStringLiteral("dav"));
    QCOMPARE(before.providers.first().state,
             Kalburator::Sync::ProviderConnectionState::Disconnected);
    QVERIFY(before.providers.first().collections.isEmpty());
    QVERIFY(!before.providers.first().errorMessage.contains(QStringLiteral("secret")));

    QList<RuntimeEvent> events;
    runtime->setEventSink([&events](const RuntimeEvent &event) {
        if (event.kind == RuntimeEvent::Kind::ProviderStateChanged)
            events.append(event);
    });
    auto connected = runtime->connectProviders();
    QTRY_VERIFY_WITH_TIMEOUT(connected.isFinished(), 10000);
    QVERIFY(connected.result());

    const auto after = runtime->snapshot();
    const auto providerIt = std::find_if(after.providers.cbegin(), after.providers.cend(),
                                         [](const auto &item) { return item.id == QStringLiteral("dav"); });
    QVERIFY(providerIt != after.providers.cend());
    QCOMPARE(providerIt->state, Kalburator::Sync::ProviderConnectionState::Connected);
    QVERIFY(!providerIt->backendIds.isEmpty());
    QVERIFY(!providerIt->collections.isEmpty());
    QVERIFY(!providerIt->collections.first().id.isEmpty());
    QVERIFY(!events.isEmpty());
    QVERIFY(std::any_of(events.cbegin(), events.cend(), [](const auto &event) {
        return event.providerState == Kalburator::Sync::ProviderConnectionState::Connected
            && !event.providerCollections.isEmpty();
    }));
    for (const auto &event : events)
        QVERIFY(!event.providerError.contains(QStringLiteral("secret")));

    provider.displayName = QStringLiteral("Reconnected account");
    QVERIFY2(runtime->updateProvider(provider, error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(
        std::any_of(runtime->snapshot().providers.cbegin(),
                    runtime->snapshot().providers.cend(), [](const auto &item) {
                        return item.id == QStringLiteral("dav")
                            && item.state == Kalburator::Sync::ProviderConnectionState::Connected
                            && !item.collections.isEmpty();
                    }), 10000);
    const auto reconnected = runtime->snapshot().providers.first();
    QCOMPARE(reconnected.displayName, QStringLiteral("Reconnected account"));
    QVERIFY(!reconnected.collections.isEmpty());

    // The provider clears the old discovery atomically before reconnecting,
    // so stale collection IDs cannot survive a replacement.
    runtime.reset();
}

void TestPlanStanRuntimeContract::providerMutationIsIncrementalAndInvalidatesStaleTopology()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    auto factory = QSharedPointer<PlanEndpointFactory>(new PlanEndpointFactory);
    definition.backendFactories.append(factory);
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));

    Kalburator::Sync::SyncMapping mapping;
    mapping.id = QStringLiteral("local-mapping");
    mapping.sourceBackend = QStringLiteral("local");
    mapping.sourceCalendar = QStringLiteral("calendar");
    mapping.targetBackend = QStringLiteral("target");
    mapping.targetCalendar = QStringLiteral("calendar");
    QVERIFY(runtime->applyTopology({
        {{QStringLiteral("local"), QStringLiteral("plan-endpoint"),
          {{QStringLiteral("kind"), QStringLiteral("empty")}}},
         {QStringLiteral("target"), QStringLiteral("plan-endpoint"),
          {{QStringLiteral("kind"), QStringLiteral("empty")}}}},
        {mapping}}).committed);
    const auto before = runtime->snapshot();

    Kalburator::Sync::BackendConfiguration provider;
    provider.id = QStringLiteral("account");
    provider.type = QStringLiteral("caldav");
    provider.displayName = QStringLiteral("Account");
    QVERIFY2(runtime->addProvider(provider, error), qPrintable(error));
    QVERIFY(runtime->snapshot().providerIds.contains(provider.id));

    provider.displayName = QStringLiteral("Renamed account");
    QVERIFY2(runtime->updateProvider(provider, error), qPrintable(error));
    QCOMPARE(runtime->snapshot().mappingIds, QStringList{});
    QVERIFY(runtime->snapshot().generation > before.generation);

    QVERIFY2(runtime->removeProvider(provider.id, error), qPrintable(error));
    QVERIFY(!runtime->snapshot().providerIds.contains(provider.id));
    QVERIFY(!runtime->removeProvider(provider.id, error));
    QVERIFY(error.contains(QStringLiteral("unknown provider")));
}

void TestPlanStanRuntimeContract::canonicalRecordEventNeedsNoBackendReadOrNativeParse()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    auto factory = QSharedPointer<PlanEndpointFactory>(new PlanEndpointFactory);
    definition.backendFactories.append(factory);
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));

    QList<RuntimeEvent> events;
    QList<RuntimeEvent> telemetry;
    QThread *callbackThread = nullptr;
    runtime->setEventSink([&](const RuntimeEvent &event) {
        if (event.kind == RuntimeEvent::Kind::RecordChanged) {
            events.append(event);
            callbackThread = QThread::currentThread();
        }
        if (event.mappingStarted || event.mappingFinished || event.pass > 0)
            telemetry.append(event);
    });

    Kalburator::Sync::SyncMapping mapping;
    mapping.id = QStringLiteral("local-to-dav");
    mapping.sourceBackend = QStringLiteral("local");
    mapping.sourceCalendar = QStringLiteral("local-calendar");
    mapping.targetBackend = QStringLiteral("dav");
    mapping.targetCalendar = QStringLiteral("dav-calendar");
    QVERIFY(runtime->applyTopology({
        {{QStringLiteral("local"), QStringLiteral("plan-endpoint"),
          {{QStringLiteral("kind"), QStringLiteral("empty")}}},
         {QStringLiteral("dav"), QStringLiteral("plan-endpoint"),
          {{QStringLiteral("kind"), QStringLiteral("remote-seeded")}}}},
        {mapping}}).committed);
    QCOMPARE(runtime->snapshot().mappings.size(), 1);
    QCOMPARE(runtime->snapshot().mappings.first().sourceCollectionId,
             QStringLiteral("local-calendar"));

    auto run = runtime->run({RunSelection::one(mapping.id), RunIntent::Normal});
    QTRY_VERIFY_WITH_TIMEOUT(run.isFinished(), 30000);
    QVERIFY2(run.result().success, qPrintable(run.result().errorMessage));
    QTRY_VERIFY_WITH_TIMEOUT(std::any_of(telemetry.cbegin(), telemetry.cend(),
                                         [](const auto &event) {
        return event.mappingId == QStringLiteral("local-to-dav") && event.mappingStarted;
    }), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(std::any_of(telemetry.cbegin(), telemetry.cend(),
                                         [](const auto &event) {
        return event.mappingId == QStringLiteral("local-to-dav")
            && event.mappingFinished && event.mappingSuccess
            && event.lastSuccessfulSync.isValid();
    }), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(events.size(), 1, 5000);
    const auto event = events.first();
    QCOMPARE(callbackThread, QThread::currentThread());
    QCOMPARE(event.mappingId, mapping.id);
    QCOMPARE(event.backendId, QStringLiteral("local"));
    QCOMPARE(event.collectionId, QStringLiteral("local-calendar"));
    QCOMPARE(event.recordId, QStringLiteral("remote-event"));
    QCOMPARE(event.changeKind, RecordChangeKind::Created);
    QCOMPARE(event.canonicalDomain, QStringLiteral("calendar"));
    QCOMPARE(event.canonicalEncoding, QStringLiteral("canon"));
    QCOMPARE(event.canonicalSchemaVersion, 1);
    const auto json = QJsonDocument::fromJson(event.canonicalPayload).object();
    QCOMPARE(json.value(QStringLiteral("summary")).toString(),
             QStringLiteral("canonical runtime event"));
}

void TestPlanStanRuntimeContract::runTelemetryUsesTypedProgressKinds()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    auto factory = QSharedPointer<PlanEndpointFactory>(new PlanEndpointFactory);
    definition.backendFactories.append(factory);
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));

    QList<RuntimeEvent> events;
    runtime->setEventSink([&](const RuntimeEvent &event) { events.append(event); });
    const Kalburator::Sync::SyncMapping mapping{
        QStringLiteral("telemetry"), QStringLiteral("source"),
        QStringLiteral("calendar"), QStringLiteral("target"),
        QStringLiteral("calendar")};
    QVERIFY(runtime->applyTopology({
        {{QStringLiteral("source"), QStringLiteral("plan-endpoint"),
          {{QStringLiteral("kind"), QStringLiteral("empty")}}},
         {QStringLiteral("target"), QStringLiteral("plan-endpoint"),
          {{QStringLiteral("kind"), QStringLiteral("empty")}}}},
        {mapping}}).committed);

    auto run = runtime->run({RunSelection::one(mapping.id), RunIntent::Normal});
    QTRY_VERIFY_WITH_TIMEOUT(run.isFinished(), 30000);
    QVERIFY(run.result().success);
    QTRY_VERIFY_WITH_TIMEOUT(std::any_of(events.cbegin(), events.cend(),
        [](const RuntimeEvent &event) {
            return event.progressKind == RunProgressKind::MappingStarted;
        }), 5000);
    QVERIFY(std::any_of(events.cbegin(), events.cend(),
        [](const RuntimeEvent &event) {
            return event.progressKind == RunProgressKind::MappingFinished
                && event.mappingSuccess && event.lastSuccessfulSync.isValid();
        }));
    QVERIFY(std::any_of(events.cbegin(), events.cend(),
        [](const RuntimeEvent &event) {
            return event.kind == RuntimeEvent::Kind::RunFinished;
        }));

    RuntimeEvent collectionEvent;
    collectionEvent.kind = RuntimeEvent::Kind::CollectionChanged;
    collectionEvent.collectionChangeKind = CollectionChangeKind::Created;
    collectionEvent.collection.id = QStringLiteral("calendar");
    collectionEvent.collectionMutationCommitted = true;
    QCOMPARE(collectionEvent.collection.id, QStringLiteral("calendar"));
    QVERIFY(collectionEvent.collectionMutationCommitted);
}

void TestPlanStanRuntimeContract::runtimePolicyHasValidatedConstructionAndUpdateBoundary()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition invalid;
    invalid.storagePath = profile.filePath(QStringLiteral("invalid.db"));
    invalid.policy.maxConcurrentMappings = 0;
    QString error;
    QVERIFY(CollectionRuntime::create(invalid, error) == nullptr);
    QVERIFY(error.contains(QStringLiteral("concurrency")));

    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    auto factory = QSharedPointer<PlanEndpointFactory>(new PlanEndpointFactory);
    definition.backendFactories.append(factory);
    definition.policy.maxConcurrentMappings = 2;
    definition.policy.skipUnchangedMappings = false;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));
    RuntimePolicy updated = definition.policy;
    updated.maxConcurrentMappings = 3;
    QVERIFY2(runtime->updatePolicy(updated, error), qPrintable(error));

    const Kalburator::Sync::SyncMapping mapping{
        QStringLiteral("policy"), QStringLiteral("source"),
        QStringLiteral("calendar"), QStringLiteral("target"),
        QStringLiteral("calendar")};
    QVERIFY(runtime->applyTopology({
        {{QStringLiteral("source"), QStringLiteral("plan-endpoint"),
          {{QStringLiteral("kind"), QStringLiteral("empty")}}},
         {QStringLiteral("target"), QStringLiteral("plan-endpoint"),
          {{QStringLiteral("kind"), QStringLiteral("empty")}}}},
        {mapping}}).committed);
    auto run = runtime->run({RunSelection::one(mapping.id), RunIntent::Normal});
    QTest::qWait(20);
    QVERIFY(!run.isFinished());
    RuntimePolicy rejected = updated;
    rejected.interaction = RunInteraction::Monitored;
    QVERIFY(!runtime->updatePolicy(rejected, error));
    QVERIFY(error.contains(QStringLiteral("active")));
    runtime->cancel();
    QTRY_VERIFY_WITH_TIMEOUT(run.isFinished(), 30000);
}

void TestPlanStanRuntimeContract::topologyPersistenceCommitsBeforePublication()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    auto persistence = QSharedPointer<TopologyPersistenceFake>(new TopologyPersistenceFake);
    definition.topologyPersistence = persistence;
    definition.backendFactories.append(QSharedPointer<PlanEndpointFactory>(new PlanEndpointFactory));
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));
    QVERIFY(runtime->applyTopology({}).committed);
    QCOMPARE(persistence->prepareCount, 1);
    QCOMPARE(persistence->commitCount, 1);
    QCOMPARE(persistence->rollbackCount, 0);

    const auto before = runtime->snapshot();
    persistence->failCommit = true;
    const auto failed = runtime->applyTopology({
        {{QStringLiteral("new-endpoint"), QStringLiteral("plan-endpoint"), {}, {}, {}, {}}},
        {}});
    QVERIFY(!failed.committed);
    QCOMPARE(runtime->snapshot().generation, before.generation);
    QCOMPARE(persistence->rollbackCount, 1);

    persistence->failCommit = false;
    QVERIFY(runtime->applyTopology({}).committed);
    QCOMPARE(persistence->prepareCount, 3);
    QCOMPARE(persistence->commitCount, 3);
}

void TestPlanStanRuntimeContract::providerEditsRollbackWithTopologyPersistence()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    auto persistence = QSharedPointer<TopologyPersistenceFake>(new TopologyPersistenceFake);
    definition.topologyPersistence = persistence;
    Kalburator::Sync::BackendConfiguration provider;
    provider.id = QStringLiteral("dav");
    provider.type = QStringLiteral("caldav");
    provider.connectionParams.insert(QStringLiteral("url"), server.baseUrl().toString());
    provider.connectionParams.insert(QStringLiteral("username"), QStringLiteral("user"));
    provider.connectionParams.insert(QStringLiteral("password"), QStringLiteral("secret"));
    definition.providers.append(provider);

    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));
    auto connected = runtime->connectProviders();
    QTRY_VERIFY_WITH_TIMEOUT(connected.isFinished(), 10000);
    QVERIFY(connected.result());
    const auto before = runtime->snapshot();

    persistence->failCommit = true;
    TopologyDefinition desired;
    desired.replaceProviders = true;
    const auto failed = runtime->applyTopology(desired);
    QVERIFY(!failed.committed);
    QCOMPARE(runtime->snapshot().providerIds, before.providerIds);
    QCOMPARE(runtime->snapshot().providers.size(), 1);
    QCOMPARE(runtime->snapshot().providers.first().id, QStringLiteral("dav"));
    QCOMPARE(runtime->snapshot().providers.first().state,
             Kalburator::Sync::ProviderConnectionState::Connected);
}

void TestPlanStanRuntimeContract::collectionMutationsPublishAfterTopologyCommit()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    definition.backendFactories.append(QSharedPointer<PlanEndpointFactory>(new PlanEndpointFactory));
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));
    QList<RuntimeEvent> events;
    runtime->setEventSink([&events](const RuntimeEvent &event) { events.append(event); });

    CollectionMutation mutation;
    mutation.kind = CollectionMutationKind::Create;
    mutation.endpointId = QStringLiteral("local");
    mutation.collection.id = QStringLiteral("created");
    mutation.collection.name = QStringLiteral("Created");
    mutation.collection.type = QStringLiteral("calendar");
    TopologyDefinition topology;
    topology.endpoints.append({QStringLiteral("local"), QStringLiteral("plan-endpoint"), {}, {}, {}, {}});
    topology.collectionMutations.append(mutation);
    const auto result = runtime->applyTopology(topology);
    QVERIFY2(result.committed, qPrintable(result.errorMessage));
    QVERIFY(std::any_of(events.cbegin(), events.cend(), [](const auto &event) {
        return event.kind == RuntimeEvent::Kind::CollectionChanged
            && event.collectionMutationCommitted
            && event.collectionChangeKind == CollectionChangeKind::Created;
    }));
}

void TestPlanStanRuntimeContract::collectionMutationVerbsAndCompensationAreExplicit()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    auto factory = QSharedPointer<PlanEndpointFactory>(new PlanEndpointFactory);
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    definition.backendFactories.append(factory);
    auto persistence = QSharedPointer<TopologyPersistenceFake>(new TopologyPersistenceFake);
    definition.topologyPersistence = persistence;
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));
    QList<RuntimeEvent> events;
    runtime->setEventSink([&events](const RuntimeEvent &event) { events.append(event); });

    const TopologyEndpoint endpoint{
        QStringLiteral("local"), QStringLiteral("plan-endpoint"), {}, {}, {}, {}};
    auto apply = [&](QList<CollectionMutation> mutations) {
        TopologyDefinition topology;
        topology.endpoints.append(endpoint);
        topology.collectionMutations = std::move(mutations);
        return runtime->applyTopology(topology);
    };
    CollectionMutation create;
    create.kind = CollectionMutationKind::Create;
    create.endpointId = QStringLiteral("local");
    create.collection = {QStringLiteral("c1"), QStringLiteral("First"), QStringLiteral("calendar")};
    CollectionMutation adopt;
    adopt.kind = CollectionMutationKind::Adopt;
    adopt.endpointId = QStringLiteral("local");
    adopt.collectionId = QStringLiteral("c1");
    CollectionMutation update;
    update.kind = CollectionMutationKind::Update;
    update.endpointId = QStringLiteral("local");
    update.collectionId = QStringLiteral("c1");
    update.metadata.insert(QStringLiteral("name"), QStringLiteral("Renamed"));
    CollectionMutation rename;
    rename.kind = CollectionMutationKind::Rename;
    rename.endpointId = QStringLiteral("local");
    rename.collectionId = QStringLiteral("c1");
    rename.metadata.insert(QStringLiteral("newCollectionId"), QStringLiteral("c2"));
    CollectionMutation untrack;
    untrack.kind = CollectionMutationKind::Untrack;
    untrack.endpointId = QStringLiteral("local");
    untrack.collectionId = QStringLiteral("c2");
    CollectionMutation destroy;
    destroy.kind = CollectionMutationKind::Destroy;
    destroy.endpointId = QStringLiteral("local");
    destroy.collectionId = QStringLiteral("c2");
    const auto mutationResult = apply({create, adopt, update, rename, untrack, destroy});
    QVERIFY(mutationResult.committed);
    QCOMPARE(std::count_if(events.cbegin(), events.cend(), [](const auto &event) {
        return event.kind == RuntimeEvent::Kind::CollectionChanged;
    }), 4);
    QVERIFY(std::any_of(events.cbegin(), events.cend(), [](const auto &event) {
        return event.kind == RuntimeEvent::Kind::CollectionChanged
            && event.collectionId == QStringLiteral("c2")
            && event.collectionChangeKind == CollectionChangeKind::Updated;
    }));

    persistence->failCommit = true;
    CollectionMutation compensatedCreate;
    compensatedCreate.kind = CollectionMutationKind::Create;
    compensatedCreate.endpointId = QStringLiteral("local");
    compensatedCreate.collection = {QStringLiteral("compensated"), QStringLiteral("Temporary"), QStringLiteral("calendar")};
    const auto failed = apply({compensatedCreate});
    QVERIFY(!failed.committed);
    QVERIFY(!failed.repairRequired);
}

void TestPlanStanRuntimeContract::unsupportedCollectionMutationIsTypedFailure()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    definition.backendFactories.append(QSharedPointer<PlanEndpointFactory>(new PlanEndpointFactory));
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));
    TopologyDefinition topology;
    topology.endpoints.append({QStringLiteral("read-only"), QStringLiteral("plan-endpoint"),
                                {{QStringLiteral("kind"), QStringLiteral("read-only")}}, {}, {}, {}});
    CollectionMutation update;
    update.kind = CollectionMutationKind::Update;
    update.endpointId = QStringLiteral("read-only");
    update.collectionId = QStringLiteral("missing");
    topology.collectionMutations.append(update);
    const auto result = runtime->applyTopology(topology);
    QVERIFY(!result.committed);
    QVERIFY(result.errorMessage.contains(QStringLiteral("unsupported")));
    QVERIFY(!result.repairRequired);
}

void TestPlanStanRuntimeContract::topologyReplacementIsRejectedWhileRunIsActive()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    definition.backendFactories.append(QSharedPointer<PlanEndpointFactory>(new PlanEndpointFactory));
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));

    const Kalburator::Sync::SyncMapping mapping{
        QStringLiteral("held-run"), QStringLiteral("source"),
        QStringLiteral("local-calendar"), QStringLiteral("target"),
        QStringLiteral("target-calendar")};
    TopologyDefinition topology;
    topology.endpoints = {
        {QStringLiteral("source"), QStringLiteral("plan-endpoint"),
         {{QStringLiteral("kind"), QStringLiteral("local")},
          {QStringLiteral("delayMs"), 1500}}},
        {QStringLiteral("target"), QStringLiteral("plan-endpoint"),
         {{QStringLiteral("kind"), QStringLiteral("empty")},
          {QStringLiteral("delayMs"), 1500}}}};
    topology.mappings = {mapping};
    QVERIFY2(runtime->applyTopology(topology).committed, qPrintable(error));
    const auto before = runtime->snapshot();

    const auto run = runtime->run({RunSelection::allEnabled(), RunIntent::Normal});
    QVERIFY(run.isStarted());
    QVERIFY2(QTest::qWaitFor([&] { return !run.isFinished(); }, 1000),
             "the held backend run finished before topology rejection was exercised");

    const auto replacement = runtime->applyTopology(topology);
    QVERIFY(!replacement.committed);
    QVERIFY(replacement.errorMessage.contains(QStringLiteral("active")));
    QCOMPARE(runtime->snapshot().generation, before.generation);
    QCOMPARE(runtime->snapshot().mappingIds, before.mappingIds);

    QTRY_VERIFY_WITH_TIMEOUT(run.isFinished(), 10000);
}

void TestPlanStanRuntimeContract::invalidLaterCollectionMutationHasNoEarlierPhysicalEffect()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    auto factory = QSharedPointer<PlanEndpointFactory>(new PlanEndpointFactory);
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    definition.backendFactories.append(factory);
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));

    CollectionMutation create;
    create.kind = CollectionMutationKind::Create;
    create.endpointId = QStringLiteral("local");
    create.collection = {QStringLiteral("created-first"), QStringLiteral("Created"),
                         QStringLiteral("calendar")};
    CollectionMutation malformedLater;
    malformedLater.kind = CollectionMutationKind::Update;
    malformedLater.endpointId = QStringLiteral("local");
    // No collection id: this must be rejected before create reaches the backend.
    TopologyDefinition topology;
    topology.endpoints = {{QStringLiteral("local"), QStringLiteral("plan-endpoint"),
                           {{QStringLiteral("kind"), QStringLiteral("mutation-probe")}},
                           {}, {}, {}}};
    topology.collectionMutations = {create, malformedLater};

    const auto result = runtime->applyTopology(topology);
    QVERIFY(!result.committed);
    QVERIFY(result.errorMessage.contains(QStringLiteral("no collection id")));
    QCOMPARE(factory->mutationProbe->creates.loadRelaxed(), 0);
    QCOMPARE(factory->mutationProbe->deletes.loadRelaxed(), 0);
}

void TestPlanStanRuntimeContract::failedLaterCollectionMutationCompensatesEarlierCreate()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    auto factory = QSharedPointer<PlanEndpointFactory>(new PlanEndpointFactory);
    factory->mutationProbe->failCreateAfter.storeRelaxed(1);
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    definition.backendFactories.append(factory);
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));

    auto create = [](const QString &id) {
        CollectionMutation mutation;
        mutation.kind = CollectionMutationKind::Create;
        mutation.endpointId = QStringLiteral("local");
        mutation.collection = {id, id, QStringLiteral("calendar")};
        return mutation;
    };
    TopologyDefinition topology;
    topology.endpoints = {{QStringLiteral("local"), QStringLiteral("plan-endpoint"),
                           {{QStringLiteral("kind"), QStringLiteral("mutation-probe")}},
                           {}, {}, {}}};
    topology.collectionMutations = {create(QStringLiteral("first")),
                                    create(QStringLiteral("second"))};

    const auto result = runtime->applyTopology(topology);
    QVERIFY(!result.committed);
    QVERIFY(!result.repairRequired);
    QCOMPARE(factory->mutationProbe->creates.loadRelaxed(), 1);
    QCOMPARE(factory->mutationProbe->deletes.loadRelaxed(), 1);
    QCOMPARE(factory->mutationProbe->executorThreadCalls.loadRelaxed(), 2);
}

void TestPlanStanRuntimeContract::reentrantTopologyCommandIsRejectedDuringCommitNotification()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    definition.backendFactories.append(QSharedPointer<PlanEndpointFactory>(new PlanEndpointFactory));
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));

    TopologyDefinition topology;
    topology.endpoints = {{QStringLiteral("local"), QStringLiteral("plan-endpoint"), {}, {}, {}, {}}};
    TopologyResult reentrant;
    runtime->setEventSink([&](const RuntimeEvent &event) {
        if (event.kind == RuntimeEvent::Kind::TopologyCommitted)
            reentrant = runtime->applyTopology(topology);
    });

    const auto committed = runtime->applyTopology(topology);
    QVERIFY2(committed.committed, qPrintable(committed.errorMessage));
    QVERIFY(!reentrant.committed);
    QVERIFY(reentrant.errorMessage.contains(QStringLiteral("already in progress")));
}

void TestPlanStanRuntimeContract::reentrantRunAndProviderCommandsAreRejectedDuringTopologyCommit()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    definition.backendFactories.append(QSharedPointer<PlanEndpointFactory>(new PlanEndpointFactory));
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));

    TopologyDefinition topology;
    topology.endpoints = {{QStringLiteral("local"), QStringLiteral("plan-endpoint"), {}, {}, {}, {}}};
    Kalburator::Runtime::RunResult reentrantRun;
    QString providerError;
    runtime->setEventSink([&](const RuntimeEvent &event) {
        if (event.kind != RuntimeEvent::Kind::TopologyCommitted)
            return;
        reentrantRun = runtime->run({RunSelection::one(QStringLiteral("any-mapping")),
                                     RunIntent::Normal}).result();
        QVERIFY(!runtime->removeProvider(QStringLiteral("any-provider"), providerError));
    });

    const auto committed = runtime->applyTopology(topology);
    QVERIFY2(committed.committed, qPrintable(committed.errorMessage));
    QVERIFY(!reentrantRun.success);
    QVERIFY2(reentrantRun.errorMessage.contains(QStringLiteral("topology")),
             qPrintable(reentrantRun.errorMessage));
    QVERIFY(providerError.contains(QStringLiteral("topology")));
}

void TestPlanStanRuntimeContract::failedCollectionMutationDoesNotPublishAndReportsRepair()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    definition.backendFactories.append(QSharedPointer<PlanEndpointFactory>(new PlanEndpointFactory));
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));
    QList<RuntimeEvent> events;
    runtime->setEventSink([&events](const RuntimeEvent &event) { events.append(event); });

    CollectionMutation mutation;
    mutation.kind = CollectionMutationKind::Destroy;
    mutation.endpointId = QStringLiteral("local");
    mutation.collectionId = QStringLiteral("not-present");
    TopologyDefinition topology;
    topology.endpoints.append({QStringLiteral("local"), QStringLiteral("plan-endpoint"), {}, {}, {}, {}});
    topology.collectionMutations.append(mutation);
    const auto result = runtime->applyTopology(topology);
    QVERIFY(!result.committed);
    QVERIFY(result.repairRequired);
    QVERIFY(std::none_of(events.cbegin(), events.cend(), [](const auto &event) {
        return event.kind == RuntimeEvent::Kind::CollectionChanged;
    }));
}

void TestPlanStanRuntimeContract::conflictsArePersistedSurfacedAndResolvableThroughRuntime()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    auto factory = QSharedPointer<PlanEndpointFactory>(new PlanEndpointFactory);
    definition.backendFactories.append(factory);
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));

    auto event = [](const QString &summary) {
        auto result = KCalendarCore::Event::Ptr(new KCalendarCore::Event);
        result->setUid(QStringLiteral("conflict-event"));
        result->setSummary(summary);
        result->setDtStart(QDateTime::currentDateTimeUtc());
        result->setDtEnd(QDateTime::currentDateTimeUtc().addSecs(3600));
        return result;
    };
    const Kalburator::Sync::SyncMapping mapping{
        QStringLiteral("conflict-mapping"), QStringLiteral("source"),
        QStringLiteral("calendar"), QStringLiteral("target"),
        QStringLiteral("calendar")};
    QVERIFY(runtime->applyTopology({
        {{QStringLiteral("source"), QStringLiteral("plan-endpoint"),
          {{QStringLiteral("kind"), QStringLiteral("empty")}}},
         {QStringLiteral("target"), QStringLiteral("plan-endpoint"),
          {{QStringLiteral("kind"), QStringLiteral("empty")}}}},
        {mapping}}).committed);

    auto *source = factory->endpoints.value(QStringLiteral("source"));
    auto *target = factory->endpoints.value(QStringLiteral("target"));
    QVERIFY(source);
    QVERIFY(target);
    source->addIncidence(QStringLiteral("calendar"), event(QStringLiteral("initial")));
    target->addIncidence(QStringLiteral("calendar"), event(QStringLiteral("initial")));

    auto first = runtime->run({RunSelection::one(mapping.id), RunIntent::Normal});
    QTRY_VERIFY_WITH_TIMEOUT(first.isFinished(), 30000);
    QVERIFY2(first.result().success, qPrintable(first.result().errorMessage));

    source->addIncidence(QStringLiteral("calendar"), event(QStringLiteral("source edit")));
    target->addIncidence(QStringLiteral("calendar"), event(QStringLiteral("target edit")));

    QList<RuntimeEvent> conflicts;
    runtime->setEventSink([&](const RuntimeEvent &event) {
        if (event.kind == RuntimeEvent::Kind::ConflictDetected
            || event.kind == RuntimeEvent::Kind::ConflictResolved)
            conflicts.append(event);
    });
    auto second = runtime->run({RunSelection::one(mapping.id), RunIntent::Normal});
    QTRY_VERIFY_WITH_TIMEOUT(second.isFinished(), 30000);
    QVERIFY(!second.result().success);
    QTRY_VERIFY_WITH_TIMEOUT(!conflicts.isEmpty(), 5000);
    const auto detected = std::find_if(
        conflicts.cbegin(), conflicts.cend(), [](const RuntimeEvent &event) {
            return event.kind == RuntimeEvent::Kind::ConflictDetected;
        });
    QVERIFY(detected != conflicts.cend());
    QVERIFY(!detected->conflict.conflictId.isEmpty());
    QCOMPARE(detected->conflict.mappingId, mapping.id);
    const QString conflictId = detected->conflict.conflictId;
    const auto backlog = runtime->unresolvedConflicts();
    QCOMPARE(backlog.size(), 1);
    QCOMPARE(backlog.first().conflictId, conflictId);
    QCOMPARE(backlog.first().mappingId, mapping.id);

    runtime.reset();
    runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));
    const auto reboundBacklog = runtime->unresolvedConflicts();
    QCOMPARE(reboundBacklog.size(), 1);
    QCOMPARE(reboundBacklog.first().conflictId, conflictId);
    QCOMPARE(reboundBacklog.first().mappingId, mapping.id);
    runtime->setEventSink([&](const RuntimeEvent &event) {
        if (event.kind == RuntimeEvent::Kind::ConflictResolved)
            conflicts.append(event);
    });
    QVERIFY(runtime->resolveConflict(conflictId,
                                     Kalburator::Sync::ConflictResolution::SourceWins));
    QVERIFY(runtime->unresolvedConflicts().isEmpty());
    const auto resolved = std::find_if(
        conflicts.cbegin(), conflicts.cend(), [](const RuntimeEvent &event) {
            return event.kind == RuntimeEvent::Kind::ConflictResolved;
        });
    QVERIFY(resolved != conflicts.cend());
    QCOMPARE(resolved->objectId, conflictId);
}

void TestPlanStanRuntimeContract::activeRunDestructionCancelsAndCompletesFuture()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    RuntimeDefinition definition;
    definition.storagePath = profile.filePath(QStringLiteral("sync.db"));
    auto factory = QSharedPointer<PlanEndpointFactory>(new PlanEndpointFactory);
    definition.backendFactories.append(factory);
    QString error;
    auto runtime = CollectionRuntime::create(definition, error);
    QVERIFY2(runtime != nullptr, qPrintable(error));

    Kalburator::Sync::SyncMapping mapping;
    mapping.id = QStringLiteral("slow");
    mapping.sourceBackend = QStringLiteral("local");
    mapping.sourceCalendar = QStringLiteral("local-calendar");
    mapping.targetBackend = QStringLiteral("dav");
    mapping.targetCalendar = QStringLiteral("dav-calendar");
    QVERIFY(runtime->applyTopology({
        {{QStringLiteral("local"), QStringLiteral("plan-endpoint"),
          {{QStringLiteral("kind"), QStringLiteral("local")},
           {QStringLiteral("delayMs"), 2000}}},
         {QStringLiteral("dav"), QStringLiteral("plan-endpoint"),
          {{QStringLiteral("kind"), QStringLiteral("empty")},
           {QStringLiteral("delayMs"), 2000}}}},
        {mapping}}).committed);

    auto run = runtime->run({RunSelection::one(mapping.id), RunIntent::Normal});
    QTest::qWait(20);
    QVERIFY(!run.isFinished());
    QElapsedTimer elapsed;
    elapsed.start();
    runtime.reset();
    QVERIFY2(elapsed.elapsed() < 5000, "active runtime teardown exceeded its bound");
    QVERIFY(run.isFinished());
    QVERIFY(run.result().cancelled);
}

QTEST_MAIN(TestPlanStanRuntimeContract)
#include "tst_planstan_runtime_contract.moc"
