#ifndef LOCALBACKEND_H
#define LOCALBACKEND_H

#include <QObject>
#include <QString>
#include <QColor>
#include <KCalendarCore/MemoryCalendar>
#include "syncbackend.h"
#include "syncoperation.h"
#include <memory>

namespace Kalburator::Sync {

struct BackendCapabilities;
class AsyncFileWriter;
class FingerprintStore;

class LocalBackend : public SyncBackend
{
    Q_OBJECT

public:
    explicit LocalBackend(const QString &calendarRootPath, QObject *parent = nullptr);
    ~LocalBackend() override;

    void setcalendarRootPath(const QString &path);

    // SyncBackend interface
    void loadCalendars(const QString &collectionId) override;
    void loadItems(KCalendarCore::MemoryCalendar* cal, bool suppressSignals = false) override;
    void storeCalendars(const QString &collectionId,
                        const QList<KCalendarCore::MemoryCalendar*> &calendars) override;
    void storeItems(KCalendarCore::MemoryCalendar* cal,
                    const QList<KCalendarCore::Incidence::Ptr> &items) override;
    void updateItem(KCalendarCore::MemoryCalendar* cal,
                    const KCalendarCore::Incidence::Ptr &item,
                    const QString &icalData) override;
    void startSync(const QString &collectionId,
                   KCalendarCore::MemoryCalendar* calendar,
                   const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                   const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                   const QMap<QString, QString> &stagedDeletions) override;
    void removeItem(const QString &calId, const QString &itemUid) override;

    static const QString BackendTypeName;
    QString backendType() const override;

    /**
     * @brief Set the DB file path so the private FingerprintStore can be
     * initialised. Must be called before using fingerprint-based optimizations.
     * Mirrors RemoteBackend::setDbPath().
     */
    void setDbPath(const QString &dbPath);

    /**
     * @brief Get the persisted fingerprint for a calendar.
     * @return Stored fingerprint, or empty string if not present or store not set.
     */
    QString cachedFingerprint(const QString &calendarId) const;

    /**
     * @brief Persist a fingerprint for a calendar.
     */
    void setCachedFingerprint(const QString &calendarId, const QString &fingerprint);

    /**
     * @brief Check if a calendar directory is writable.
     *
     * Checks both filesystem permissions and for a "readonly" marker file:
     * - Returns false if directory is not writable by filesystem permissions
     * - Returns false if a "readonly" file (case-insensitive) exists in the directory
     *
     * @param calendarId The calendar ID (directory name)
     * @return true if writable, false if read-only
     */
    bool discoveredWritable(const QString &calendarId) const override;

    // Backend capabilities
    BackendCapabilities capabilities() const override;

    // Binding metadata support
    QStringList bindingMetadataKeys() const override;
    void populateBindingMetadata(const DiscoveredCalendar &discovered,
                                 CalendarBackendBinding &binding) const override;
    void prepareCreationMetadata(const QString &calendarId,
                                 CalendarBackendBinding &binding) const override;

    // Operation-based API for SyncTransaction support
    FetchOperation* fetchItems(const QString &calendarId) override;
    PushOperation* pushItems(const QString &calendarId,
                             const QList<KCalendarCore::Incidence::Ptr> &items) override;
    DeleteOperation* deleteItems(const QString &calendarId,
                                  const QStringList &uids) override;

    bool supportsCalendarCreation() const override;
    bool createCalendar(const QString &collectionId, const QString &calendarId, const QString &name,
                        CalendarType type = CalendarType::Hybrid) override;
    bool updateCalendar(const QString &collectionId, const QString &calendarId, const QVariantMap &properties) override;
    bool deleteCalendar(const QString &collectionId, const QString &calendarId) override;

    // VDirSyncer-compatible calendar metadata (stored as files in calendar folder)
    QColor calendarColor(const QString &calendarId) const override;
    bool setCalendarColor(const QString &calendarId, const QColor &color);

    QString calendarDisplayName(const QString &calendarId) const;
    bool setCalendarDisplayName(const QString &calendarId, const QString &name);

    QString calendarDescription(const QString &calendarId) const override;
    bool setCalendarDescription(const QString &calendarId, const QString &description);

    int calendarOrder(const QString &calendarId) const;
    bool setCalendarOrder(const QString &calendarId, int order);

    // Debug/Raw ICS access
    QString getRawIcs(const QString &calendarId, const QString &uid) const override;
    bool setRawIcs(const QString &calendarId, const QString &uid,
                   const QString &icsContent) override;

    /**
     * @brief Phase-2 perf: cheap fingerprint of a calendar's on-disk state.
     *
     * Returns sha256 hex digest of the sorted list of (filename | mtime | size)
     * tuples for *.ics files in the calendar directory. Detects adds, removes,
     * and modifications. Returns empty string if the calendar directory does
     * not exist.
     *
     * Cost: O(N) stat calls; ~100 ms for ~600 files.
     */
    QString calendarFingerprint(const QString &calendarId) const;

private slots:
    void onAsyncWriteCompleted(const QString &filePath, const QString &identifier,
                                bool success, const QString &errorMessage);
    void onAsyncWritesFinished(int successCount, int failCount);
    void onAsyncWriteProgress(int completed, int total);

private:
    QString m_calendarRootPath;

    // Private per-backend fingerprint store (persisted to .kalburator-sync.db)
    std::unique_ptr<FingerprintStore> m_fingerprints;

    // Async file writer for non-blocking writes
    AsyncFileWriter *m_asyncWriter = nullptr;
    QString m_pendingSyncCollectionId;
    int m_pendingWriteCount = 0;

    // Helpers to load/save hierarchy
    void buildHierarchy(KCalendarCore::MemoryCalendar* cal);
    void writeIncidenceWithHierarchy(KCalendarCore::MemoryCalendar* cal, const KCalendarCore::Incidence::Ptr &incidence);

    // Utility to find parent UID from RELATED-TO with RELTYPE=CHILD
    QString findParentUid(const KCalendarCore::Incidence::Ptr &incidence) const;

    QString filePathForCalendar(const QString &calendarId) const;

    // Helper for async write setup
    void ensureAsyncWriterReady();
};

} // namespace Kalburator::Sync

#endif // LOCALBACKEND_H
