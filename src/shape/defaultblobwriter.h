#pragma once

#include "recordwriter.h"

namespace Kalburator::Sync { class IBlobBackend; }

namespace Kalburator::Shape {

/// E5.3 (sync-excellence campaign): when `m_backend` is a `SyncBackendBase`
/// (every real production construction site — LocalBackend, MockBackend,
/// RemoteCalendarBackend — is one), `apply()`'s body routes through
/// `SyncBackendBase::applyRecords()` (the same call the engine's live write
/// path uses directly, bypassing this class entirely) rather than calling
/// `createRecord`/`updateRecord`/`deleteRecord` itself. `apply()` checks
/// `isFinished()` immediately after the call — correct for backends without
/// async internals (LocalBackend, MockBackend), whose default
/// `applyRecords()` completes synchronously before returning. It
/// deliberately does NOT spin a `QEventLoop` to await an async backend's
/// `applyRecords()` (e.g. RemoteCalendarBackend) — nothing in the live
/// engine path calls `apply()` against such a backend anymore
/// (SyncEngineWorker::applyBatch calls `applyRecords()` directly and awaits
/// it properly); if a future caller needs `apply()` to be synchronously
/// awaitable against an async backend, that is a real design gap to raise,
/// not something to paper over here with a nested loop.
///
/// `m_backend` stays `IBlobBackend*` (NOT widened to `SyncBackendBase*`):
/// some existing callers (e.g. `tests/shape/tst_default_blob_writer.cpp`'s
/// `MockBlobBackend`, the universal-sink test fixtures) construct this
/// class directly over a plain `IBlobBackend` that predates
/// `SyncBackendBase` and has no `applyRecords()` override to call. For
/// those, `apply()` falls back to the pre-E5.3 per-record
/// createRecord/updateRecord/deleteRecord loop — unchanged behavior for a
/// case E5.3's write-path rework was never meant to touch (no engine
/// dispatch reaches a plain-IBlobBackend writer; only these narrower unit
/// tests construct DefaultBlobWriter directly against one).
class DefaultBlobWriter : public RecordWriter {
public:
    explicit DefaultBlobWriter(Kalburator::Sync::IBlobBackend *backend)
        : m_backend(backend) {}

    bool apply(
        const QString &collectionId,
        const QList<Kalburator::Sync::BackendRecord> &creates,
        const QList<Kalburator::Sync::BackendRecord> &updates,
        const QStringList &deletes) override;

private:
    Kalburator::Sync::IBlobBackend *m_backend;
};

} // namespace Kalburator::Shape
