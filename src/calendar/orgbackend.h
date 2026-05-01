#ifndef ORGBACKEND_H
#define ORGBACKEND_H

#include "syncbackend.h"
#include "orgfilemanager.h"
#include "backendrecord.h"
#include "collectioninfo.h"

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
    void storeCalendars(const QString &collectionId,
                        const QList<KCalendarCore::MemoryCalendar*> &calendars) override;
    void startSync(const QString &collectionId,
                   KCalendarCore::MemoryCalendar *calendar,
                   const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                   const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                   const QMap<QString, QString> &stagedDeletions,
                   const TranscodingPlan& plan = TranscodingPlan{}) override;
    void removeItem(const QString &calId, const QString &itemUid) override;

    static const QString BackendTypeName;
    QString backendType() const override;
    QList<Kalburator::Shape::Shape> nativeShapes() const override;

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

    // F2 Task 9: 3-arg form holds the logic; 2-arg form delegates.
    // The 2-arg override is kept until F2 Task 38 removes the base
    // 2-arg virtual entirely. No default argument on the 3-arg form
    // here because the 2-arg overload is still present — a default
    // would make 2-arg calls ambiguous. The base virtual still
    // declares the default for callers that bind to SyncBackend*.
    PushOperation* pushItems(const QString &calendarId,
                             const QList<KCalendarCore::Incidence::Ptr> &items,
                             const TranscodingPlan &plan) override;
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

    // =========================================================================
    // IBlobBackend overrides (Phase D Task 14)
    //
    // Gated: this class is only compiled when KALBURATOR_HAVE_ORG_IO=ON.
    // recordId     = uid from the org :ID: property (incidence->uid())
    // collectionId = calendarId (each .org file is a calendar)
    // data         = serialized iCal bytes of the incidence
    // contentHash  = SHA-256 of the iCal bytes
    // lastModified = .org file mtime (whole-file granularity; Phase E improves)
    // =========================================================================

    // Identity
    QString backendId()   const override;
    QString displayName() const override;
    bool    isAvailable() const override;

    // Collections
    QList<CollectionInfo> availableCollections() override;
    CollectionInfo        collectionInfo(const QString &collectionId) override;
    QString               createCollection(const CollectionInfo &info) override;

    // Records
    QList<BackendRecord>         loadRecords(const QString &collectionId) override;
    std::optional<BackendRecord> loadRecord(const QString &recordId) override;
    QString                      createRecord(const QString &collectionId,
                                              const BackendRecord &record) override;
    bool                         updateRecord(const BackendRecord &record) override;
    bool                         deleteRecord(const QString &recordId) override;

    // Change detection — whole-.org-file mtime short-circuit
    QList<BackendRecord> modifiedSince(const QString &collectionId,
                                       const QDateTime &since) override;
    QStringList          deletedSince(const QString &collectionId,
                                      const QDateTime &since) override;
    bool                 supportsDeleteTracking() const override { return false; }

    // Batch — file I/O is synchronous; no batching needed in Phase D
    void beginBatch()    override {}
    bool commitBatch()   override { return true; }
    void rollbackBatch() override {}
    bool supportsBatch() const override { return false; }

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
