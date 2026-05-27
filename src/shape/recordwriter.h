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
class RecordWriter {
public:
    virtual ~RecordWriter() = default;

    /// Threading contract that controls how the engine dispatches
    /// `apply()`. The default is `BackendThread` — the engine wraps
    /// `apply()` in a `BlockingQueuedConnection` to the backend's own
    /// thread. A writer may override to `WorkerThread` when its
    /// `apply()` uses BlockingQueuedConnection internally and therefore
    /// must be called from a thread that is NOT the backend thread.
    enum class Threading { BackendThread, WorkerThread };

    virtual Threading threading() const { return Threading::BackendThread; }

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
