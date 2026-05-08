#include "calendarplugin_writer.h"

#include "createincidenceitem.h"
#include "deleteincidenceitem.h"
#include "icalendarcollection.h"
#include "syncbackend.h"
#include "synctransaction.h"
#include "synctransactionitem.h"
#include "transcodingplan.h"
#include "updateincidenceitem.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Incidence>
#include <KCalendarCore/MemoryCalendar>

#include <QDebug>
#include <QMetaObject>
#include <QStringList>

namespace Kalburator::Calendar {

namespace {

/// Parse an iCal blob (UTF-8 bytes from `BackendRecord::data`) into
/// an Incidence::Ptr. Returns null on parse failure. Mirrors the
/// pattern used by `CalendarDomainAdapter::backendRecordToSyncRecord`
/// (calendardomainadapter.cpp:47-61).
KCalendarCore::Incidence::Ptr parseIncidence(const QByteArray &data)
{
    if (data.isEmpty()) return {};
    KCalendarCore::ICalFormat format;
    return format.fromString(QString::fromUtf8(data));
}

} // namespace

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CreateIncidenceItem;
using Kalburator::Sync::DeleteIncidenceItem;
using Kalburator::Sync::ICalendarCollection;
using Kalburator::Sync::SyncBackend;
using Kalburator::Sync::SyncTransaction;
using Kalburator::Sync::TranscodingPlan;
using Kalburator::Sync::UpdateIncidenceItem;

CalendarPluginWriter::CalendarPluginWriter(SyncBackend *backend)
    : m_backend(backend)
{}

CalendarPluginWriter::~CalendarPluginWriter() = default;

void CalendarPluginWriter::setCollection(ICalendarCollection *collection)
{
    m_collection = collection;
}

bool CalendarPluginWriter::apply(
    const QString &collectionId,
    const QList<BackendRecord> &creates,
    const QList<BackendRecord> &updates,
    const QStringList &deletes)
{
    if (!m_backend) {
        qWarning() << "CalendarPluginWriter::apply - backend is null";
        return false;
    }
    if (!m_collection) {
        qWarning() << "CalendarPluginWriter::apply - no collection set"
                   << "(engine must call setCollection() before apply())";
        return false;
    }

    KCalendarCore::MemoryCalendar *cal = m_collection->calendar(collectionId);
    if (!cal) {
        qWarning() << "CalendarPluginWriter::apply - calendar not found:"
                   << collectionId;
        return false;
    }

    const QString txId =
        QStringLiteral("calendar-writer-%1").arg(collectionId);

    SyncTransaction tx(txId);
    int itemCount = 0;

    // v1: no per-record cancellation oracle. The unified engine's
    // cancel channel will be wired in a later task; for now we commit
    // the batch straight through.
    const TranscodingPlan plan{};

    for (const auto &r : creates) {
        auto inc = parseIncidence(r.data);
        if (!inc) {
            qWarning() << "CalendarPluginWriter::apply - skipping create,"
                       << "iCal parse failed (id:" << r.id << ")";
            continue;
        }
        tx.addItem(new CreateIncidenceItem(collectionId, inc, cal,
                                           m_backend, plan));
        ++itemCount;
    }

    for (const auto &r : updates) {
        auto newInc = parseIncidence(r.data);
        if (!newInc) {
            qWarning() << "CalendarPluginWriter::apply - skipping update,"
                       << "iCal parse failed (id:" << r.id << ")";
            continue;
        }
        // The IRecordWriter contract gives us the new record only.
        // The legacy `applyChangesToBackend` had `oldInc` from the
        // SyncChange's other side; the unified engine merges before
        // dispatch and feeds us the post-merge value. UpdateIncidenceItem
        // tolerates a null oldIncidence (it's used for rollback only).
        KCalendarCore::Incidence::Ptr oldInc;
        tx.addItem(new UpdateIncidenceItem(collectionId, oldInc, newInc,
                                           cal, m_backend, plan));
        ++itemCount;
    }

    for (const auto &id : deletes) {
        // Null deletedIncidence is acceptable: the legacy code path
        // also passes null when the parsed incidence isn't available
        // (used for rollback only).
        tx.addItem(new DeleteIncidenceItem(collectionId, id,
                                           KCalendarCore::Incidence::Ptr{},
                                           m_backend));
        ++itemCount;
    }

    if (itemCount == 0) return true;

    qDebug() << "CalendarPluginWriter::apply -"
             << "items:" << itemCount
             << "txId:" << txId;

    // Backends are main-thread objects; commit must run there.
    // BlockingQueuedConnection blocks the calling (worker) thread until
    // commitAll() returns. Same pattern as
    // CalendarDomainAdapter::applyChangesToBackend.
    bool txResult = false;
    QMetaObject::invokeMethod(m_backend, [&tx, &txResult]() {
        txResult = tx.commitAll();
    }, Qt::BlockingQueuedConnection);

    if (!txResult) {
        QStringList errors;
        for (auto *item : tx.items()) {
            if (!item->errorString().isEmpty())
                errors.append(item->errorString());
        }
        const QString combined = errors.isEmpty()
            ? QStringLiteral("SyncTransaction commitAll() failed")
            : errors.join(QStringLiteral("; "));
        qWarning() << "CalendarPluginWriter::apply -"
                   << "transaction failed:" << combined;
        return false;
    }
    return true;
}

} // namespace Kalburator::Calendar
