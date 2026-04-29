#ifndef KALBURATOR_TEST_STUBSYNCCONFIGSTORE_H
#define KALBURATOR_TEST_STUBSYNCCONFIGSTORE_H

#include <QHash>
#include <QList>
#include <QString>
#include <QVariantMap>

#include "isyncconfigstore.h"
#include "logicalcalendar.h"
#include "synctypes.h"

namespace Kalburator::Sync::Test {

/**
 * @brief In-memory ISyncConfigStore for libkalburator integration tests.
 *
 * Holds logical-calendar entries, backend config maps, and sync mappings
 * in plain hashes. Tests seed via setMappings()/setBackendConfig() and
 * inspect saveCount() to verify save() was invoked.
 */
class StubSyncConfigStore : public ISyncConfigStore
{
public:
    StubSyncConfigStore() = default;
    ~StubSyncConfigStore() override = default;

    // ISyncConfigStore
    void addLogicalCalendar(const LogicalCalendar &logCal) override;
    void updateLogicalCalendar(const LogicalCalendar &logCal) override;
    void removeLogicalCalendar(const QString &logicalCalendarId) override;
    LogicalCalendar logicalCalendar(const QString &logicalCalendarId) const override;

    QVariantMap backendConfig(const QString &backendId) const override;

    bool hasSyncMappings() const override { return !m_mappings.isEmpty(); }
    QList<SyncMapping> syncMappings() const override { return m_mappings; }

    void save() override { ++m_saveCount; }

    // Test helpers
    void setMappings(QList<SyncMapping> mappings) { m_mappings = std::move(mappings); }
    void setBackendConfig(const QString &backendId, const QVariantMap &cfg) {
        m_backendConfigs.insert(backendId, cfg);
    }
    int saveCount() const { return m_saveCount; }

private:
    QHash<QString, LogicalCalendar> m_logicalCalendars;
    QHash<QString, QVariantMap>     m_backendConfigs;
    QList<SyncMapping>              m_mappings;
    int m_saveCount = 0;
};

} // namespace Kalburator::Sync::Test

#endif // KALBURATOR_TEST_STUBSYNCCONFIGSTORE_H
