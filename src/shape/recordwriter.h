#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "backendrecord.h"

namespace KCalendarCore { class MemoryCalendar; }

namespace Kalburator::Shape {

/// Apply create/update/delete operations against a backend's
/// target collection. One implementation per domain, returned by
/// DomainPlugin::createWriter().
///
/// **Phase K.4 (threading model)**: writers declare which thread the
/// engine must call `apply()` on, and an opaque `prepareForApply()`
/// hook is used to inject any per-call setup (replacing the previous
/// `dynamic_cast<CalendarPluginWriter*>` + `setCollection()` /
/// `prepareForApply()` hook) engine-side dance.
///
/// **E5.3 (sync-excellence campaign, CP-A amendment A3, 2026-07-08):** the
/// `Threading` enum and `threading()` are GONE. They existed to tell the
/// engine which thread to marshal `apply()` onto — a distinction that only
/// mattered because `apply()` itself blocked the calling thread for the
/// full duration of the (possibly network-bound) write. The engine no
/// longer calls `apply()` at all in the live write path:
/// `SyncEngineWorker::applyBatch` now calls `SyncBackendBase::
/// applyRecords()` directly, which returns immediately with a
/// `WriteOperation` the worker awaits the same (cancellable, watchdogged)
/// way it awaits a fetch gate — no thread-affinity decision needed, because
/// nothing blocks. `RecordWriter`/`DefaultBlobWriter` still exist
/// (`DefaultBlobWriter::apply()` now itself routes through
/// `applyRecords()`, for any caller still reaching `apply()` directly), but
/// the `threading()` contract they used to negotiate has no reader left. A
/// repo-wide grep across libkalburator, PlanStan, and WildPalms at removal
/// time found zero `threading()` overrides (the enum's only implementation
/// was the default) and zero WildPalms references to `RecordWriter` or the
/// (also-deleted) `awaitOperation` at all.
class RecordWriter {
public:
    virtual ~RecordWriter() = default;

    /// Per-apply context the engine hands the writer right before
    /// calling `apply()`. Domain-typed fields (e.g. the host
    /// `MemoryCalendar*`) are optional — calendar-typed writers use
    /// it when present and fall back to working from
    /// `BackendRecord::data` (raw iCal bytes) when null.
    struct ApplyContext {
        QString collectionId;
        /// Optional: target backend's host MemoryCalendar (when the
        /// engine has one bound). May be null for backends without a
        /// host-resident MemoryCalendar (e.g. RemoteCalendarBackend
        /// when used via the unified blob path).
        KCalendarCore::MemoryCalendar *calendarCollection = nullptr;
    };

    /// Hook invoked by the engine before each `apply()`. Default: no-op.
    virtual void prepareForApply(const ApplyContext &ctx) { Q_UNUSED(ctx); }

    /// Apply a batch of operations. Implementations may run them
    /// inside a transaction (calendar plugin does), or as
    /// independent calls (default SyncBackend writer).
    virtual bool apply(
        const QString &collectionId,
        const QList<Kalburator::Sync::BackendRecord> &creates,
        const QList<Kalburator::Sync::BackendRecord> &updates,
        const QStringList &deletes) = 0;
};

} // namespace Kalburator::Shape
