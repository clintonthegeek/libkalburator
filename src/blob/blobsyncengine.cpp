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
    Q_UNUSED(collectionId); Q_UNUSED(mappingId);
    Q_UNUSED(handlers); Q_UNUSED(conflicts); Q_UNUSED(policy);

    BlobSyncResult result;
    if (!a || !b || !baseline) {
        result.success = false;
        result.errorMessage = QStringLiteral(
            "twoWayWithBaseline: null backend or baseline store");
        return result;
    }
    result.errorMessage = QStringLiteral("twoWayWithBaseline: not implemented yet");
    result.success = false;
    return result;
}

} // namespace Kalburator::Sync
