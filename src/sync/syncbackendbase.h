#ifndef KALBURATOR_SYNC_SYNCBACKENDBASE_H
#define KALBURATOR_SYNC_SYNCBACKENDBASE_H

// LAYER ROLE (Plan 9) — `sync/` is the orchestration layer's home for the
// NEUTRAL backend contracts: the abstract bases/mixins that concrete domain
// backends implement and the engine dispatches against, naming no concrete
// domain backend (invariant 1). The neutral contracts are `SyncBackendBase`
// (here) and the `ChangeDetection` capability mixin (sync/changedetection.h);
// the blob-level contract `IBlobBackend` lives one layer down in `blob/`.
// Concrete backends (calendar/, contacts/, universal/) inherit these
// downward; `sync/` itself never inherits or #includes a concrete backend.
//
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
#include <QPointer>
#include <functional>

#include "iblobbackend.h"   // pure interface (no QObject)
#include "shape.h"          // Kalburator::Shape::Shape
#include "syncoperation.h"  // neutral SyncOperation base (same dir)
#include "writeoperation.h" // E5.3: applyRecords() return type (same dir)
#include "writerbatch.h"    // E5.3: applyRecords() batch parameter type (same dir)

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
    /// is attached to. Default: m_resourceId if set via setResourceId()
    /// (ProviderManager stamps the registry id here at registration time),
    /// else "backend:<hex-address>".
    virtual QString resourceId() const;

    /// Stamp the registry id ("<providerId>:<domainId>") this backend was
    /// registered under. Called by ProviderManager::registerProviderBackends
    /// immediately after construction; not meant for general use.
    void setResourceId(const QString &id) { m_resourceId = id; }

    /// Best shape for a specific collection. Default:
    /// `nativeShapes().first()` or `Shape::Any()` if empty.
    virtual Kalburator::Shape::Shape shapeFor(const QString &collectionId) const;

    /// Whether the given collection is writable. Default true; calendar/remote
    /// backends override with discovered writability.
    virtual bool discoveredWritable(const QString &collectionId) const {
        Q_UNUSED(collectionId);
        return true;
    }

    /**
     * @brief Max operations this backend can usefully have in flight
     * across ALL its collections. 0 = unlimited (only the engine's global
     * concurrency cap applies).
     *
     * Parallel-sync Task 5. The engine caps concurrent mappings per
     * resourceId() using this, so a backend can veto a host's concurrency
     * setting downward without host cooperation. Override with a small
     * number for a rate-limited network service, or 1 for a backend
     * speaking to a single physical device over one link.
     *
     * Note this is about USEFUL concurrency, not safety: the
     * per-collection FIFO in enqueueOperation() already guarantees
     * operations on one collection never interleave.
     */
    virtual int maxConcurrentOperations() const { return 0; }

    // ========== Operation-Based API ==========
    // BackendRecord-id-typed CRUD. The base API returns the neutral
    // `SyncOperation` handle (sync/syncoperation.h, no KCalendarCore):
    //   - fetchItems(colId)               -> SyncOperation*
    //   - deleteItems(colId, uids)        -> SyncOperation*
    // Calendar backends override these covariantly to return the
    // calendar-typed `FetchOperation`/`DeleteOperation` subclasses; the
    // Incidence::Ptr-typed `pushItems(...)` overload lives only on the
    // calendar-typed `SyncBackend` subclass. (Plan 3: this base no longer
    // names any KCalendarCore-bearing type.)

    virtual SyncOperation* fetchItems(const QString &calendarId);
    virtual SyncOperation* deleteItems(const QString &calendarId,
                                       const QStringList &uids);

    /// E5.3 (audit B7 / CP-A): the engine's write-path entry point — replaces
    /// the old thread-blocking dispatch through `RecordWriter::apply()`
    /// (recordwriter.h). Applies a classified `WriterBatch` (creates/updates/
    /// deletes) to `collectionId` and returns a `WriteOperation` tracking
    /// per-record success/failure.
    ///
    /// Default implementation (correct for backends with no async internals
    /// — LocalBackend, MockBackend): adapts the existing `createRecord`/
    /// `updateRecord`/`deleteRecord` virtuals SYNCHRONOUSLY, one call per
    /// record, in the same order `DefaultBlobWriter::apply()` always has
    /// (creates, then updates, then deletes) — so backend failure-injection
    /// test hooks (e.g. MockBackend::setFailurePoint) keep working unchanged.
    /// The returned op is already finished (`isFinished()` true) before this
    /// call returns; callers on backends with real async internals (e.g.
    /// RemoteCalendarBackend, which overrides this) must not assume that.
    virtual WriteOperation* applyRecords(const QString &collectionId,
                                         const WriterBatch &batch);

    /// Records equivalent to loadRecords(collectionId), but served from the
    /// most recent successfully completed fetchItems() for that collection
    /// when the backend can do so without new I/O (H5/O23: the dispatchSync
    /// fetch gate already ran fetchItems() moments earlier; this avoids a
    /// second, fully redundant read of the same collection). Single-shot:
    /// once served, the memo is cleared, so a later call with no fresh
    /// fetchItems() in between falls through to the default. Default:
    /// delegates to loadRecordsOrError (correct for backends without a
    /// fetch cache).
    virtual bool recordsFromLastFetch(const QString &collectionId,
                                      QList<BackendRecord> &records,
                                      QString &errorMessage);

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

    // ========== E5.1: per-collection FIFO operation queue ==========
    // Neutral primitive both layers' operation-producing entry points call
    // (SyncBackendBase's own fetchItems/deleteItems and the calendar-typed
    // SyncBackend subclass's pushItems/startSync): at most one operation per
    // collection is ever in flight; `startFunctor` runs only once this op
    // reaches the front of its collection's queue, always deferred to the
    // next event-loop turn (Qt event loop must run once for
    // `startFunctor` to fire — preserves the "caller connects signals to
    // `op` before it starts" guarantee every entry point already relied on
    // via its own QTimer::singleShot(0, ...)). `op` is registered for
    // pending-operation tracking/cancellation exactly as before; cancelling
    // a still-queued op (state flips to the terminal Cancelled) makes it
    // skip its body entirely when its turn comes.
    void enqueueOperation(const QString &collectionId, SyncOperation *op,
                         std::function<void()> startFunctor);

    QHash<QString, QList<SyncOperation*>> m_pendingOperations;

private:
    // Set via setResourceId(); resourceId() returns this when non-empty.
    QString m_resourceId;

    struct QueuedOp {
        QPointer<SyncOperation> op;
        std::function<void()> startFunctor;
    };

    void onOperationSettled(const QString &collectionId, SyncOperation *op);
    void maybeStartNext(const QString &collectionId);

    QHash<QString, QList<QueuedOp>> m_opQueue;
    QHash<QString, QPointer<SyncOperation>> m_opInFlight;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_SYNCBACKENDBASE_H
