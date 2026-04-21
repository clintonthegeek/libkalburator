#include "blobsyncengine.h"

#include <QHash>
#include <QSet>
#include <QMap>

#include "backendrecord.h"
#include "iblobbackend.h"
#include "blobbaselinestore.h"
#include "conflicthandlerregistry.h"
#include "conflictpolicy.h"
#include "conflictrecord.h"
#include "conflictstore.h"

namespace Kalburator::Sync {

BlobSyncEngine::BlobSyncEngine(QObject *parent)
    : QObject(parent)
{
}

BlobSyncEngine::~BlobSyncEngine() = default;

namespace {

QHash<QString, BackendRecord> indexById(const QList<BackendRecord> &records)
{
    QHash<QString, BackendRecord> out;
    out.reserve(records.size());
    for (const auto &r : records) {
        out.insert(r.id, r);
    }
    return out;
}

} // namespace

BlobSyncResult BlobSyncEngine::mirror(IBlobBackend *source,
                                      IBlobBackend *target,
                                      const QString &collectionId)
{
    BlobSyncResult result;
    if (!source || !target) {
        result.success = false;
        result.errorMessage = QStringLiteral("mirror: null backend");
        Q_EMIT finished(result);
        return result;
    }

    const auto srcRecords = source->loadRecords(collectionId);
    const auto tgtRecords = target->loadRecords(collectionId);
    const auto tgtById = indexById(tgtRecords);

    const int total = srcRecords.size() + tgtRecords.size();
    int step = 0;
    Q_EMIT progressChanged(step, total, QStringLiteral("mirror: starting"));

    // Copy source → target (create or update).
    for (const auto &sr : srcRecords) {
        ++step;
        const auto it = tgtById.constFind(sr.id);
        if (it == tgtById.constEnd()) {
            // Record doesn't exist in target — create.
            if (target->createRecord(collectionId, sr).isEmpty()) {
                ++result.targetStats.errors;
            } else {
                ++result.targetStats.created;
            }
        } else if (it.value().contentHash != sr.contentHash) {
            // Exists but different — update.
            BackendRecord out = sr;
            out.id = it.value().id; // keep target's id
            if (!target->updateRecord(out)) {
                ++result.targetStats.errors;
            } else {
                ++result.targetStats.updated;
            }
        } else {
            ++result.targetStats.unchanged;
        }
        Q_EMIT progressChanged(step, total, QStringLiteral("mirror: copying"));
    }

    // Delete target records not in source.
    const auto srcById = indexById(srcRecords);
    for (const auto &tr : tgtRecords) {
        ++step;
        if (!srcById.contains(tr.id)) {
            if (!target->deleteRecord(tr.id)) {
                ++result.targetStats.errors;
            } else {
                ++result.targetStats.deleted;
            }
        }
        Q_EMIT progressChanged(step, total, QStringLiteral("mirror: pruning"));
    }

    result.success = (result.targetStats.errors == 0);
    Q_EMIT finished(result);
    return result;
}

BlobSyncResult BlobSyncEngine::twoWayNaive(IBlobBackend *a,
                                           IBlobBackend *b,
                                           const QString &collectionId)
{
    BlobSyncResult result;
    if (!a || !b) {
        result.success = false;
        result.errorMessage = QStringLiteral("twoWayNaive: null backend");
        Q_EMIT finished(result);
        return result;
    }

    const auto aRecords = a->loadRecords(collectionId);
    const auto bRecords = b->loadRecords(collectionId);
    const auto aById = indexById(aRecords);
    const auto bById = indexById(bRecords);

    const int total = aRecords.size() + bRecords.size();
    int step = 0;
    Q_EMIT progressChanged(step, total, QStringLiteral("twoWayNaive: starting"));

    // A → B: records only on A, or A newer than B
    for (const auto &ar : aRecords) {
        ++step;
        const auto it = bById.constFind(ar.id);
        if (it == bById.constEnd()) {
            if (b->createRecord(collectionId, ar).isEmpty()) {
                ++result.targetStats.errors;
            } else {
                ++result.targetStats.created;
            }
        } else if (ar.contentHash != it.value().contentHash
                   && ar.lastModified > it.value().lastModified) {
            BackendRecord out = ar;
            out.id = it.value().id;
            if (!b->updateRecord(out)) {
                ++result.targetStats.errors;
            } else {
                ++result.targetStats.updated;
            }
        }
        Q_EMIT progressChanged(step, total, QStringLiteral("twoWayNaive: A->B"));
    }

    // B → A: records only on B, or B newer than A
    for (const auto &br : bRecords) {
        ++step;
        const auto it = aById.constFind(br.id);
        if (it == aById.constEnd()) {
            if (a->createRecord(collectionId, br).isEmpty()) {
                ++result.sourceStats.errors;
            } else {
                ++result.sourceStats.created;
            }
        } else if (br.contentHash != it.value().contentHash
                   && br.lastModified > it.value().lastModified) {
            BackendRecord out = br;
            out.id = it.value().id;
            if (!a->updateRecord(out)) {
                ++result.sourceStats.errors;
            } else {
                ++result.sourceStats.updated;
            }
        }
        Q_EMIT progressChanged(step, total, QStringLiteral("twoWayNaive: B->A"));
    }

    result.success = (result.sourceStats.errors == 0 && result.targetStats.errors == 0);
    Q_EMIT finished(result);
    return result;
}

BlobSyncResult BlobSyncEngine::twoWayWithBaseline(
    IBlobBackend *a,
    IBlobBackend *b,
    const QString &collectionId,
    const QString &mappingId,
    BlobBaselineStore *baseline,
    QSyncCore::ConflictHandlerRegistry *handlers,
    QSyncCore::ConflictStore *conflicts,
    const QSyncCore::ConflictPolicy &policy)
{
    BlobSyncResult result;
    if (!a || !b || !baseline) {
        result.success = false;
        result.errorMessage = QStringLiteral(
            "twoWayWithBaseline: null backend or baseline store");
        return result;
    }

    const QHash<QString, BackendRecord> byIdA = indexById(a->loadRecords(collectionId));
    const QHash<QString, BackendRecord> byIdB = indexById(b->loadRecords(collectionId));

    QHash<QString, QString> baselineHashes;
    const QStringList baseIds = baseline->baselineRecordIds(mappingId);
    for (const QString &id : baseIds) {
        baselineHashes.insert(id, baseline->baselineHash(mappingId, id));
    }

    QSet<QString> allIds;
    for (auto it = byIdA.constBegin(); it != byIdA.constEnd(); ++it) allIds.insert(it.key());
    for (auto it = byIdB.constBegin(); it != byIdB.constEnd(); ++it) allIds.insert(it.key());
    for (auto it = baselineHashes.constBegin(); it != baselineHashes.constEnd(); ++it) allIds.insert(it.key());

    QMap<QString, QString> finalHashes;

    for (const QString &id : allIds) {
        const bool hasA = byIdA.contains(id);
        const bool hasB = byIdB.contains(id);
        const bool hasBase = baselineHashes.contains(id);

        if (hasA && hasB && hasBase) {
            const BackendRecord ra = byIdA.value(id);
            const BackendRecord rb = byIdB.value(id);
            const QString bHash = baselineHashes.value(id);
            const bool aChanged = (ra.contentHash != bHash);
            const bool bChanged = (rb.contentHash != bHash);

            if (!aChanged && !bChanged) {
                result.sourceStats.unchanged++;
                result.targetStats.unchanged++;
                finalHashes.insert(id, ra.contentHash);
            } else if (aChanged && !bChanged) {
                if (b->updateRecord(ra)) {
                    result.targetStats.updated++;
                    finalHashes.insert(id, ra.contentHash);
                } else {
                    result.targetStats.errors++;
                    finalHashes.insert(id, bHash);
                }
            } else if (!aChanged && bChanged) {
                if (a->updateRecord(rb)) {
                    result.sourceStats.updated++;
                    finalHashes.insert(id, rb.contentHash);
                } else {
                    result.sourceStats.errors++;
                    finalHashes.insert(id, bHash);
                }
            } else {
                // Both modified → conflict.
                QSyncCore::ConflictRecord cr;
                cr.conflictId = QStringLiteral("%1:%2").arg(mappingId, id);
                cr.conduitId = mappingId;
                cr.type = QSyncCore::ConflictType::BothModified;
                cr.source.id = ra.id;
                cr.source.description = ra.displayName;
                cr.source.content = ra.data;
                cr.source.contentHash = ra.contentHash;
                cr.source.contentType = ra.type;
                cr.source.lastModified = ra.lastModified;
                cr.target.id = rb.id;
                cr.target.description = rb.displayName;
                cr.target.content = rb.data;
                cr.target.contentHash = rb.contentHash;
                cr.target.contentType = rb.type;
                cr.target.lastModified = rb.lastModified;
                cr.detectedAt = QDateTime::currentDateTimeUtc();

                QSyncCore::ConflictHandler *h = handlers
                    ? handlers->handlerFor(a->backendId())
                    : nullptr;

                QSyncCore::ConflictDecision decision = QSyncCore::ConflictDecision::Pending;
                if (h) decision = h->handleConflict(cr, policy);

                if (decision == QSyncCore::ConflictDecision::UseSource) {
                    if (b->updateRecord(ra)) {
                        result.targetStats.updated++;
                        finalHashes.insert(id, ra.contentHash);
                    } else {
                        result.targetStats.errors++;
                        finalHashes.insert(id, bHash);
                    }
                } else if (decision == QSyncCore::ConflictDecision::UseTarget) {
                    if (a->updateRecord(rb)) {
                        result.sourceStats.updated++;
                        finalHashes.insert(id, rb.contentHash);
                    } else {
                        result.sourceStats.errors++;
                        finalHashes.insert(id, bHash);
                    }
                } else {
                    if (conflicts) conflicts->addConflict(cr);
                    result.sourceStats.conflicts++;
                    finalHashes.insert(id, bHash);
                }
            }
        } else if (!hasA && hasB && hasBase) {
            // Deleted on A since baseline → delete on B.
            if (b->deleteRecord(id)) {
                result.targetStats.deleted++;
            } else {
                result.targetStats.errors++;
                finalHashes.insert(id, baselineHashes.value(id));
            }
        } else if (hasA && !hasB && hasBase) {
            // Deleted on B since baseline → delete on A.
            if (a->deleteRecord(id)) {
                result.sourceStats.deleted++;
            } else {
                result.sourceStats.errors++;
                finalHashes.insert(id, baselineHashes.value(id));
            }
        } else if (hasA && !hasB && !hasBase) {
            // New on A → create on B.
            const BackendRecord ra = byIdA.value(id);
            if (!b->createRecord(collectionId, ra).isEmpty()) {
                result.targetStats.created++;
                finalHashes.insert(id, ra.contentHash);
            } else {
                result.targetStats.errors++;
            }
        } else if (!hasA && hasB && !hasBase) {
            // New on B → create on A.
            const BackendRecord rb = byIdB.value(id);
            if (!a->createRecord(collectionId, rb).isEmpty()) {
                result.sourceStats.created++;
                finalHashes.insert(id, rb.contentHash);
            } else {
                result.sourceStats.errors++;
            }
        }
        // Other edge cases (both missing, or impossible combos) fall through.
    }

    if (!finalHashes.isEmpty()) {
        baseline->commitBaselines(mappingId, finalHashes);
    }

    result.success = (result.sourceStats.errors == 0 && result.targetStats.errors == 0);
    Q_EMIT finished(result);
    return result;
}

} // namespace Kalburator::Sync
