#ifndef KALBURATOR_SYNC_WRITERBATCH_H
#define KALBURATOR_SYNC_WRITERBATCH_H

#include <QList>
#include <QStringList>

#include "backendrecord.h"

namespace Kalburator::Sync {

/// Classified batch of records ready to hand to a backend's write path:
/// `creates`/`updates` are BackendRecord-typed (blob-neutral, raw bytes +
/// metadata), `deletes` is just the id list. Produced by the engine's
/// `classifyForWriter()` (src/engine/syncengine.cpp) from a diff's
/// post-merge "to write" list, by loading the destination's existing record
/// ids and bucketing each incoming record into create/update/delete.
///
/// E5.3 (sync-excellence campaign, audit B7 / CP-A): moved here from its
/// original home as a local struct defined inside syncengine.cpp, because
/// `SyncBackendBase::applyRecords()` (syncbackendbase.h) needs to name this
/// type and `sync/` must not depend on `engine/`. Consumed by
/// `SyncBackendBase::applyRecords()` and (legacy, pre-E5.3) `RecordWriter::apply()`.
struct WriterBatch {
    QList<BackendRecord> creates;
    QList<BackendRecord> updates;
    QStringList          deletes;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_WRITERBATCH_H
