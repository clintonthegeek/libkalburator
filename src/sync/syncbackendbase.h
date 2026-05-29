#ifndef KALBURATOR_SYNC_SYNCBACKENDBASE_H
#define KALBURATOR_SYNC_SYNCBACKENDBASE_H

// Phase K.4: slim, domain-neutral sync-backend base. Lifted out of
// `src/calendar/syncbackend.h` so that non-calendar backends
// (RawFilesBackend, GenericSqliteBackend, RemoteContactsBackend,
// blob-only adapters) can inherit a base that does NOT pull in
// KCalendarCore and does NOT carry calendar-typed pure virtuals.
//
// `Kalburator::Sync::SyncBackend` (in `src/calendar/syncbackend.h`)
// continues to exist as a calendar-typed subclass for the calendar
// backends; this is the "deprecated forwarding header" path described
// in the K.4 design doc, generalized so it does not break PlanStan
// and the existing test suite which reference `SyncBackend::xxx` for
// calendar-typed signals.

#include <QObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QHash>

#include "iblobbackend.h"   // pure interface (no QObject)
#include "shape.h"          // Kalburator::Shape::Shape
#include "syncoperation.h"  // neutral SyncOperation base (same dir)

namespace Kalburator::Sync {

/**
 * @brief Domain-neutral abstract sync-backend base.
 *
 * Concrete backends inherit either this class directly (blob-only,
 * raw-file, sqlite, contacts, etc.) or `SyncBackend` (calendar-typed
 * variant, in `src/calendar/syncbackend.h`).
 *
 * This base intentionally has no calendar-specific API, no
 * KCalendarCore include, and no calendar-typed signals. It declares:
 *   - identity surface (backendType, nativeShapes, resourceId, shapeFor)
 *   - operation-tracking machinery (cancellation, pending-operation queries)
 *     — the operation handle types (`FetchOperation` / `PushOperation` /
 *     `DeleteOperation`) live on the calendar-typed `SyncBackend`
 *     subclass; they include KCalendarCore types and would re-pull
 *     calendar deps into this header.
 *   - domain-neutral telemetry signals (fetch/write started/finished,
 *     transcodingWarning)
 *   - default IBlobBackend implementations (sensible identity
 *     fallbacks; data-path methods log a warning if not overridden).
 */
class SyncBackendBase : public QObject, public IBlobBackend
{
    Q_OBJECT

public:
    explicit SyncBackendBase(QObject *parent = nullptr);
    virtual ~SyncBackendBase() = default;

    // ========== Core Backend Identity ==========

    /// Return a unique backend type string, e.g. "local", "raw-files",
    /// "carddav-contacts".
    virtual QString backendType() const = 0;

    /// Return the shapes this backend natively stores (Phase G.3).
    virtual QList<Kalburator::Shape::Shape> nativeShapes() const = 0;

    /// Stable identifier for the resource (device/store) this backend
    /// is attached to. Default: "backend:<hex-address>".
    virtual QString resourceId() const;

    /// Best shape for a specific collection. Default:
    /// `nativeShapes().first()` or `Shape::Any()` if empty.
    virtual Kalburator::Shape::Shape shapeFor(const QString &collectionId) const;

    /// Whether the given collection is writable. Default true; calendar/remote
    /// backends override with discovered writability.
    virtual bool discoveredWritable(const QString &collectionId) const {
        Q_UNUSED(collectionId);
        return true;
    }

    // ========== Operation-Based API ==========
    // BackendRecord-id-typed CRUD; the operation HANDLE types
    // (`FetchOperation`, `PushOperation`, `DeleteOperation`) are
    // forward-declared here — their headers include KCalendarCore but
    // their use-points (engine, calendar plugin) include them
    // explicitly. The base API exposes:
    //   - fetchItems(colId)               -> FetchOperation*
    //   - deleteItems(colId, uids)        -> DeleteOperation*
    // The Incidence::Ptr-typed `pushItems(...)` overload lives on the
    // calendar-typed `SyncBackend` subclass (would require pulling
    // KCalendarCore::Incidence into the base header).

    virtual SyncOperation* fetchItems(const QString &calendarId);
    virtual SyncOperation* deleteItems(const QString &calendarId,
                                       const QStringList &uids);

    // ========== Operation Tracking ==========

    virtual bool hasPendingOperations() const;
    virtual bool hasPendingOperationsFor(const QString &calendarId) const;
    virtual QList<SyncOperation*> pendingOperations() const;
    virtual QList<SyncOperation*> pendingOperationsFor(const QString &calendarId) const;
    virtual void cancelOperationsFor(const QString &calendarId);
    virtual void cancelAllOperations();

    // ========== IBlobBackend default implementations ==========
    QString backendId() const override;
    QString displayName() const override;
    bool    isAvailable() const override;
    bool    supportsBatch() const override;
    bool    supportsDeleteTracking() const override;

    QList<CollectionInfo> availableCollections() override;
    CollectionInfo collectionInfo(const QString &collectionId) override;
    QString createCollection(const CollectionInfo &info) override;

    QList<BackendRecord> loadRecords(const QString &collectionId) override;
    std::optional<BackendRecord> loadRecord(const QString &recordId) override;
    QString createRecord(const QString &collectionId, const BackendRecord &record) override;
    bool    updateRecord(const BackendRecord &record) override;
    bool    deleteRecord(const QString &recordId) override;

    QList<BackendRecord> modifiedSince(const QString &collectionId,
                                       const QDateTime &since) override;
    QStringList deletedSince(const QString &collectionId,
                             const QDateTime &since) override;

    void beginBatch() override;
    bool commitBatch() override;
    void rollbackBatch() override;

Q_SIGNALS:
    // ========== Domain-neutral telemetry signals ==========

    /// Sync operation finished for a collection.
    void syncCompleted(const QString &collectionId);

    /// Emitted when a write operation invokes a non-lossless transcoder.
    /// Carries collection id, record uid, and the warning descriptions
    /// from each transcoder that contributed to the loss.
    void transcodingWarning(const QString &calendarId,
                            const QString &uid,
                            const QStringList &warnings);

    /// Fetch lifecycle telemetry.
    void fetchStarted(const QString &calendarId, int totalItems);
    void fetchProgressChanged(const QString &calendarId, int current, int total);
    void fetchFinished(const QString &calendarId, bool success,
                       const QString &errorMessage = QString());

    /// Write lifecycle telemetry.
    void writeStarted(const QString &calendarId, int totalItems);
    void writeProgressChanged(const QString &calendarId, int current, int total);

protected:
    // ========== Operation Tracking Implementation ==========

    void registerOperation(SyncOperation *op);
    void unregisterOperation(SyncOperation *op);

    QHash<QString, QList<SyncOperation*>> m_pendingOperations;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_SYNCBACKENDBASE_H
