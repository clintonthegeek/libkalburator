#ifndef KALBURATOR_BLOB_BLOBSYNCENGINE_H
#define KALBURATOR_BLOB_BLOBSYNCENGINE_H

#include <QObject>
#include <QString>

namespace Kalburator::Sync {

class IBlobBackend;

struct BlobSyncStats {
    int created   = 0;
    int updated   = 0;
    int deleted   = 0;
    int unchanged = 0;
    int errors    = 0;
};

struct BlobSyncResult {
    bool          success = true;
    QString       errorMessage;
    BlobSyncStats sourceStats;
    BlobSyncStats targetStats;
};

/**
 * @brief Minimum-viable lower-layer sync engine.
 *
 * Phase B2 scope: stateless one-way mirror and two-way naive
 * (last-write-wins-by-lastModified). No baseline, no 3-way merge,
 * no conflict-store integration — those are explicitly deferred
 * (see docs/phase0/04h-blob-layer-design.md §"Explicitly deferred").
 */
class BlobSyncEngine : public QObject {
    Q_OBJECT
public:
    explicit BlobSyncEngine(QObject *parent = nullptr);
    ~BlobSyncEngine() override;

    /// One-way: source → target. Target ends up mirroring source's
    /// `collectionId`. Records in target not present in source are
    /// deleted; records present in both with matching contentHash
    /// are left untouched.
    BlobSyncResult mirror(IBlobBackend *source,
                          IBlobBackend *target,
                          const QString &collectionId);

    /// Two-way: whichever side has the newer `lastModified` wins for
    /// records present on both sides. Records only on one side are
    /// copied to the other. Deletions are not propagated (cannot be
    /// distinguished from "never existed" without a baseline).
    BlobSyncResult twoWayNaive(IBlobBackend *a,
                               IBlobBackend *b,
                               const QString &collectionId);

Q_SIGNALS:
    void progressChanged(int current, int total, const QString &message);
    void finished(const BlobSyncResult &result);
};

} // namespace Kalburator::Sync

#endif
