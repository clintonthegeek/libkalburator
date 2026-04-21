#ifndef ORGBACKEND_H
#define ORGBACKEND_H

#include "syncbackend.h"
#include "orgfilemanager.h"

namespace Kalburator::Sync {

struct BackendCapabilities;

class OrgBackend : public SyncBackend
{
    Q_OBJECT
public:
    explicit OrgBackend(const QString &orgRootPath, QObject *parent = nullptr);
    ~OrgBackend() override = default;

    // SyncBackend interface
    void loadCalendars(const QString &collectionId) override;
    void loadItems(KCalendarCore::MemoryCalendar *cal, bool suppressSignals = false) override;
    void storeCalendars(const QString &collectionId,
                        const QList<KCalendarCore::MemoryCalendar*> &calendars) override;
    void storeItems(KCalendarCore::MemoryCalendar *cal,
                    const QList<KCalendarCore::Incidence::Ptr> &items) override;
    void updateItem(KCalendarCore::MemoryCalendar *cal,
                    const KCalendarCore::Incidence::Ptr &item,
                    const QString &icalData) override;
    void startSync(const QString &collectionId,
                   KCalendarCore::MemoryCalendar *calendar,
                   const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                   const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                   const QMap<QString, QString> &stagedDeletions) override;
    void removeItem(const QString &calId, const QString &itemUid) override;

    static const QString BackendTypeName;
    QString backendType() const override;

    bool discoveredWritable(const QString &calendarId) const override;

    // Backend capabilities
    BackendCapabilities capabilities() const override;

    bool supportsCalendarCreation() const override;
    bool createCalendar(const QString &collectionId, const QString &calendarId, const QString &name,
                        CalendarType type = CalendarType::Hybrid) override;
    bool updateCalendar(const QString &collectionId, const QString &calendarId, const QVariantMap &properties) override;
    bool deleteCalendar(const QString &collectionId, const QString &calendarId) override;

    // Operation-Based API
    FetchOperation* fetchItems(const QString &calendarId) override;
    PushOperation* pushItems(const QString &calendarId,
                             const QList<KCalendarCore::Incidence::Ptr> &items) override;
    DeleteOperation* deleteItems(const QString &calendarId,
                                 const QStringList &uids) override;

    // Recurrence capabilities
    RecurrenceCapabilities recurrenceCapabilities() const override;

    // Source File Access
    QString sourceFilePath(const QString &calendarId) const override;

    // Calendar Property Getters
    QColor calendarColor(const QString &calendarId) const override;
    QString calendarDescription(const QString &calendarId) const override;

private:
    OrgFileManager *m_fileManager;

    // Internal data maps — org-specific data stashed on load, restored on save.
    // These replace custom properties on incidences (zero X-* pollution).
    QHash<QString, OrgRoundtripData> m_roundtripData;
    QHash<QString, OrgPlanningData> m_planningData;

    // Reparenting helper — SyncBackend-level orchestration
    void reparentHeadline(OrgMode::OrgFile::Pointer orgFile,
                          OrgMode::Headline::Pointer headline,
                          const QString &newParentUid);

    // Collect sibling orders from m_planningData for saveOrgFile()
    QMap<QString, int> collectSiblingOrders() const;
};

} // namespace Kalburator::Sync

#endif // ORGBACKEND_H
