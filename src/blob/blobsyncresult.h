#ifndef KALBURATOR_BLOB_BLOBSYNCRESULT_H
#define KALBURATOR_BLOB_BLOBSYNCRESULT_H

#include <QString>

namespace Kalburator::Sync {

/// Per-side counters for a one-shot blob sync. Returned by
/// SyncEngine::runBlobTwoWay / runBlobMirror (Phase F1 Task 6).
///
/// Phase F1 Task 10 (2026-04-29): split out from blobsyncengine.h
/// before that file was deleted. The structs survive because they
/// are part of SyncEngine's public one-shot API; the engine class
/// that originally hosted them does not.
struct BlobSyncStats {
    int created   = 0;
    int updated   = 0;
    int deleted   = 0;
    int unchanged = 0;
    int errors    = 0;
    int conflicts = 0;  ///< Unresolved conflicts (deferred to ConflictStore).
};

struct BlobSyncResult {
    bool          success = true;
    QString       errorMessage;
    BlobSyncStats sourceStats;
    BlobSyncStats targetStats;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_BLOB_BLOBSYNCRESULT_H
