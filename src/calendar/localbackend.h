#ifndef LOCALBACKEND_H
#define LOCALBACKEND_H

#include <optional>
#include <memory>

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QColor>
#include <KCalendarCore/MemoryCalendar>
#include "syncbackend.h"
#include "syncoperation.h"
#include "backendrecord.h"
#include "collectioninfo.h"
#include "../sync/changedetection.h"

namespace Kalburator::Sync {

struct BackendCapabilities;
class AsyncFileWriter;
class FingerprintStore;

class LocalBackend : public SyncBackend,
                     public Kalburator::Sync::ChangeDetection
{
    Q_OBJECT

public:
    explicit LocalBackend(const QString &calendarRootPath, QObject *parent = nullptr);
    ~LocalBackend() override;

    void setcalendarRootPath(const QString &path);

    // SyncBackend interface
    void loadCalendars(const QString &collectionId) override;
    void storeCalendars(const QString &collectionId,
                        const QList<KCalendarCore::MemoryCalendar*> &calendars) override;
    void startSync(const QString &collectionId,
                   KCalendarCore::MemoryCalendar* calendar,
                   const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                   const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                   const QMap<QString, QString> &stagedDeletions) override;
    void removeItem(const QString &calId, const QString &itemUid) override;

    static const QString BackendTypeName;
    QString backendType() const override;
    QList<Kalburator::Shape::Shape> nativeShapes() const override;

    /**
     * @brief Set the DB file path so the private FingerprintStore can be
     * initialised. Must be called before using fingerprint-based optimizations.
     * Mirrors RemoteCalendarBackend::setDbPath().
     */
    void setDbPath(const QString &dbPath);

    // ---- Sync::ChangeDetection ----
    // The engine's ONLY fingerprint entry points (consumed via
    // dynamic_cast<Sync::ChangeDetection*>). The backend's own fingerprint
    // accessors are private since Plan 7b T3 — one public face per concept.
    QString collectionRevision(const QString &collectionId) override;
    QString cachedCollectionRevision(const QString &collectionId) const override;
    void primeRevisionCache(const QMap<QString, QString> &cache) override;

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

    // Operation-based push/delete API (PushOperation / DeleteOperation)
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

    // VDirSyncer-compatible calendar metadata (stored as files in the
    // calendar folder). Public surface = the interface overrides +
    // setCalendarColor (PlanStan PROD: backenddiscoverycoordinator.cpp:199,
    // collectioncontroller.cpp:397 — caught by the Plan 7b downstream gate);
    // writes otherwise go through updateCalendar(QVariantMap). The remaining
    // per-property accessors are private since Plan 7b T3 (zero external
    // callers).
    QColor calendarColor(const QString &calendarId) const override;
    bool setCalendarColor(const QString &calendarId, const QColor &color);
    QString calendarDescription(const QString &calendarId) const override;

    // Debug/Raw ICS access
    QString getRawIcs(const QString &calendarId, const QString &uid) const override;
    bool setRawIcs(const QString &calendarId, const QString &uid,
                   const QString &icsContent) override;

    // =========================================================================
    // IBlobBackend overrides (Phase D Task 12)
    // Maps calendar-typed .ics file storage through BackendRecord:
    //   uid         → recordId / filename without ".ics"
    //   file bytes  → data
    //   SHA-256     → contentHash
    //   file mtime  → lastModified
    // =========================================================================

    // Identity
    QString backendId() const override;
    QString displayName() const override;
    bool    isAvailable() const override;

    // Collections
    QList<CollectionInfo> availableCollections() override;
    CollectionInfo        collectionInfo(const QString &collectionId) override;
    QString               createCollection(const CollectionInfo &info) override;

    // Records
    QList<BackendRecord>         loadRecords(const QString &collectionId) override;
    std::optional<BackendRecord> loadRecord(const QString &recordId) override;
    bool recordsFromLastFetch(const QString &collectionId,
                              QList<BackendRecord> &records,
                              QString &errorMessage) override;
    QString                      createRecord(const QString &collectionId,
                                              const BackendRecord &record) override;
    bool                         updateRecord(const BackendRecord &record) override;
    bool                         deleteRecord(const QString &recordId) override;

    // Change detection — consults m_fingerprints to short-circuit unchanged dirs
    QList<BackendRecord> modifiedSince(const QString &collectionId,
                                       const QDateTime &since) override;
    QStringList          deletedSince(const QString &collectionId,
                                      const QDateTime &since) override;
    bool                 supportsDeleteTracking() const override { return false; }

    // Batch — file I/O is synchronous; no real batching needed
    void beginBatch()    override {}
    bool commitBatch()   override { return true; }
    void rollbackBatch() override {}
    bool supportsBatch() const override { return false; }

private slots:
    void onAsyncWriteCompleted(const QString &filePath, const QString &identifier,
                                bool success, const QString &errorMessage);
    void onAsyncWritesFinished(int successCount, int failCount);
    void onAsyncWriteProgress(int completed, int total);

private:
    // ---- Fingerprint store (persisted change-detection tokens) ----
    // Private since Plan 7b T3: the engine reaches these only through the
    // Sync::ChangeDetection overrides above (mirrors Plan 7's ctag move).

    /// Cheap fingerprint of a calendar's on-disk state: sha256 over the
    /// sorted (filename | mtime | size) tuples of *.ics files. Empty when the
    /// directory does not exist. O(N) stat calls; ~100 ms for ~600 files.
    QString calendarFingerprint(const QString &calendarId) const;
    QString cachedFingerprint(const QString &calendarId) const;
    void setCachedFingerprint(const QString &calendarId, const QString &fingerprint);

    // ---- Per-property VDir metadata accessors (updateCalendar's backend) ----
    QString calendarDisplayName(const QString &calendarId) const;
    bool setCalendarDisplayName(const QString &calendarId, const QString &name);
    bool setCalendarDescription(const QString &calendarId, const QString &description);
    int calendarOrder(const QString &calendarId) const;
    bool setCalendarOrder(const QString &calendarId, int order);

    /// Validated metadata directory for a calendar, or nullopt when the
    /// calendarId or root path is empty (the guard every per-property
    /// accessor used to duplicate).
    std::optional<QString> metadataDirFor(const QString &calendarId) const;

    QString m_calendarRootPath;

    // H5/O23: single-shot memo of the last successful fetchItems() per
    // collection, so recordsFromLastFetch() can serve it without a second
    // directory scan. Cleared once served (see recordsFromLastFetch()).
    QHash<QString, QList<BackendRecord>> m_lastFetchRecords;

    // Private per-backend fingerprint store (persisted to .kalburator-sync.db)
    std::unique_ptr<FingerprintStore> m_fingerprints;

    // Async file writer for non-blocking writes
    AsyncFileWriter *m_asyncWriter = nullptr;
    QString m_pendingSyncCollectionId;

    QString filePathForCalendar(const QString &calendarId) const;

    /// <root>/<calendarId>/<uid>.ics (the path every item operation builds).
    QString icsPathFor(const QString &calendarId, const QString &uid) const;

    /// First calendar subdirectory owning @p recordId, as the full .ics path;
    /// nullopt when not found (the scan loadRecord/updateRecord/deleteRecord
    /// used to triplicate).
    std::optional<QString> recordPathFor(const QString &recordId) const;

    // Helper for async write setup
    void ensureAsyncWriterReady();
};

} // namespace Kalburator::Sync

#endif // LOCALBACKEND_H
