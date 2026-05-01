#include "calendardomainadapter.h"

#include "calendarbaselinestore.h"
#include "canonicalrecord.h"
#include "conflictpolicy.h"
#include "createincidenceitem.h"
#include "deleteincidenceitem.h"
#include "domainplugin.h"
#include "domainregistry.h"
#include "irecordmerger.h"
#include "icalendarcollection.h"
#include "shape.h"
#include "syncbackend.h"
#include "syncdiff.h"
#include "synctransaction.h"
#include "synctransactionitem.h"
#include "synctypes.h"
#include "transcodingrouter.h"
#include "updateincidenceitem.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include <QDateTime>
#include <QDebug>
#include <QHash>
#include <QMap>
#include <QMetaObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace Kalburator::Sync {

namespace {

QString recordKey(const QString &uid, const QDateTime &recurrenceId)
{
    if (recurrenceId.isValid()) {
        return uid + QChar(0) + recurrenceId.toString(Qt::ISODate);
    }
    return uid;
}

/// Parse a BackendRecord's iCal data into a SyncRecord. Returns an
/// invalid SyncRecord if parsing fails.
SyncRecord backendRecordToSyncRecord(const BackendRecord &b,
                                     const QString &calendarId,
                                     const QString &backendId)
{
    if (b.data.isEmpty()) {
        return SyncRecord{};
    }
    KCalendarCore::ICalFormat format;
    KCalendarCore::Incidence::Ptr inc =
        format.fromString(QString::fromUtf8(b.data));
    if (!inc) {
        return SyncRecord{};
    }
    return SyncRecord::fromIncidence(inc, calendarId, backendId);
}

QList<SyncRecord> backendRecordsToSyncRecords(
    const QList<BackendRecord> &records,
    const QString &calendarId,
    const QString &backendId)
{
    QList<SyncRecord> out;
    out.reserve(records.size());
    for (const auto &r : records) {
        if (r.isDeleted) continue;
        SyncRecord s = backendRecordToSyncRecord(r, calendarId, backendId);
        if (s.isValid()) out.append(s);
    }
    return out;
}

/// Wrap a SyncRecord (newly fetched/computed) into a BackendRecord with
/// id=uid, data=icalText, contentHash=versionHash. Used when projecting
/// SyncDiff entries back into EngineDiffOps.
BackendRecord syncRecordToBackendRecord(const SyncRecord &s)
{
    BackendRecord b;
    b.id = recordKey(s.uid, s.recurrenceId);
    b.type = QStringLiteral("calendar");
    b.displayName = s.uid;
    b.data = s.icalData.toUtf8();
    b.contentHash = s.versionHash;
    b.lastModified = s.lastModified;
    return b;
}

/// For Delete ops where we only have the baseline iCal text.
BackendRecord baselineToDoomedRecord(const QString &uid,
                                     const QString &baselineIcal)
{
    BackendRecord b;
    b.id = uid;
    b.type = QStringLiteral("calendar");
    b.displayName = uid;
    b.data = baselineIcal.toUtf8();
    b.lastModified = QDateTime::currentDateTimeUtc();
    return b;
}

EngineDiffOp makeOp(EngineDiffOp::Kind kind,
                    const BackendRecord &record,
                    const BackendRecord &baseline = BackendRecord{},
                    const BackendRecord &target = BackendRecord{})
{
    EngineDiffOp op;
    op.kind = kind;
    op.record = record;
    op.baselineRecord = baseline;
    op.targetRecord = target;
    return op;
}

/// Translate a calendar-typed SyncChange into an EngineDiffOp.
EngineDiffOp syncChangeToEngineOp(const SyncChange &c, bool toTarget)
{
    if (c.isConflict) {
        const BackendRecord src = syncRecordToBackendRecord(c.sourceRecord);
        const BackendRecord tgt = syncRecordToBackendRecord(c.targetRecord);
        const BackendRecord base = syncRecordToBackendRecord(c.baselineRecord);
        return makeOp(EngineDiffOp::Kind::Conflict, src, base, tgt);
    }

    switch (c.type) {
        case SyncChangeType::Created: {
            // The "created" record is on the side that doesn't have it yet;
            // the actual record content is on the originating side.
            const SyncRecord &src = toTarget ? c.sourceRecord : c.targetRecord;
            return makeOp(EngineDiffOp::Kind::Create,
                          syncRecordToBackendRecord(src));
        }
        case SyncChangeType::Modified: {
            const SyncRecord &src = toTarget ? c.sourceRecord : c.targetRecord;
            return makeOp(EngineDiffOp::Kind::Update,
                          syncRecordToBackendRecord(src),
                          syncRecordToBackendRecord(c.baselineRecord));
        }
        case SyncChangeType::Deleted: {
            BackendRecord doomed;
            doomed.id = c.uid;
            doomed.type = QStringLiteral("calendar");
            return makeOp(EngineDiffOp::Kind::Delete, doomed,
                          syncRecordToBackendRecord(c.baselineRecord));
        }
        case SyncChangeType::Unchanged:
            // Should not appear in toSource/toTarget lists.
            return makeOp(EngineDiffOp::Kind::Update,
                          syncRecordToBackendRecord(c.sourceRecord));
    }
    Q_UNREACHABLE();
}

bool resolvePolicy(ConflictResolution policy,
                   const BackendRecord &source,
                   const BackendRecord &target,
                   bool *sourceWins)
{
    switch (policy) {
        case ConflictResolution::SourceWins:
            *sourceWins = true;
            return true;
        case ConflictResolution::TargetWins:
            *sourceWins = false;
            return true;
        case ConflictResolution::LastWriteWins:
            *sourceWins = source.lastModified >= target.lastModified;
            return true;
        case ConflictResolution::Skip:
        case ConflictResolution::AskUser:
        case ConflictResolution::Duplicate:
        case ConflictResolution::CustomMerge:
            // F1 Task 5 absorbs the richer resolveConflictAutomatically
            // logic (ModifyDelete fixups + Duplicate clone-and-rename).
            // For Task 3 these defer.
            return false;
    }
    return false;
}

} // namespace

CalendarDomainAdapter::CalendarDomainAdapter(const TranscodingRouter &router)
    : m_router(router)
{
}

CalendarDomainAdapter::~CalendarDomainAdapter() = default;

void CalendarDomainAdapter::setBaselineStore(CalendarBaselineStore *store) noexcept
{
    m_baselineStore = store;
}

void CalendarDomainAdapter::setCollection(ICalendarCollection *collection) noexcept
{
    m_collection = collection;
}

void CalendarDomainAdapter::setSyncMode(SyncMode mode) noexcept
{
    m_syncMode = mode;
}

void CalendarDomainAdapter::setUseQuickPath(bool quick) noexcept
{
    m_useQuickPath = quick;
}

QList<BackendRecord> CalendarDomainAdapter::fetchRecords(
    SyncBackend *backend, const QString &collectionId)
{
    if (!backend) return {};
    return backend->loadRecords(collectionId);
}

EngineDiff CalendarDomainAdapter::diff(
    const QList<BackendRecord> &source,
    const QList<BackendRecord> &target,
    const QList<BackendRecord> &baseline,
    const BackendCapabilities & /*sourceCaps*/,
    const BackendCapabilities & /*targetCaps*/) const
{
    // BackendRecord lists carry iCal text in `data`. Convert to SyncRecord
    // (parses iCal once) and then drive the existing computeSyncDiff /
    // computeQuickDiff. The calendarId/backendId arguments are only used
    // by the diff engine for ConflictInfo bookkeeping; they don't affect
    // the change classification, so we pass empty strings.
    const QList<SyncRecord> sourceSync =
        backendRecordsToSyncRecords(source, QString(), QString());
    const QList<SyncRecord> targetSync =
        backendRecordsToSyncRecords(target, QString(), QString());

    SyncDiff syncDiff;
    if (m_useQuickPath || baseline.isEmpty()) {
        syncDiff = computeQuickDiff(sourceSync, targetSync, m_syncMode);
    } else {
        QMap<QString, QString> baselineMap;
        for (const auto &b : baseline) {
            if (b.isDeleted || b.id.isEmpty()) continue;
            baselineMap.insert(b.id, QString::fromUtf8(b.data));
        }
        syncDiff = computeSyncDiff(sourceSync, targetSync, baselineMap, m_syncMode);
    }

    // Translate SyncDiff back to EngineDiff.
    EngineDiff out;
    for (const auto &c : syncDiff.toTarget) {
        out.toTarget.append(syncChangeToEngineOp(c, /*toTarget=*/true));
    }
    for (const auto &c : syncDiff.toSource) {
        out.toSource.append(syncChangeToEngineOp(c, /*toTarget=*/false));
    }
    return out;
}

EngineMerge CalendarDomainAdapter::merge(const EngineDiff &d,
                                         ConflictResolution policy) const
{
    EngineMerge m;

    auto routeOp = [&m](const EngineDiffOp &op, bool toTarget) {
        QList<BackendRecord> &bucket = toTarget ? m.finalTarget : m.finalSource;
        switch (op.kind) {
            case EngineDiffOp::Kind::Create:
            case EngineDiffOp::Kind::Update:
                bucket.append(op.record);
                m.updatedBaselines.append(op.record);
                return;
            case EngineDiffOp::Kind::Delete: {
                BackendRecord doomed = op.record;
                doomed.isDeleted = true;
                bucket.append(doomed);
                return;
            }
            case EngineDiffOp::Kind::Conflict:
                Q_UNREACHABLE();
                return;
        }
    };

    for (const auto &op : d.toSource) {
        if (op.kind == EngineDiffOp::Kind::Conflict) continue;
        routeOp(op, /*toTarget=*/false);
    }

    for (const auto &op : d.toTarget) {
        if (op.kind != EngineDiffOp::Kind::Conflict) {
            routeOp(op, /*toTarget=*/true);
            continue;
        }
        bool sourceWins = false;
        if (resolvePolicy(policy, op.record, op.targetRecord, &sourceWins)) {
            EngineDiffOp resolved;
            resolved.kind = EngineDiffOp::Kind::Update;
            resolved.baselineRecord = op.baselineRecord;
            if (sourceWins) {
                resolved.record = op.record;
                routeOp(resolved, /*toTarget=*/true);
            } else {
                resolved.record = op.targetRecord;
                routeOp(resolved, /*toTarget=*/false);
            }
            ++m.conflictsResolved;
        } else if (policy == ConflictResolution::CustomMerge) {
            // G.2 Task 17: delegate 3-way merge to the registry's IRecordMergerICal.
            // Non-conflicting per-property changes (only source changed, or only
            // target changed) are merged automatically; properties where both sides
            // changed fall back to the baseline value.
            using Kalburator::Shape::DomainId;
            using Kalburator::Shape::EncodingId;
            using Kalburator::Sync::QSyncCore::ConflictPolicy;

            auto* plugin = Kalburator::Shape::DomainRegistry::instance()
                               .findByDomain(DomainId{"calendar"});
            if (plugin) {
                const Kalburator::Shape::Shape calIcal{DomainId{"calendar"}, EncodingId{"ical"}};
                Kalburator::Shape::CanonicalRecord srcRec{calIcal, op.record.data,         op.record.id};
                Kalburator::Shape::CanonicalRecord tgtRec{calIcal, op.targetRecord.data,   op.record.id};
                Kalburator::Shape::CanonicalRecord baseRec{calIcal, op.baselineRecord.data, op.record.id};

                auto merger = plugin->createCanonicalMerger();
                const auto merged = merger->merge(srcRec, tgtRec, baseRec,
                                                  ConflictPolicy::deferAll());

                BackendRecord mergedRecord = op.record;
                mergedRecord.data = merged.data;

                EngineDiffOp resolved;
                resolved.kind           = EngineDiffOp::Kind::Update;
                resolved.record         = mergedRecord;
                resolved.baselineRecord = op.baselineRecord;
                routeOp(resolved, /*toTarget=*/true);
                ++m.conflictsResolved;
            } else {
                ++m.conflictsDeferred;
            }
        } else {
            ++m.conflictsDeferred;
        }
    }

    return m;
}

EngineApplyResult CalendarDomainAdapter::applyChanges(
    const EngineMerge & /*merge*/, SyncBackend * /*destination*/,
    const QString & /*collectionId*/, const TranscodingPlan & /*plan*/)
{
    // Phase F1 Task 3 stub. Task 5 ("SyncEngine routes calendar path through
    // CalendarDomainAdapter") wires the calendar apply-changes-to-backend
    // body — SyncTransaction + CreateIncidenceItem / UpdateIncidenceItem /
    // DeleteIncidenceItem wrappers + Phase E writeFinished-capture pattern
    // + BlockingQueuedConnection commit marshalling — into this method.
    EngineApplyResult r;
    r.success = false;
    r.errorMessage = QStringLiteral(
        "CalendarDomainAdapter::applyChanges: not yet implemented "
        "(scheduled for F1 Task 5; engine drives the inner worker today)");
    return r;
}

QList<BackendRecord> CalendarDomainAdapter::loadBaselines(
    const QString &mappingId) const
{
    if (!m_baselineStore) return {};

    const QHash<QString, QString> baselines =
        m_baselineStore->allBaselines(mappingId);
    QList<BackendRecord> out;
    out.reserve(baselines.size());
    for (auto it = baselines.constBegin(); it != baselines.constEnd(); ++it) {
        BackendRecord r;
        r.id = it.key();
        r.type = QStringLiteral("calendar");
        r.data = it.value().toUtf8();
        r.contentHash = SyncRecord::computeHash(it.value());
        out.append(r);
    }
    return out;
}

bool CalendarDomainAdapter::saveBaselines(
    const QString &mappingId, const QList<BackendRecord> &baselines)
{
    if (!m_baselineStore) return false;

    QHash<QString, QString> uidToIcal;
    for (const auto &rec : baselines) {
        if (rec.id.isEmpty() || rec.isDeleted) continue;
        uidToIcal.insert(rec.id, QString::fromUtf8(rec.data));
    }
    return m_baselineStore->setBaselines(mappingId, uidToIcal);
}

// ---------------------------------------------------------------------------
// Calendar-typed convenience entry points (F1 Task 5)
// ---------------------------------------------------------------------------

SyncDiff CalendarDomainAdapter::diffCalendarRecords(
    const QList<SyncRecord> &source,
    const QList<SyncRecord> &target,
    const QMap<QString, QString> &baselines,
    SyncMode mode,
    bool useQuickPath) const
{
    if (useQuickPath || baselines.isEmpty()) {
        return computeQuickDiff(source, target, mode);
    }
    return computeSyncDiff(source, target, baselines, mode);
}

bool CalendarDomainAdapter::applyChangesToBackend(
    SyncBackend *backend,
    const QString &calendarId,
    const QList<SyncChange> &changes,
    bool useTargetRecord,
    const QString &mappingId,
    const TranscodingPlan &plan,
    QString *errorMessage)
{
    if (!backend) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "CalendarDomainAdapter::applyChangesToBackend - backend is null");
        return false;
    }
    if (!m_collection) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "CalendarDomainAdapter::applyChangesToBackend - no collection");
        return false;
    }

    KCalendarCore::MemoryCalendar *cal = m_collection->calendar(calendarId);
    if (!cal) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "CalendarDomainAdapter::applyChangesToBackend - calendar not found: %1")
                .arg(calendarId);
        return false;
    }

    const QString direction = useTargetRecord ? QStringLiteral("source")
                                              : QStringLiteral("target");
    const QString txId = QStringLiteral("sync-%1-%2-%3")
        .arg(mappingId, direction,
             QString::number(QDateTime::currentMSecsSinceEpoch()));

    SyncTransaction tx(txId);
    int itemCount = 0;

    for (const auto &change : changes) {
        // F2 Task 19: per-record cancellation check. The oracle is
        // installed by SyncEngine at construction and reads the
        // worker's m_cancelled atomic with acquire ordering. This
        // catches "cancel was already requested when we entered the
        // loop" and prevents dispatching the next record. The
        // already-built SyncTransaction is dropped (commitAll() is
        // not called). Returns false to indicate apply did not
        // complete fully; cancellation isn't an error, so
        // errorMessage is left empty — outer logic interprets
        // success == false as the cancellation marker.
        if (m_cancelOracle && m_cancelOracle()) {
            qInfo() << "CalendarDomainAdapter::applyChangesToBackend -"
                    << "cancellation observed mid-apply, returning early"
                    << "(itemsBuiltSoFar:" << itemCount << ")";
            return false;
        }
        switch (change.type) {
            case SyncChangeType::Created: {
                KCalendarCore::Incidence::Ptr inc = useTargetRecord
                    ? change.targetRecord.incidence
                    : change.sourceRecord.incidence;
                if (!inc) break;

                auto *item = new CreateIncidenceItem(calendarId, inc, cal,
                                                      backend, plan);
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

                auto *item = new UpdateIncidenceItem(calendarId, oldInc, newInc,
                                                      cal, backend, plan);
                tx.addItem(item);
                itemCount++;
                break;
            }
            case SyncChangeType::Deleted: {
                KCalendarCore::Incidence::Ptr deletedInc = useTargetRecord
                    ? change.sourceRecord.incidence
                    : change.targetRecord.incidence;
                auto *item = new DeleteIncidenceItem(calendarId, change.uid,
                                                      deletedInc, backend);
                tx.addItem(item);
                itemCount++;
                break;
            }
            case SyncChangeType::Unchanged:
                break;
        }
    }

    if (itemCount == 0) {
        return true;
    }

    qDebug() << "CalendarDomainAdapter::applyChangesToBackend -"
             << "items:" << itemCount
             << "direction:" << direction
             << "txId:" << txId;

    // Backends are main-thread objects; commit must run there.
    // BlockingQueuedConnection blocks the calling (worker) thread until
    // commitAll() returns.
    bool txResult = false;
    QMetaObject::invokeMethod(backend, [&tx, &txResult]() {
        txResult = tx.commitAll();
    }, Qt::BlockingQueuedConnection);

    if (!txResult) {
        QStringList errors;
        for (auto *item : tx.items()) {
            if (!item->errorString().isEmpty()) {
                errors.append(item->errorString());
            }
        }
        const QString combined = errors.isEmpty()
            ? QStringLiteral("SyncTransaction commitAll() failed")
            : errors.join(QStringLiteral("; "));
        qWarning() << "CalendarDomainAdapter::applyChangesToBackend - "
                      "transaction failed:" << combined;
        if (errorMessage) *errorMessage = combined;
        return false;
    }

    return true;
}

} // namespace Kalburator::Sync
