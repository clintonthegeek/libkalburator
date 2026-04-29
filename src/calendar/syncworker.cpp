#include "syncworker.h"
#include "calendarbaselinestore.h"
#include "blobbaselinestore.h"
#include "syncdiff.h"
#include "isynchost.h"
#include "icalendarcollection.h"
#include "syncbackend.h"
#include "iblobbackend.h"
#include "syncoperation.h"
#include "synctesthooks.h"
#include "transcodingregistry.h"
#include "synctransaction.h"
#include "createincidenceitem.h"
#include "updateincidenceitem.h"
#include "deleteincidenceitem.h"

#include <KCalendarCore/ICalFormat>
#include <QDebug>
#include <QSet>
#include <QThread>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QPointer>
#include <QMetaObject>
#include <QTimer>

namespace Kalburator::Sync {

// Build a compound sync key from a SyncRecord (uid + recurrenceId).
// Matches the syncRecordKey() format from syncdiff.cpp.
static QString syncRecordKey(const SyncRecord &rec)
{
    if (rec.recurrenceId.isValid())
        return rec.uid + QChar(0) + rec.recurrenceId.toString(Qt::ISODate);
    return rec.uid;
}

// Register metatypes for cross-thread signal/slot
static bool metatypesRegistered = []() {
    qRegisterMetaType<SyncWorker::Request>("SyncWorker::Request");
    qRegisterMetaType<SyncWorker::Mode>("SyncWorker::Mode");
    qRegisterMetaType<ConflictResolution>("ConflictResolution");
    qRegisterMetaType<ConflictInfo>("ConflictInfo");
    qRegisterMetaType<SyncResult>("SyncResult");
    qRegisterMetaType<KCalendarCore::Incidence::Ptr>("KCalendarCore::Incidence::Ptr");
    return true;
}();

SyncWorker::SyncWorker(QObject *parent)
    : QObject(parent)
{
}

SyncWorker::~SyncWorker()
{
    QMutexLocker locker(&m_mutex);
    m_cancelled = true;
}

void SyncWorker::setDependencies(ISyncHost *host,
                                  CalendarBaselineStore *calendarBaselines,
                                  ICalendarCollection *collection,
                                  BlobBaselineStore *blobBaselines)
{
    m_controller = host;
    m_calendarBaselines = calendarBaselines;
    m_blobBaselines = blobBaselines;
    m_collection = collection;
}

void SyncWorker::cancel()
{
    QMutexLocker locker(&m_mutex);
    m_cancelled = true;
}

void SyncWorker::processSync(const SyncWorker::Request &request)
{
    // Helper to convert SyncMode to string
    auto syncModeStr = [](SyncMode mode) -> const char* {
        switch (mode) {
            case SyncMode::Disabled: return "Disabled";
            case SyncMode::OneWayUpload: return "OneWayUpload";
            case SyncMode::OneWayDownload: return "OneWayDownload";
            case SyncMode::TwoWay: return "TwoWay";
            default: return "Unknown";
        }
    };

    qDebug().noquote() << QString("SyncWorker: === Starting sync [%1/%2] -> [%3/%4] ===")
        .arg(request.mapping.sourceBackend, request.mapping.sourceCalendar,
             request.mapping.targetBackend, request.mapping.targetCalendar);
    qDebug() << "  mapping:" << request.mapping.id
             << "mode:" << syncModeStr(request.mapping.mode)
             << (request.mode == Mode::Monitored ? "(monitored)" : "(unmonitored)");

    // Timing breakdown
    m_totalTimer.start();
    m_propertyFetchMs = 0;
    m_propertyDiffMs = 0;
    m_propertyApplyMs = 0;
    m_sourceFetchMs = 0;
    m_targetFetchMs = 0;
    m_diffMs = 0;

    // Reset state
    {
        QMutexLocker locker(&m_mutex);
        m_cancelled = false;
        m_fetchFailed = false;
        m_applyFailed = false;
    }
    m_fetchErrorMessage.clear();
    m_applyErrorMessage.clear();
    m_yieldedForConflict = false;
    m_conflictPhase = ConflictPhase::Done;
    m_conflictIndex = 0;

    m_currentRequest = request;
    m_sourceRecords.clear();
    m_targetRecords.clear();
    m_currentDiff = SyncDiff();
    m_currentResult = SyncResult{};
    m_currentResult.startTime = QDateTime::currentDateTime();
    m_resolvedToTarget.clear();
    m_resolvedToSource.clear();
    m_resolvedToSourceConflictStart = 0;

    // Reset property sync state
    m_sourceProperties = CalendarPropertyRecord();
    m_targetProperties = CalendarPropertyRecord();
    m_propertyDiff = CalendarPropertyDiff();

    emit syncStarted(request.mapping.id);

    // === Property Sync (before incidence sync) ===

    // Property Phase 1: Fetch calendar properties
    m_phaseTimer.start();
    fetchCalendarProperties();
    m_propertyFetchMs = m_phaseTimer.elapsed();

    // Property Phase 2: Compute property diff
    m_phaseTimer.restart();
    computePropertyDiff();
    m_propertyDiffMs = m_phaseTimer.elapsed();

    // Property Phase 3: Apply property changes
    m_phaseTimer.restart();
    applyPropertyChanges();
    m_propertyApplyMs = m_phaseTimer.elapsed();

    // === Incidence Sync ===

    // Phase 1: Fetch source
    m_phaseTimer.start();
    fetchSourceRecords();
    m_sourceFetchMs = m_phaseTimer.elapsed();

    // CRITICAL: Abort if fetch failed - continuing would cause data loss
    if (m_fetchFailed) {
        m_currentResult.success = false;
        m_currentResult.errorMessage = m_fetchErrorMessage.isEmpty()
            ? QStringLiteral("Source fetch failed") : m_fetchErrorMessage;
        m_currentResult.endTime = QDateTime::currentDateTime();
        qWarning() << "SyncWorker: Source fetch failed, aborting sync:" << m_currentResult.errorMessage;
        emit syncCompleted(request.mapping.id, m_currentResult);
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        if (m_cancelled) {
            m_currentResult.success = false;
            m_currentResult.errorMessage = QStringLiteral("Cancelled");
            m_currentResult.endTime = QDateTime::currentDateTime();
            emit syncCompleted(request.mapping.id, m_currentResult);
            return;
        }
    }

    // Phase 2: Fetch target
    m_phaseTimer.restart();
    fetchTargetRecords();
    m_targetFetchMs = m_phaseTimer.elapsed();

    // CRITICAL: Abort if fetch failed
    if (m_fetchFailed) {
        m_currentResult.success = false;
        m_currentResult.errorMessage = m_fetchErrorMessage.isEmpty()
            ? QStringLiteral("Target fetch failed") : m_fetchErrorMessage;
        m_currentResult.endTime = QDateTime::currentDateTime();
        qWarning() << "SyncWorker: Target fetch failed, aborting sync:" << m_currentResult.errorMessage;
        emit syncCompleted(request.mapping.id, m_currentResult);
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        if (m_cancelled) {
            m_currentResult.success = false;
            m_currentResult.errorMessage = QStringLiteral("Cancelled");
            m_currentResult.endTime = QDateTime::currentDateTime();
            emit syncCompleted(request.mapping.id, m_currentResult);
            return;
        }
    }

    // Phase 3: Compute diff and handle conflicts
    m_phaseTimer.restart();
    computeDiff();
    handleConflicts();

    // If handleConflicts() yielded for a monitored conflict, return early.
    // resumeAfterConflict() will be called when the user resolves it,
    // which re-enters handleConflicts() and eventually calls continueAfterConflicts().
    if (m_yieldedForConflict) {
        return;
    }

    // No conflicts yielded — continue straight through
    continueAfterConflicts();
}

void SyncWorker::resumeAfterConflict(ConflictResolution resolution, const QString &mergedIcal)
{
    qDebug() << "SyncWorker::resumeAfterConflict - resolution:" << static_cast<int>(resolution);

    if (!m_yieldedForConflict) {
        qWarning() << "SyncWorker::resumeAfterConflict called but not yielded — ignoring";
        return;
    }

    // Determine which list and index the current conflict came from
    const QList<SyncChange> &currentList =
        (m_conflictPhase == ConflictPhase::ToTarget) ? m_currentDiff.toTarget : m_currentDiff.toSource;

    if (m_conflictIndex < currentList.size()) {
        const SyncChange &change = currentList[m_conflictIndex];
        applyMonitoredResolution(change, resolution, mergedIcal);
    }

    // Advance past the conflict we just resolved
    m_conflictIndex++;
    m_yieldedForConflict = false;

    // Resume handling remaining conflicts (may yield again)
    handleConflicts();

    // If we yielded again, return and wait for next resolution
    if (m_yieldedForConflict) {
        return;
    }

    // All conflicts resolved — continue to apply + baselines
    continueAfterConflicts();
}

// ============================================================================
// Sync Phases
// ============================================================================

// ============================================================================
// Phase D Task 19 — blob-view fetch helper
// ============================================================================

// Convenience cast: every SyncBackend is-a IBlobBackend (Group 2 hoisted it).
namespace {
inline IBlobBackend *asBlob(SyncBackend *b) { return static_cast<IBlobBackend *>(b); }
} // namespace

void SyncWorker::fetchRecordsViaBlob(const QString &backendId,
                                      const QString &calendarId,
                                      QList<SyncRecord> &out)
{
    SyncBackend *backend = m_controller->backendById(backendId);
    if (!backend) {
        m_fetchFailed = true;
        m_fetchErrorMessage = QStringLiteral("Backend not found: %1").arg(backendId);
        return;
    }

    // Determine the lastSyncTime cutoff so modifiedSince only returns changed
    // records.  A null/invalid QDateTime means "return all records" (first sync
    // baseline-building step), but Task 21 intercepts the first-sync path
    // before we get here, so in practice since is always set.
    QDateTime since;
    if (m_calendarBaselines) {
        const QString mappingId = m_currentRequest.mapping.id;
        QMetaObject::invokeMethod(m_calendarBaselines,
            [this, mappingId, &since]() {
                since = m_calendarBaselines->lastSyncTime(mappingId);
            }, Qt::BlockingQueuedConnection);
    }

    // Fetch via blob view on the main thread (backends are main-thread objects).
    QList<BackendRecord> records;
    IBlobBackend *blob = asBlob(backend);
    QMetaObject::invokeMethod(backend, [blob, calendarId, since, &records]() {
        records = blob->modifiedSince(calendarId, since);
    }, Qt::BlockingQueuedConnection);

    // Per-record hash skip (Phase D Task 20):
    // Records whose contentHash matches the BlobBaselineStore baseline are
    // unchanged since the last sync — skip the calendar-level merge for them.
    // The BlobBaselineStore is not a QObject, so we piggy-back on
    // m_calendarBaselines (which IS a main-thread QObject) as the trampoline.
    QSet<QString> skipIds;
    if (m_blobBaselines && m_calendarBaselines) {
        QString bId = backendId;
        QString cId = calendarId;
        BlobBaselineStore *blobStore = m_blobBaselines;
        QList<BackendRecord> *rPtr = &records;
        QMetaObject::invokeMethod(m_calendarBaselines,
            [blobStore, bId, cId, rPtr, &skipIds]() {
                for (const BackendRecord &r : *rPtr) {
                    const QString stored = blobStore->baselineHash(bId, cId, r.id);
                    if (!stored.isEmpty() && stored == r.contentHash) {
                        skipIds.insert(r.id);
                    }
                }
            }, Qt::BlockingQueuedConnection);
    }

    // Translate BackendRecord → SyncRecord
    KCalendarCore::ICalFormat icalFormat;
    for (const BackendRecord &r : records) {
        if (skipIds.contains(r.id)) {
            continue;  // Unchanged since last sync; skip calendar-level merge
        }
        const QString ical = QString::fromUtf8(r.data);
        KCalendarCore::Incidence::Ptr inc = icalFormat.fromString(ical);
        if (inc) {
            out.append(SyncRecord::fromIncidence(inc, calendarId, backendId));
        }
    }
}

void SyncWorker::fetchSourceRecords()
{
    emit phaseChanged(m_currentRequest.mapping.id, 1);  // FetchingSource

    if (!m_controller) {
        m_fetchFailed = true;
        m_fetchErrorMessage = QStringLiteral("No controller");
        return;
    }

    // Subsequent-sync path (Phase D Task 19): use IBlobBackend::modifiedSince.
    // Quick path (first sync, no baselines) continues via calendar-typed fetchItems.
    if (!m_currentRequest.useQuickPath) {
        fetchRecordsViaBlob(m_currentRequest.mapping.sourceBackend,
                            m_currentRequest.mapping.sourceCalendar,
                            m_sourceRecords);
        return;
    }

    SyncBackend *backend = m_controller->backendById(m_currentRequest.mapping.sourceBackend);
    if (!backend) {
        m_fetchFailed = true;
        m_fetchErrorMessage = QStringLiteral("Source backend not found: %1").arg(m_currentRequest.mapping.sourceBackend);
        return;
    }

    // Call fetchItems on main thread (backends are main-thread objects)
    // Use BlockingQueuedConnection to wait for completion
    FetchOperation *fetchOpRaw = nullptr;
    QMetaObject::invokeMethod(backend, [backend, this, &fetchOpRaw]() {
        fetchOpRaw = backend->fetchItems(m_currentRequest.mapping.sourceCalendar);
    }, Qt::BlockingQueuedConnection);

    QPointer<FetchOperation> fetchOp = fetchOpRaw;
    if (!fetchOp) {
        m_fetchFailed = true;
        m_fetchErrorMessage = QStringLiteral("Source fetchItems returned null");
        return;
    }

    // Wait for fetch to complete
    // The operation runs on main thread, we wait for its finished signal
    QEventLoop loop;

    // Connect to finished signal (cross-thread safe with QueuedConnection)
    connect(fetchOp.data(), &SyncOperation::finished, &loop, &QEventLoop::quit, Qt::QueuedConnection);

    // Forward progress signals from backend (also on main thread)
    connect(backend, &SyncBackend::fetchProgressChanged, this,
            [this](const QString &calendarId, int current, int total) {
                emit fetchProgress(calendarId, current, total);
            }, Qt::QueuedConnection);

    // Only skip waiting if operation has definitively completed
    // Do NOT skip if state is Pending (not yet started) - must wait for Running -> Succeeded
    if (fetchOp && (fetchOp->state() == SyncOperation::Succeeded ||
                    fetchOp->state() == SyncOperation::Failed ||
                    fetchOp->state() == SyncOperation::Cancelled)) {
        // Already done
    } else {
        loop.exec();
    }

    // Disconnect progress
    backend->disconnect(this);

    if (!fetchOp || fetchOp->state() == SyncOperation::Failed) {
        m_fetchFailed = true;
        m_fetchErrorMessage = fetchOp ? fetchOp->errorString() : QStringLiteral("Source fetch operation deleted");
        return;
    }

    // Convert to sync records
    QList<KCalendarCore::Incidence::Ptr> incidences = fetchOp->fetchedItems();
    for (const auto &inc : incidences) {
        if (inc) {
            m_sourceRecords.append(SyncRecord::fromIncidence(inc,
                                                              m_currentRequest.mapping.sourceCalendar,
                                                              m_currentRequest.mapping.sourceBackend));
        }
    }

    // Timing summary will show record count
}

void SyncWorker::fetchTargetRecords()
{
    emit phaseChanged(m_currentRequest.mapping.id, 2);  // FetchingTarget

    // Subsequent-sync path (Phase D Task 19): use IBlobBackend::modifiedSince.
    // Quick path (first sync, no baselines) continues via calendar-typed fetchItems.
    if (!m_currentRequest.useQuickPath) {
        fetchRecordsViaBlob(m_currentRequest.mapping.targetBackend,
                            m_currentRequest.mapping.targetCalendar,
                            m_targetRecords);
        return;
    }

    SyncBackend *backend = m_controller->backendById(m_currentRequest.mapping.targetBackend);
    if (!backend) {
        m_fetchFailed = true;
        m_fetchErrorMessage = QStringLiteral("Target backend not found: %1").arg(m_currentRequest.mapping.targetBackend);
        return;
    }

    // Removed: start log is redundant with processSync header

    // Call fetchItems on main thread (backends are main-thread objects)
    FetchOperation *fetchOpRaw = nullptr;
    QMetaObject::invokeMethod(backend, [backend, this, &fetchOpRaw]() {
        fetchOpRaw = backend->fetchItems(m_currentRequest.mapping.targetCalendar);
    }, Qt::BlockingQueuedConnection);

    QPointer<FetchOperation> fetchOp = fetchOpRaw;
    if (!fetchOp) {
        m_fetchFailed = true;
        m_fetchErrorMessage = QStringLiteral("Target fetchItems returned null");
        return;
    }

    // Wait for fetch to complete
    QEventLoop loop;
    connect(fetchOp.data(), &SyncOperation::finished, &loop, &QEventLoop::quit, Qt::QueuedConnection);

    connect(backend, &SyncBackend::fetchProgressChanged, this,
            [this](const QString &calendarId, int current, int total) {
                emit fetchProgress(calendarId, current, total);
            }, Qt::QueuedConnection);

    // Only skip waiting if operation has definitively completed
    // Do NOT skip if state is Pending (not yet started) - must wait for Running -> Succeeded
    if (fetchOp && (fetchOp->state() == SyncOperation::Succeeded ||
                    fetchOp->state() == SyncOperation::Failed ||
                    fetchOp->state() == SyncOperation::Cancelled)) {
        // Already done
    } else {
        loop.exec();
    }

    backend->disconnect(this);

    if (!fetchOp || fetchOp->state() == SyncOperation::Failed) {
        m_fetchFailed = true;
        m_fetchErrorMessage = fetchOp ? fetchOp->errorString() : QStringLiteral("Target fetch operation deleted");
        return;
    }

    // Convert to sync records
    QList<KCalendarCore::Incidence::Ptr> incidences = fetchOp->fetchedItems();
    for (const auto &inc : incidences) {
        if (inc) {
            m_targetRecords.append(SyncRecord::fromIncidence(inc,
                                                              m_currentRequest.mapping.targetCalendar,
                                                              m_currentRequest.mapping.targetBackend));
        }
    }

    // Timing summary will show record count
}

void SyncWorker::computeDiff()
{
    emit phaseChanged(m_currentRequest.mapping.id, 3);  // Processing

    if (m_currentRequest.useQuickPath) {
        // Use fast 2-way diff without baselines
        m_currentDiff = computeQuickDiff(m_sourceRecords, m_targetRecords,
                                          m_currentRequest.mapping.mode);
    } else {
        // Load baselines from CalendarBaselineStore (call on main thread - SQLite is thread-affine)
        QMap<QString, QString> baselines;
        if (m_calendarBaselines) {
            QString mappingId = m_currentRequest.mapping.id;
            QMetaObject::invokeMethod(m_calendarBaselines, [this, mappingId, &baselines]() {
                const QHash<QString, QString> hash = m_calendarBaselines->allBaselines(mappingId);
                for (auto it = hash.constBegin(); it != hash.constEnd(); ++it)
                    baselines.insert(it.key(), it.value());
            }, Qt::BlockingQueuedConnection);
        }

        // Compute 3-way diff (CPU intensive, fine to run in worker)
        m_currentDiff = computeSyncDiff(m_sourceRecords, m_targetRecords, baselines,
                                         m_currentRequest.mapping.mode);
    }

    // Only log if there are changes or conflicts
    if (!m_currentDiff.toTarget.isEmpty() || !m_currentDiff.toSource.isEmpty() ||
        !m_currentDiff.conflicts.isEmpty()) {
        qDebug().noquote() << QString("  Diff: toTarget=%1 toSource=%2 conflicts=%3")
            .arg(m_currentDiff.toTarget.size())
            .arg(m_currentDiff.toSource.size())
            .arg(m_currentDiff.conflicts.size());
    }
}

void SyncWorker::handleConflicts()
{
    // On first entry (not a resume), record the resolvedToSource watermark.
    // Everything added after this point comes from conflict resolution
    // (not regular diff pass-through). applyChanges() uses this to
    // distinguish conflict-resolved entries from regular entries.
    if (m_conflictPhase == ConflictPhase::Done && !m_yieldedForConflict) {
        m_resolvedToSourceConflictStart = m_resolvedToSource.size();
        m_conflictPhase = ConflictPhase::ToTarget;
        m_conflictIndex = 0;
    }

    // Effective conflict policy for this sync pass.
    // On first sync (quick path / no baselines), computeQuickDiff reports
    // BothCreated conflicts for every item that exists on both sides with
    // different hashes. This is expected after mirror creation or when
    // connecting to a backend that already has data — the items are
    // semantically the same but differ due to cross-backend serialization.
    // Override AskUser → SourceWins to avoid flooding the user with false
    // conflict dialogs. After this sync, updateBaselines() seeds baselines
    // so subsequent syncs use the 3-way path with the user's configured policy.
    ConflictResolution effectivePolicy = m_currentRequest.mapping.conflictPolicy;
    if (m_currentRequest.useQuickPath && effectivePolicy == ConflictResolution::AskUser) {
        effectivePolicy = ConflictResolution::SourceWins;
        if (m_conflictPhase == ConflictPhase::ToTarget && m_conflictIndex == 0) {
            qDebug() << "SyncWorker: First sync (no baselines) — auto-resolving"
                     << "BothCreated conflicts as SourceWins";
        }
    }

    // Process toTarget changes, handling conflicts based on mode
    if (m_conflictPhase == ConflictPhase::ToTarget) {
        for (int i = m_conflictIndex; i < m_currentDiff.toTarget.size(); ++i) {
            {
                QMutexLocker locker(&m_mutex);
                if (m_cancelled) return;
            }

            const SyncChange &change = m_currentDiff.toTarget[i];

            if (change.isConflict) {
                SYNC_HOOK_CALL(onConflictDetected, change.conflictInfo);
#ifdef PLANSTAN_TESTING
                SyncTestHooks::instance().conflictCount++;
#endif

                if (effectivePolicy == ConflictResolution::AskUser) {
                    if (m_currentRequest.mode == Mode::Monitored) {
                        // YIELD: save position and return
                        m_conflictPhase = ConflictPhase::ToTarget;
                        m_conflictIndex = i;
                        m_yieldedForConflict = true;
                        qDebug() << "SyncWorker::handleConflicts - yielding for monitored conflict:" << change.uid;
                        emit conflictPauseRequested(change.conflictInfo);
                        return;
                    } else {
                        handleConflictUnmonitored(change);
                    }
                } else {
                    // Auto-resolve based on policy
                    resolveConflictAutomatically(change, effectivePolicy);
                }
            } else {
                m_resolvedToTarget.append(change);
            }
        }

        // Finished toTarget — move to toSource
        m_conflictPhase = ConflictPhase::ToSource;
        m_conflictIndex = 0;
    }

    // Process toSource changes, handling conflicts (e.g. OneWayDownload)
    if (m_conflictPhase == ConflictPhase::ToSource) {
        for (int i = m_conflictIndex; i < m_currentDiff.toSource.size(); ++i) {
            {
                QMutexLocker locker(&m_mutex);
                if (m_cancelled) return;
            }

            const SyncChange &change = m_currentDiff.toSource[i];

            if (change.isConflict) {
                SYNC_HOOK_CALL(onConflictDetected, change.conflictInfo);
#ifdef PLANSTAN_TESTING
                SyncTestHooks::instance().conflictCount++;
#endif

                if (effectivePolicy == ConflictResolution::AskUser) {
                    if (m_currentRequest.mode == Mode::Monitored) {
                        // YIELD: save position and return
                        m_conflictPhase = ConflictPhase::ToSource;
                        m_conflictIndex = i;
                        m_yieldedForConflict = true;
                        qDebug() << "SyncWorker::handleConflicts - yielding for monitored conflict:" << change.uid;
                        emit conflictPauseRequested(change.conflictInfo);
                        return;
                    } else {
                        handleConflictUnmonitored(change);
                    }
                } else {
                    // Auto-resolve based on policy
                    resolveConflictAutomatically(change, effectivePolicy);
                }
            } else {
                m_resolvedToSource.append(change);
            }
        }

        // Finished toSource — all conflicts done
        m_conflictPhase = ConflictPhase::Done;
    }
}

void SyncWorker::continueAfterConflicts()
{
    m_diffMs = m_phaseTimer.elapsed();

    {
        QMutexLocker locker(&m_mutex);
        if (m_cancelled) {
            m_currentResult.success = false;
            m_currentResult.errorMessage = QStringLiteral("Cancelled");
            m_currentResult.endTime = QDateTime::currentDateTime();
            emit syncCompleted(m_currentRequest.mapping.id, m_currentResult);
            return;
        }
    }

    // Phase 4: Apply changes
    QElapsedTimer applyTimer;
    applyTimer.start();
    applyChanges();
    qint64 applyMs = applyTimer.elapsed();

    // Phase 5: Update baselines
    QElapsedTimer baselinesTimer;
    baselinesTimer.start();
    updateBaselines();
    updatePropertyBaselines();  // Also update property baselines
    qint64 baselinesMs = baselinesTimer.elapsed();

    // Complete
    m_currentResult.success = !m_applyFailed &&
                              !m_currentResult.hasUnresolvedConflicts() &&
                              !m_currentResult.sourceStats.hasErrors() &&
                              !m_currentResult.targetStats.hasErrors();
    if (m_applyFailed && m_currentResult.errorMessage.isEmpty()) {
        m_currentResult.errorMessage = m_applyErrorMessage;
    }
    m_currentResult.endTime = QDateTime::currentDateTime();

    qint64 totalMs = m_totalTimer.elapsed();
    qDebug().noquote() << QString("SyncWorker: === Completed [%1] %2 in %3ms (%4 items) ===")
        .arg(m_currentRequest.mapping.sourceCalendar,
             m_currentResult.success ? "OK" : "FAILED",
             QString::number(totalMs),
             QString::number(m_sourceRecords.size()));
    qDebug().noquote() << QString("  Timing: props=%1+%2+%3ms fetch=%4+%5ms diff=%6ms apply=%7ms baselines=%8ms")
        .arg(m_propertyFetchMs).arg(m_propertyDiffMs).arg(m_propertyApplyMs)
        .arg(m_sourceFetchMs).arg(m_targetFetchMs).arg(m_diffMs).arg(applyMs).arg(baselinesMs);

    emit syncCompleted(m_currentRequest.mapping.id, m_currentResult);
}

void SyncWorker::handleConflictUnmonitored(const SyncChange &change)
{
    // Emit conflict and continue - main thread will queue it
    qDebug() << "SyncWorker::handleConflictUnmonitored - queuing conflict:" << change.uid;
    emit conflictDetected(change.conflictInfo);
    m_currentResult.unresolvedConflicts.append(change.conflictInfo);
}

void SyncWorker::applyMonitoredResolution(const SyncChange &change,
                                           ConflictResolution resolution,
                                           const QString &mergedIcal)
{
    qDebug() << "SyncWorker::applyMonitoredResolution - resolved with:" << static_cast<int>(resolution);

    if (resolution == ConflictResolution::Skip || resolution == ConflictResolution::AskUser) {
        // User skipped or deferred
        m_currentResult.unresolvedConflicts.append(change.conflictInfo);
        return;
    }

    if (resolution == ConflictResolution::CustomMerge) {
        // Apply merged incidence
        if (!mergedIcal.isEmpty()) {
            KCalendarCore::ICalFormat icalFormat;
            auto tempCal = QSharedPointer<KCalendarCore::MemoryCalendar>(
                new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone())
            );
            if (icalFormat.fromString(tempCal, mergedIcal)) {
                auto incidences = tempCal->incidences();
                if (!incidences.isEmpty()) {
                    KCalendarCore::Incidence::Ptr mergedIncidence = incidences.first();

                    SyncChange resolved = change;
                    resolved.isConflict = false;
                    resolved.type = SyncChangeType::Modified;
                    resolved.sourceRecord.incidence = mergedIncidence;
                    resolved.sourceRecord.icalData = mergedIcal;
                    resolved.targetRecord.incidence = mergedIncidence;
                    resolved.targetRecord.icalData = mergedIcal;

                    // Apply to both backends
                    m_resolvedToTarget.append(resolved);
                    m_resolvedToSource.append(resolved);
                    return;
                }
            }
        }
        // Failed to parse merge - record as unresolved
        m_currentResult.unresolvedConflicts.append(change.conflictInfo);
        return;
    }

    // Handle other resolutions
    resolveConflictAutomatically(change, resolution);
}

void SyncWorker::resolveConflictAutomatically(const SyncChange &change,
                                               ConflictResolution policy)
{
    SyncChange resolved = change;
    resolved.isConflict = false;

    switch (policy) {
        case ConflictResolution::SourceWins: {
            // For ModifyDelete conflicts, fix up the change type so the
            // correct action is applied to the target side.
            if (change.conflictInfo.type == ConflictType::ModifyDelete) {
                if (!change.sourceRecord.incidence) {
                    // Source deleted, target modified -> source wins means delete target
                    resolved.type = SyncChangeType::Deleted;
                } else if (!change.targetRecord.incidence) {
                    // Target deleted, source modified -> source wins means recreate on target
                    resolved.type = SyncChangeType::Created;
                    resolved.targetRecord.incidence = change.sourceRecord.incidence;
                    resolved.targetRecord.icalData = change.sourceRecord.icalData;
                }
            }
            m_resolvedToTarget.append(resolved);
            break;
        }

        case ConflictResolution::TargetWins: {
            // For ModifyDelete conflicts, we need to fix up the change type
            // so the correct action is applied to the source side.
            if (change.conflictInfo.type == ConflictType::ModifyDelete) {
                if (!change.targetRecord.incidence) {
                    // Target deleted, source modified -> target wins means delete source
                    resolved.type = SyncChangeType::Deleted;
                } else if (!change.sourceRecord.incidence) {
                    // Source deleted, target modified -> target wins means recreate on source
                    resolved.type = SyncChangeType::Created;
                    resolved.sourceRecord.incidence = change.targetRecord.incidence;
                    resolved.sourceRecord.icalData = change.targetRecord.icalData;
                }
            }
            m_resolvedToSource.append(resolved);
            break;
        }

        case ConflictResolution::LastWriteWins: {
            bool sourceNewer = change.sourceRecord.lastModified > change.targetRecord.lastModified;
            if (sourceNewer) {
                // Source wins — apply source's version to target
                // Same ModifyDelete fixup as SourceWins
                if (change.conflictInfo.type == ConflictType::ModifyDelete) {
                    if (!change.sourceRecord.incidence) {
                        // Source deleted, target modified -> source wins means delete target
                        resolved.type = SyncChangeType::Deleted;
                    } else if (!change.targetRecord.incidence) {
                        // Target deleted, source modified -> source wins means recreate on target
                        resolved.type = SyncChangeType::Created;
                        resolved.targetRecord.incidence = change.sourceRecord.incidence;
                        resolved.targetRecord.icalData = change.sourceRecord.icalData;
                    }
                }
                m_resolvedToTarget.append(resolved);
            } else {
                // Target wins — apply target's version to source
                // Same ModifyDelete fixup as TargetWins
                if (change.conflictInfo.type == ConflictType::ModifyDelete) {
                    if (!change.targetRecord.incidence) {
                        // Target deleted, source modified -> target wins means delete source
                        resolved.type = SyncChangeType::Deleted;
                    } else if (!change.sourceRecord.incidence) {
                        // Source deleted, target modified -> target wins means recreate on source
                        resolved.type = SyncChangeType::Created;
                        resolved.sourceRecord.incidence = change.targetRecord.incidence;
                        resolved.sourceRecord.icalData = change.targetRecord.icalData;
                    }
                }
                m_resolvedToSource.append(resolved);
            }
            break;
        }

        case ConflictResolution::Duplicate: {
            // ModifyDelete: one side has null incidence (deleted).
            // Keep the modified version on both sides (same as SourceWins/TargetWins
            // for the modified side).
            if (!change.sourceRecord.incidence && !change.targetRecord.incidence) {
                // Both null — nothing to duplicate
                break;
            }

            if (!change.sourceRecord.incidence || !change.targetRecord.incidence) {
                // One side deleted (null incidence), one side modified.
                // Keep the modified version and recreate it on the deleted side.
                if (change.sourceRecord.incidence) {
                    // Source has the modified version — push to target
                    SyncChange recreate = resolved;
                    recreate.type = SyncChangeType::Created;
                    recreate.targetRecord.incidence = change.sourceRecord.incidence;
                    recreate.targetRecord.icalData = change.sourceRecord.icalData;
                    m_resolvedToTarget.append(recreate);
                } else {
                    // Target has the modified version — push to source
                    SyncChange recreate = resolved;
                    recreate.type = SyncChangeType::Created;
                    recreate.sourceRecord.incidence = change.targetRecord.incidence;
                    recreate.sourceRecord.icalData = change.targetRecord.icalData;
                    m_resolvedToSource.append(recreate);
                }
                break;
            }

            // BothModified: both incidences are non-null.
            // Source wins for original UID
            m_resolvedToTarget.append(resolved);

            // Clone target's version with new UID
            KCalendarCore::Incidence::Ptr targetClone =
                KCalendarCore::Incidence::Ptr(change.targetRecord.incidence->clone());
            QString newUid = KCalendarCore::CalFormat::createUniqueId();
            targetClone->setUid(newUid);
            targetClone->setSummary(targetClone->summary() + QStringLiteral(" (conflict copy)"));
            targetClone->setLastModified(QDateTime::currentDateTimeUtc());

            // Create changes to add clone to both sides
            SyncChange cloneToSource;
            cloneToSource.type = SyncChangeType::Created;
            cloneToSource.uid = newUid;
            cloneToSource.isConflict = false;
            cloneToSource.sourceRecord.uid = newUid;
            cloneToSource.sourceRecord.incidence = targetClone;
            cloneToSource.targetRecord.uid = newUid;
            cloneToSource.targetRecord.incidence = targetClone;
            m_resolvedToSource.append(cloneToSource);

            SyncChange cloneToTarget;
            cloneToTarget.type = SyncChangeType::Created;
            cloneToTarget.uid = newUid;
            cloneToTarget.isConflict = false;
            cloneToTarget.sourceRecord.uid = newUid;
            cloneToTarget.sourceRecord.incidence = targetClone;
            cloneToTarget.targetRecord.uid = newUid;
            cloneToTarget.targetRecord.incidence = targetClone;
            m_resolvedToTarget.append(cloneToTarget);
            break;
        }

        case ConflictResolution::Skip:
            m_currentResult.unresolvedConflicts.append(change.conflictInfo);
            break;

        case ConflictResolution::AskUser:
            // Should not reach here
            m_currentResult.unresolvedConflicts.append(change.conflictInfo);
            break;

        case ConflictResolution::CustomMerge:
            // Should not reach here without merged data
            m_resolvedToTarget.append(resolved);
            break;
    }
}

void SyncWorker::applyChanges()
{
    emit phaseChanged(m_currentRequest.mapping.id, 3);  // Still Processing

    // Emit itemReady for each change before applying to backend
    // This allows main thread to update models immediately
    for (const auto &change : m_resolvedToTarget) {
        KCalendarCore::Incidence::Ptr inc = change.sourceRecord.incidence;
        if (inc) {
            emit itemReady(m_currentRequest.mapping.targetCalendar, inc,
                          static_cast<int>(change.type));
        }
    }

    if (!m_resolvedToTarget.isEmpty()) {
        qDebug().noquote() << QString("  Applying %1 changes to target")
            .arg(m_resolvedToTarget.size());
        SYNC_HOOK_CALL(onBackendPush, m_currentRequest.mapping.targetBackend,
                       m_currentRequest.mapping.targetCalendar, m_resolvedToTarget.size());

        applyChangesToBackend(m_currentRequest.mapping.targetBackend,
                              m_currentRequest.mapping.targetCalendar,
                              m_resolvedToTarget);
        m_currentResult.targetStats = m_currentDiff.targetStats();
    }

    // Apply resolvedToSource in TwoWay mode always, and in OneWay modes for
    // conflict-resolved changes only (regular non-conflict changes should not
    // flow in the wrong direction for OneWay modes, but conflict resolutions
    // like TargetWins in OneWayUpload must be honored).
    QList<SyncChange> sourceChangesToApply;
    if (m_currentRequest.mapping.mode == SyncMode::TwoWay) {
        sourceChangesToApply = m_resolvedToSource;
    } else {
        // OneWay modes: only apply entries added during conflict resolution.
        // m_resolvedToSourceConflictStart marks where conflict-resolved entries begin.
        for (int i = m_resolvedToSourceConflictStart; i < m_resolvedToSource.size(); ++i) {
            sourceChangesToApply.append(m_resolvedToSource[i]);
        }
    }

    if (!sourceChangesToApply.isEmpty()) {
        // Emit itemReady for source changes
        for (const auto &change : sourceChangesToApply) {
            KCalendarCore::Incidence::Ptr inc = change.targetRecord.incidence;
            if (inc) {
                emit itemReady(m_currentRequest.mapping.sourceCalendar, inc,
                              static_cast<int>(change.type));
            }
        }

        qDebug().noquote() << QString("  Applying %1 changes to source")
            .arg(sourceChangesToApply.size());
        SYNC_HOOK_CALL(onBackendPush, m_currentRequest.mapping.sourceBackend,
                       m_currentRequest.mapping.sourceCalendar, sourceChangesToApply.size());

        applyChangesToBackend(m_currentRequest.mapping.sourceBackend,
                              m_currentRequest.mapping.sourceCalendar,
                              sourceChangesToApply, true);
        m_currentResult.sourceStats = m_currentDiff.sourceStats();
    }
}

void SyncWorker::applyChangesToBackend(const QString &backendId,
                                        const QString &calendarId,
                                        const QList<SyncChange> &changes,
                                        bool useTargetRecord)
{
    if (!m_controller || !m_collection) {
        qWarning() << "SyncWorker::applyChangesToBackend - no controller or collection";
        return;
    }

    SyncBackend *backend = m_controller->backendById(backendId);
    if (!backend) {
        qWarning() << "SyncWorker::applyChangesToBackend - backend not found:" << backendId;
        return;
    }

    KCalendarCore::MemoryCalendar *cal = m_collection->calendar(calendarId);
    if (!cal) {
        qWarning() << "SyncWorker::applyChangesToBackend - calendar not found:" << calendarId;
        return;
    }

    // Determine source and target backend types for transcoding
    QString targetType = backend->backendType();
    QString sourceType;

    // Source type is the opposite of where we're applying
    if (useTargetRecord) {
        // We're applying to source backend, so source of data is target
        SyncBackend *sourceBackend = m_controller->backendById(m_currentRequest.mapping.targetBackend);
        sourceType = sourceBackend ? sourceBackend->backendType() : QString();
    } else {
        // We're applying to target backend, so source of data is source
        SyncBackend *sourceBackend = m_controller->backendById(m_currentRequest.mapping.sourceBackend);
        sourceType = sourceBackend ? sourceBackend->backendType() : QString();
    }

    bool needsTranscoding = !sourceType.isEmpty() && !targetType.isEmpty() && sourceType != targetType;

    // Build transaction ID
    QString direction = useTargetRecord ? QStringLiteral("source") : QStringLiteral("target");
    QString txId = QStringLiteral("sync-%1-%2-%3")
        .arg(m_currentRequest.mapping.id, direction,
             QString::number(QDateTime::currentMSecsSinceEpoch()));

    // Create transaction items from sync changes
    SyncTransaction tx(txId);

    int itemCount = 0;

    for (const auto &change : changes) {
        switch (change.type) {
            case SyncChangeType::Created: {
                KCalendarCore::Incidence::Ptr inc = useTargetRecord
                    ? change.targetRecord.incidence
                    : change.sourceRecord.incidence;
                if (!inc) break;

                if (needsTranscoding) {
                    auto transcoded = KCalendarCore::Incidence::Ptr(inc->clone());
                    QStringList warnings = TranscodingRegistry::instance()
                        .transcodeIncidence(sourceType, targetType, transcoded);
                    if (!warnings.isEmpty()) {
                        emit transcodingWarning(calendarId, inc->uid(), warnings);
                    }
                    inc = transcoded;
                }

                auto *item = new CreateIncidenceItem(calendarId, inc, backend);
                tx.addItem(item);
                itemCount++;
                break;
            }

            case SyncChangeType::Modified: {
                KCalendarCore::Incidence::Ptr newInc = useTargetRecord
                    ? change.targetRecord.incidence
                    : change.sourceRecord.incidence;
                KCalendarCore::Incidence::Ptr oldInc = useTargetRecord
                    ? change.sourceRecord.incidence
                    : change.targetRecord.incidence;
                if (!newInc) break;

                if (needsTranscoding) {
                    auto transcoded = KCalendarCore::Incidence::Ptr(newInc->clone());
                    QStringList warnings = TranscodingRegistry::instance()
                        .transcodeIncidence(sourceType, targetType, transcoded);
                    if (!warnings.isEmpty()) {
                        emit transcodingWarning(calendarId, newInc->uid(), warnings);
                    }
                    newInc = transcoded;
                }

                auto *item = new UpdateIncidenceItem(calendarId, oldInc, newInc, backend);
                tx.addItem(item);
                itemCount++;
                break;
            }

            case SyncChangeType::Deleted: {
                KCalendarCore::Incidence::Ptr deletedInc = useTargetRecord
                    ? change.sourceRecord.incidence
                    : change.targetRecord.incidence;

                auto *item = new DeleteIncidenceItem(calendarId, change.uid, deletedInc, backend);
                tx.addItem(item);
                itemCount++;
                break;
            }

            case SyncChangeType::Unchanged:
                break;
        }
    }

    if (itemCount == 0) {
        return;
    }

    qDebug() << "SyncWorker::applyChangesToBackend -"
             << "items:" << itemCount
             << "direction:" << direction
             << (needsTranscoding ? QString("(transcoding %1->%2)").arg(sourceType, targetType) : QString());

    // Emit write progress
    for (int i = 0; i < itemCount; i++) {
        emit writeProgress(calendarId, i + 1, itemCount);
    }

    // Marshal commitAll() to the main thread — backends are main-thread objects.
    // Transaction items' commit()/rollback() call backend->pushItems()/deleteItems()
    // which use QTimer::singleShot and QEventLoop internally, requiring the main
    // thread's event loop. BlockingQueuedConnection blocks this worker thread
    // until commitAll() returns on the main thread.
    bool txResult = false;
    QMetaObject::invokeMethod(backend, [&tx, &txResult]() {
        txResult = tx.commitAll();
    }, Qt::BlockingQueuedConnection);

    if (!txResult) {
        m_applyFailed = true;

        // Collect error information from the transaction
        QStringList errors;
        for (auto *item : tx.items()) {
            if (!item->errorString().isEmpty()) {
                errors.append(item->errorString());
            }
        }
        m_applyErrorMessage = errors.isEmpty()
            ? QStringLiteral("SyncTransaction commitAll() failed")
            : errors.join(QStringLiteral("; "));

        qWarning() << "SyncWorker::applyChangesToBackend - transaction failed:"
                   << m_applyErrorMessage;
    }

}

void SyncWorker::updateBaselines()
{
    if (!m_calendarBaselines) {
        qDebug() << "SyncWorker::updateBaselines - no CalendarBaselineStore, skipping";
        return;
    }

    // CRITICAL: Do not update baselines if the apply phase failed.
    // Writing baselines for items that weren't actually stored on the target
    // would cause the next sync to interpret those missing items as
    // "deleted on target", propagating phantom deletions back to source.
    // This is the root cause of Bug 11 (data loss on retry).
    if (m_applyFailed) {
        qWarning() << "SyncWorker::updateBaselines - SKIPPING baseline update because "
                      "apply phase failed:" << m_applyErrorMessage;
        return;
    }

    const QString mappingId = m_currentRequest.mapping.id;

    // Batch all baseline operations to minimize main-thread invocations
    // Using single transaction batches is dramatically faster than per-item calls
    QHash<QString, QString> baselinesToSet;
    QStringList baselinesToRemove;

    // Collect baselines for non-conflict changes to target
    for (const auto &change : m_currentDiff.toTarget) {
        if (change.isConflict) continue;

        QString key = change.sourceRecord.isValid()
            ? syncRecordKey(change.sourceRecord)
            : (change.targetRecord.isValid() ? syncRecordKey(change.targetRecord) : change.uid);

        if (change.type == SyncChangeType::Deleted) {
            baselinesToRemove.append(key);
        } else if (change.sourceRecord.isValid()) {
            baselinesToSet[key] = change.sourceRecord.icalData;
        }
    }

    // Collect baselines for non-conflict changes to source
    for (const auto &change : m_currentDiff.toSource) {
        if (change.isConflict) continue;

        QString key = change.targetRecord.isValid()
            ? syncRecordKey(change.targetRecord)
            : (change.sourceRecord.isValid() ? syncRecordKey(change.sourceRecord) : change.uid);

        if (change.type == SyncChangeType::Deleted) {
            if (!baselinesToRemove.contains(key)) {
                baselinesToRemove.append(key);
            }
        } else if (change.targetRecord.isValid()) {
            baselinesToSet[key] = change.targetRecord.icalData;
        }
    }

    // Collect baselines for resolved conflicts
    for (const auto &change : m_resolvedToTarget) {
        if (change.type == SyncChangeType::Created && change.baselineRecord.uid.isEmpty()) {
            continue;  // Skip newly created items from Duplicate resolution
        }
        if (change.sourceRecord.isValid()) {
            baselinesToSet[syncRecordKey(change.sourceRecord)] = change.sourceRecord.icalData;
        }
    }

    for (const auto &change : m_resolvedToSource) {
        if (change.type == SyncChangeType::Created && change.baselineRecord.uid.isEmpty()) {
            continue;
        }
        if (change.targetRecord.isValid()) {
            baselinesToSet[syncRecordKey(change.targetRecord)] = change.targetRecord.icalData;
        }
    }

    // Get existing baselines to determine which unchanged items are missing them
    QHash<QString, QString> existingBaselines;
    QMetaObject::invokeMethod(m_calendarBaselines, [this, mappingId, &existingBaselines]() {
        existingBaselines = m_calendarBaselines->allBaselines(mappingId);
    }, Qt::BlockingQueuedConnection);

    // Add missing baselines for unchanged items (first sync scenario)
    // unchangedUids contains plain UIDs; check all source records with matching UID
    for (const auto &rec : m_sourceRecords) {
        if (!rec.isValid()) continue;
        if (!m_currentDiff.unchangedUids.contains(rec.uid)) continue;
        QString key = syncRecordKey(rec);
        if (!existingBaselines.contains(key)) {
            baselinesToSet[key] = rec.icalData;
        }
    }

    // Execute batch operations with single cross-thread calls
    if (!baselinesToRemove.isEmpty()) {
        QMetaObject::invokeMethod(m_calendarBaselines, [this, mappingId, baselinesToRemove]() {
            for (const QString &uid : baselinesToRemove)
                m_calendarBaselines->removeBaseline(mappingId, uid);
        }, Qt::BlockingQueuedConnection);
    }

    if (!baselinesToSet.isEmpty()) {
        QMetaObject::invokeMethod(m_calendarBaselines, [this, mappingId, baselinesToSet]() {
            m_calendarBaselines->setBaselines(mappingId, baselinesToSet);
        }, Qt::BlockingQueuedConnection);
    }

    // Only log if we did something interesting
    if (!baselinesToSet.isEmpty() || !baselinesToRemove.isEmpty()) {
        qDebug().noquote() << QString("  Baselines: +%1 -%2")
            .arg(baselinesToSet.size())
            .arg(baselinesToRemove.size());
    }

    // Update last sync time
    QDateTime now = QDateTime::currentDateTime();
    QMetaObject::invokeMethod(m_calendarBaselines, [this, mappingId, now]() {
        m_calendarBaselines->setLastSyncTime(mappingId, now);
    }, Qt::BlockingQueuedConnection);
}
// Property sync method implementations for SyncWorker
// Add these at the end of syncworker.cpp before the closing brace

// ============================================================================
// Property Sync Implementation
// ============================================================================

void SyncWorker::fetchCalendarProperties()
{
    qDebug() << "SyncWorker::fetchCalendarProperties: Fetching calendar properties";

    if (!m_controller || !m_collection) {
        qWarning() << "SyncWorker::fetchCalendarProperties: Missing dependencies";
        return;
    }

    // Get backends
    SyncBackend *sourceBackend = m_controller->backendById(m_currentRequest.mapping.sourceBackend);
    SyncBackend *targetBackend = m_controller->backendById(m_currentRequest.mapping.targetBackend);

    if (!sourceBackend || !targetBackend) {
        qWarning() << "SyncWorker::fetchCalendarProperties: Backends not available";
        return;
    }

    // Fetch source properties
    m_sourceProperties.backendId = m_currentRequest.mapping.sourceBackend;
    m_sourceProperties.calendarId = m_currentRequest.mapping.sourceCalendar;
    m_sourceProperties.color = sourceBackend->calendarColor(m_currentRequest.mapping.sourceCalendar);
    m_sourceProperties.description = sourceBackend->calendarDescription(m_currentRequest.mapping.sourceCalendar);
    m_sourceProperties.versionHash = CalendarPropertyRecord::computeHash(m_sourceProperties);

    // Fetch target properties
    m_targetProperties.backendId = m_currentRequest.mapping.targetBackend;
    m_targetProperties.calendarId = m_currentRequest.mapping.targetCalendar;
    m_targetProperties.color = targetBackend->calendarColor(m_currentRequest.mapping.targetCalendar);
    m_targetProperties.description = targetBackend->calendarDescription(m_currentRequest.mapping.targetCalendar);
    m_targetProperties.versionHash = CalendarPropertyRecord::computeHash(m_targetProperties);

    qDebug().noquote() << QString("  Source props: color=%1 desc=%2")
        .arg(m_sourceProperties.color.isValid() ? m_sourceProperties.color.name() : "(none)")
        .arg(m_sourceProperties.description.isEmpty() ? "(none)" : m_sourceProperties.description.left(30));
    qDebug().noquote() << QString("  Target props: color=%1 desc=%2")
        .arg(m_targetProperties.color.isValid() ? m_targetProperties.color.name() : "(none)")
        .arg(m_targetProperties.description.isEmpty() ? "(none)" : m_targetProperties.description.left(30));
}

void SyncWorker::computePropertyDiff()
{
    qDebug() << "SyncWorker::computePropertyDiff: Computing property changes";

    if (!m_calendarBaselines) {
        qWarning() << "SyncWorker::computePropertyDiff: No CalendarBaselineStore available";
        return;
    }

    // Load baseline from CalendarBaselineStore
    QString baselineJson;
    QString mappingId = m_currentRequest.mapping.id;
    QString calendarId = m_currentRequest.mapping.sourceCalendar;

    QMetaObject::invokeMethod(m_calendarBaselines, [this, mappingId, calendarId, &baselineJson]() {
        baselineJson = m_calendarBaselines->propertyBaseline(mappingId, calendarId);
    }, Qt::BlockingQueuedConnection);

    qDebug().noquote() << QString("  Baseline check: isEmpty=%1 length=%2")
        .arg(baselineJson.isEmpty() ? "true" : "false")
        .arg(baselineJson.length());

    // FIRST SYNC: No baseline exists yet
    if (baselineJson.isEmpty()) {
        qDebug() << "  First property sync - storing baseline, no changes to apply";
        m_propertyDiff = CalendarPropertyDiff(); // No changes
        return;
    }

    // SUBSEQUENT SYNCS: Compare to baseline
    CalendarPropertyRecord baseline = CalendarPropertyRecord::fromJson(
        baselineJson,
        m_sourceProperties.backendId,
        m_sourceProperties.calendarId
    );

    // Compare color
    bool sourceColorChanged = (m_sourceProperties.color != baseline.color);
    bool targetColorChanged = (m_targetProperties.color != baseline.color);

    if (sourceColorChanged && !targetColorChanged) {
        // Source changed, target unchanged -> propagate to target
        m_propertyDiff.colorChanged = true;
        m_propertyDiff.newColor = m_sourceProperties.color;
        qDebug().noquote() << QString("  Color change detected: %1 -> %2 (propagate to target)")
            .arg(baseline.color.name())
            .arg(m_sourceProperties.color.name());
    } else if (targetColorChanged && !sourceColorChanged &&
               m_currentRequest.mapping.mode == SyncMode::TwoWay) {
        // Target changed, source unchanged -> propagate to source (if two-way)
        m_propertyDiff.colorChanged = true;
        m_propertyDiff.newColor = m_targetProperties.color;
        qDebug().noquote() << QString("  Color change detected: %1 -> %2 (propagate to source)")
            .arg(baseline.color.name())
            .arg(m_targetProperties.color.name());
    } else if (sourceColorChanged && targetColorChanged) {
        // CONFLICT: both changed
        qWarning() << "  Property conflict: both sides changed color";
        if (m_currentRequest.mapping.conflictPolicy == ConflictResolution::SourceWins) {
            m_propertyDiff.colorChanged = true;
            m_propertyDiff.newColor = m_sourceProperties.color;
            qDebug() << "    Resolved: source wins";
        } else {
            m_propertyDiff.colorChanged = true;
            m_propertyDiff.newColor = m_targetProperties.color;
            qDebug() << "    Resolved: target wins";
        }
    }

    // Compare description (same logic as color)
    bool sourceDescChanged = (m_sourceProperties.description != baseline.description);
    bool targetDescChanged = (m_targetProperties.description != baseline.description);

    if (sourceDescChanged && !targetDescChanged) {
        m_propertyDiff.descriptionChanged = true;
        m_propertyDiff.newDescription = m_sourceProperties.description;
        qDebug() << "  Description change detected (propagate to target)";
    } else if (targetDescChanged && !sourceDescChanged &&
               m_currentRequest.mapping.mode == SyncMode::TwoWay) {
        m_propertyDiff.descriptionChanged = true;
        m_propertyDiff.newDescription = m_targetProperties.description;
        qDebug() << "  Description change detected (propagate to source)";
    } else if (sourceDescChanged && targetDescChanged) {
        qWarning() << "  Property conflict: both sides changed description";
        if (m_currentRequest.mapping.conflictPolicy == ConflictResolution::SourceWins) {
            m_propertyDiff.descriptionChanged = true;
            m_propertyDiff.newDescription = m_sourceProperties.description;
        } else {
            m_propertyDiff.descriptionChanged = true;
            m_propertyDiff.newDescription = m_targetProperties.description;
        }
    }

    if (!m_propertyDiff.hasChanges()) {
        qDebug() << "  No property changes detected";
    }
}

void SyncWorker::applyPropertyChanges()
{
    if (!m_propertyDiff.hasChanges()) {
        return;
    }

    qDebug() << "SyncWorker::applyPropertyChanges: Applying property changes";

    if (!m_controller) {
        qWarning() << "SyncWorker::applyPropertyChanges: Missing controller";
        return;
    }

    SyncBackend *sourceBackend = m_controller->backendById(m_currentRequest.mapping.sourceBackend);
    SyncBackend *targetBackend = m_controller->backendById(m_currentRequest.mapping.targetBackend);

    if (!sourceBackend || !targetBackend) {
        qWarning() << "SyncWorker::applyPropertyChanges: Backends not available";
        return;
    }

    QVariantMap properties;
    if (m_propertyDiff.colorChanged) {
        properties[QStringLiteral("color")] = m_propertyDiff.newColor;
    }
    if (m_propertyDiff.descriptionChanged) {
        properties[QStringLiteral("description")] = m_propertyDiff.newDescription;
    }

    // Determine which backend to update based on sync mode and change direction
    bool updateSource = false;
    bool updateTarget = false;

    if (m_currentRequest.mapping.mode == SyncMode::OneWayUpload) {
        // Source -> Target
        updateTarget = true;
    } else if (m_currentRequest.mapping.mode == SyncMode::OneWayDownload) {
        // Target -> Source
        updateSource = true;
    } else if (m_currentRequest.mapping.mode == SyncMode::TwoWay) {
        // Determine direction based on which side changed
        // (the computePropertyDiff already set newColor/newDescription from the changed side)
        // For simplicity, we'll update both to the resolved value
        updateSource = true;
        updateTarget = true;
    }

    bool success = true;

    if (updateTarget) {
        qDebug() << "  Updating target calendar properties";
        if (!targetBackend->updateCalendar(
                m_currentRequest.collectionId,
                m_currentRequest.mapping.targetCalendar,
                properties)) {
            qWarning() << "  Failed to update target calendar properties";
            success = false;
        } else {
            // Update our cached target properties
            if (m_propertyDiff.colorChanged) {
                m_targetProperties.color = m_propertyDiff.newColor;
            }
            if (m_propertyDiff.descriptionChanged) {
                m_targetProperties.description = m_propertyDiff.newDescription;
            }
            m_targetProperties.versionHash = CalendarPropertyRecord::computeHash(m_targetProperties);
        }
    }

    if (updateSource) {
        qDebug() << "  Updating source calendar properties";
        if (!sourceBackend->updateCalendar(
                m_currentRequest.collectionId,
                m_currentRequest.mapping.sourceCalendar,
                properties)) {
            qWarning() << "  Failed to update source calendar properties";
            success = false;
        } else {
            // Update our cached source properties
            if (m_propertyDiff.colorChanged) {
                m_sourceProperties.color = m_propertyDiff.newColor;
            }
            if (m_propertyDiff.descriptionChanged) {
                m_sourceProperties.description = m_propertyDiff.newDescription;
            }
            m_sourceProperties.versionHash = CalendarPropertyRecord::computeHash(m_sourceProperties);
        }
    }

    if (success) {
        qDebug() << "  Property changes applied successfully";
    }
}

void SyncWorker::updatePropertyBaselines()
{
    qDebug() << "SyncWorker::updatePropertyBaselines: Updating property baselines";

    if (!m_calendarBaselines) {
        qWarning() << "SyncWorker::updatePropertyBaselines: No CalendarBaselineStore available";
        return;
    }

    // Store the current source properties as the new baseline
    // (After sync, both source and target should have the same properties)
    QString propertiesJson = m_sourceProperties.toJson();
    QString mappingId = m_currentRequest.mapping.id;
    QString calendarId = m_currentRequest.mapping.sourceCalendar;

    QMetaObject::invokeMethod(m_calendarBaselines, [this, mappingId, calendarId, propertiesJson]() {
        m_calendarBaselines->setPropertyBaseline(mappingId, calendarId, propertiesJson);
    }, Qt::BlockingQueuedConnection);

    qDebug() << "  Property baseline updated";
}


} // namespace Kalburator::Sync
