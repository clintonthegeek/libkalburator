#ifndef KALBURATOR_TEST_STUBSYNCHOST_H
#define KALBURATOR_TEST_STUBSYNCHOST_H

#include <memory>

#include <QHash>
#include <QList>
#include <QString>

#include "isynchost.h"
#include "stubcalendarcollection.h"
#include "stubincidenceregistry.h"
#include "stubsyncconfigstore.h"

namespace Kalburator::Sync {
class BackendRegistry;
class SyncBackend;
}

namespace Kalburator::Sync::Test {

/**
 * @brief Library-side ISyncHost stub for integration tests.
 *
 * Implements the G.9.a narrowed ISyncHost: backendById, backends,
 * configStore plus the new generic lifecycle events. Records
 * recordChanged() calls for test assertions.
 */
class StubSyncHost : public ISyncHost
{
public:
    explicit StubSyncHost(BackendRegistry *registry);
    ~StubSyncHost() override;

    // ISyncHost
    SyncBackend* backendById(const QString &id) override;
    QHash<QString, SyncBackend*> backends() override;
    ISyncConfigStore* configStore() override { return m_config.get(); }

    // G.9.a generic lifecycle events
    void recordChanged(const QString &mappingId,
                       const QString &recordId,
                       ChangeKind kind) override;

    // Test inspection
    int recordChangedCount() const { return m_recordChangedCount; }
    int recordChangedCount(ChangeKind kind) const;

    StubCalendarCollection* stubCollection() { return m_collection.get(); }
    StubIncidenceRegistry*  stubRegistry()   { return m_registry.get(); }
    StubSyncConfigStore*    stubConfig()     { return m_config.get(); }

private:
    BackendRegistry *m_backendRegistry;   // not owned

    std::unique_ptr<StubCalendarCollection> m_collection;
    std::unique_ptr<StubIncidenceRegistry>  m_registry;
    std::unique_ptr<StubSyncConfigStore>    m_config;

    int m_recordChangedCount = 0;
    int m_createdCount = 0;
    int m_updatedCount = 0;
    int m_deletedCount = 0;
};

} // namespace Kalburator::Sync::Test

#endif // KALBURATOR_TEST_STUBSYNCHOST_H
