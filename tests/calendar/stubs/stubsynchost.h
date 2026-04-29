#ifndef KALBURATOR_TEST_STUBSYNCHOST_H
#define KALBURATOR_TEST_STUBSYNCHOST_H

#include <memory>

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>

#include <KCalendarCore/Incidence>

#include "isynchost.h"
#include "stubcalendarcollection.h"
#include "stubincidenceregistry.h"
#include "stubsyncconfigstore.h"

namespace Kalburator::Sync {
class BackendRegistry;
class SyncBackend;
class IIncidenceSource;
}

namespace Kalburator::Sync::Test {

/**
 * @brief Library-side ISyncHost stub for integration tests.
 *
 * Lifts the pattern from PlanStan's
 * `tests/sync-workflow/tst_sync_error_recovery.cpp:64` and completes
 * the API: real implementations of applyIncidence*, collection(),
 * incidenceRegistry(), configStore(), and a recorded log of every
 * applyIncidence* call for assertions.
 *
 * Owns the three sub-stubs (collection / registry / config). Does
 * NOT own the BackendRegistry or IIncidenceSource — caller-managed.
 */
class StubSyncHost : public ISyncHost
{
public:
    struct AppliedChange {
        enum class Kind { Add, Remove, Update };
        Kind kind;
        QString calendarId;
        QString uid;
        KCalendarCore::Incidence::Ptr incidence;
        bool stageForSync;
    };

    explicit StubSyncHost(BackendRegistry *registry,
                          IIncidenceSource *source = nullptr);
    ~StubSyncHost() override;

    // ISyncHost
    SyncBackend* backendById(const QString &id) override;
    QHash<QString, SyncBackend*> backends() override;

    bool applyIncidenceAddition(const QString &calendarId,
                                const KCalendarCore::Incidence::Ptr &inc,
                                bool stageForSync = true) override;
    bool applyIncidenceRemoval(const QString &calendarId,
                               const QString &uid,
                               bool stageForSync = true,
                               const QDateTime &recurrenceId = {}) override;
    bool applyIncidenceUpdate(const QString &calendarId,
                              const KCalendarCore::Incidence::Ptr &inc,
                              bool stageForSync = true) override;

    ICalendarCollection* collection() override   { return m_collection.get(); }
    IIncidenceSource*    incidenceSource() override   { return m_source; }
    IIncidenceRegistry*  incidenceRegistry() override { return m_registry.get(); }
    ISyncConfigStore*    configStore() override       { return m_config.get(); }

    void unloadCalendar(const QString &) override {}
    void generateSyncMappingsFromLogicalCalendars() override {}

    // Test inspection
    QList<AppliedChange> appliedChanges() const { return m_appliedChanges; }
    int appliedAdditionCount() const;
    int appliedRemovalCount() const;
    int appliedUpdateCount() const;

    StubCalendarCollection* stubCollection() { return m_collection.get(); }
    StubIncidenceRegistry*  stubRegistry()   { return m_registry.get(); }
    StubSyncConfigStore*    stubConfig()     { return m_config.get(); }

    // Allow tests to substitute a custom IIncidenceSource after construction.
    void setIncidenceSource(IIncidenceSource *source) { m_source = source; }

private:
    BackendRegistry *m_backendRegistry;        // not owned
    IIncidenceSource *m_source;                // not owned

    std::unique_ptr<StubCalendarCollection> m_collection;
    std::unique_ptr<StubIncidenceRegistry>  m_registry;
    std::unique_ptr<StubSyncConfigStore>    m_config;

    QList<AppliedChange> m_appliedChanges;
};

} // namespace Kalburator::Sync::Test

#endif // KALBURATOR_TEST_STUBSYNCHOST_H
