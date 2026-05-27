#include "calendarplugin_writer.h"

#include "createincidenceitem.h"
#include "deleteincidenceitem.h"
#include "iblobbackend.h"
#include "icalendarcollection.h"
#include "syncbackend.h"
#include "synctransaction.h"
#include "synctransactionitem.h"
#include "updateincidenceitem.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Incidence>
#include <KCalendarCore/MemoryCalendar>

#include <QDebug>
#include <QMetaObject>
#include <QStringList>
#include <QThread>

namespace Kalburator::Calendar {

namespace {

/// Parse an iCal blob (UTF-8 bytes from `BackendRecord::data`) into
/// an Incidence::Ptr. Returns null on parse failure.
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
using Kalburator::Sync::IBlobBackend;
using Kalburator::Sync::ICalendarCollection;
using Kalburator::Sync::SyncBackend;
using Kalburator::Sync::SyncTransaction;
using Kalburator::Sync::UpdateIncidenceItem;

CalendarPluginWriter::CalendarPluginWriter(SyncBackend *backend)
    : m_backend(backend)
{}

CalendarPluginWriter::~CalendarPluginWriter() = default;

void CalendarPluginWriter::prepareForApply(const ApplyContext &ctx)
{
    // Phase K.4 setup hook. Replaces the engine-side
    // `dynamic_cast<CalendarPluginWriter*>` + setCollection() dance.
    m_directCalendar = ctx.calendarCollection;
    m_prepared = true;
}

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

    // Resolve the host MemoryCalendar.
    // - If the engine handed one in via prepareForApply(), use it.
    // - Else fall back to ICalendarCollection lookup (legacy path).
    // - If neither is set at all, the caller forgot to wire us up —
    //   return false (preserves the pre-K.4 contract for direct
    //   callers / unit tests that construct the writer outside the
    //   engine).
    // - If prepareForApply() WAS called and the engine deliberately
    //   passed a null calendarCollection (e.g. RemoteCalendarBackend
    //   with no host MemoryCalendar), take the blob-only fallback
    //   path. This is what unblocks Phase J Task 9 (palm -> caldav).
    KCalendarCore::MemoryCalendar *cal = m_directCalendar;
    if (!cal && m_collection) {
        cal = m_collection->calendar(collectionId);
    }

    if (!cal && !m_prepared && !m_collection) {
        // Legacy caller path: no setCollection() AND no prepareForApply().
        // Preserve the pre-K.4 behaviour for tests that exercise this
        // explicitly (`apply_returnsFalse_whenNoCollection`).
        qWarning() << "CalendarPluginWriter::apply - no collection set"
                   << "(engine must call prepareForApply() or "
                   << "setCollection() before apply())";
        return false;
    }

    if (!cal && m_collection) {
        // setCollection() was called but the lookup failed: the
        // collection lacks an entry for this id. Pre-K.4 behaviour:
        // return false. (Don't enter blob fallback — that's reserved
        // for the engine path where prepareForApply explicitly opts
        // in by passing a null calendarCollection.)
        qWarning() << "CalendarPluginWriter::apply - calendar not found:"
                   << collectionId;
        return false;
    }

    if (!cal) {
        // ---- Blob-only fallback (Phase K.4 / Phase J Task 9) ----------------
        // No host MemoryCalendar available. The backend MUST still be
        // a SyncBackend (so we have its IBlobBackend surface) — drive
        // create/update/delete through that, on the backend thread.
        auto *blob = dynamic_cast<IBlobBackend *>(m_backend);
        if (!blob) {
            qWarning() << "CalendarPluginWriter::apply - no MemoryCalendar"
                       << "and backend does not expose IBlobBackend ("
                       << collectionId << ")";
            return false;
        }

        // Only iCal-validate when the target backend actually speaks iCal.
        // A non-iCal target (e.g. (calendar,palm)) gets native-wire bytes from
        // the engine's canon->native demote; parsing those as iCal would wrongly
        // drop every record. Let the backend validate in its own format.
        const bool targetIsICal =
            m_backend->shapeFor(collectionId).encoding
                == Kalburator::Shape::EncodingId{QStringLiteral("ical")};

        bool ok = true;
        QMetaObject::invokeMethod(m_backend, [this, &creates, &updates, &deletes,
                                              &collectionId, blob, &ok,
                                              targetIsICal]() {
            for (const auto &r : creates) {
                if (targetIsICal && !parseIncidence(r.data)) {
                    qWarning() << "CalendarPluginWriter::apply (blob path)"
                               << "- skipping create, iCal parse failed (id:" << r.id << ")";
                    continue;
                }
                BackendRecord rec = r;
                if (blob->createRecord(collectionId, rec).isEmpty()) {
                    // Mirror the legacy behavior: log and continue rather
                    // than abort the batch; an empty createRecord() result
                    // means the backend rejected the record.
                    qWarning() << "CalendarPluginWriter::apply (blob path)"
                               << "- createRecord returned empty for"
                               << r.id;
                    ok = false;
                }
            }
            for (const auto &r : updates) {
                if (targetIsICal && !parseIncidence(r.data)) {
                    qWarning() << "CalendarPluginWriter::apply (blob path)"
                               << "- skipping update, iCal parse failed (id:" << r.id << ")";
                    continue;
                }
                if (!blob->updateRecord(r)) {
                    qWarning() << "CalendarPluginWriter::apply (blob path)"
                               << "- updateRecord failed for" << r.id;
                    ok = false;
                }
            }
            for (const auto &id : deletes) {
                if (!blob->deleteRecord(id)) {
                    qWarning() << "CalendarPluginWriter::apply (blob path)"
                               << "- deleteRecord failed for" << id;
                    ok = false;
                }
            }
        }, Qt::BlockingQueuedConnection);
        return ok;
    }

    // ---- Standard SyncTransaction path -------------------------------------
    const QString txId =
        QStringLiteral("calendar-writer-%1").arg(collectionId);

    SyncTransaction tx(txId);
    int itemCount = 0;

    for (const auto &r : creates) {
        auto inc = parseIncidence(r.data);
        if (!inc) {
            qWarning() << "CalendarPluginWriter::apply - skipping create,"
                       << "iCal parse failed (id:" << r.id << ")";
            continue;
        }
        tx.addItem(new CreateIncidenceItem(collectionId, inc, cal,
                                           m_backend));
        ++itemCount;
    }

    for (const auto &r : updates) {
        auto newInc = parseIncidence(r.data);
        if (!newInc) {
            qWarning() << "CalendarPluginWriter::apply - skipping update,"
                       << "iCal parse failed (id:" << r.id << ")";
            continue;
        }
        KCalendarCore::Incidence::Ptr oldInc;
        tx.addItem(new UpdateIncidenceItem(collectionId, oldInc, newInc,
                                           cal, m_backend));
        ++itemCount;
    }

    QHash<QString, KCalendarCore::Incidence::Ptr> oldIncs;
    if (!deletes.isEmpty()) {
        QMetaObject::invokeMethod(m_backend, [this, &deletes, &oldIncs]() {
            auto *blob = dynamic_cast<Kalburator::Sync::IBlobBackend *>(m_backend);
            if (!blob) return;
            KCalendarCore::ICalFormat fmt;
            for (const auto &id : deletes) {
                auto rec = blob->loadRecord(id);
                if (rec && !rec->data.isEmpty())
                    if (auto inc = fmt.fromString(QString::fromUtf8(rec->data)); inc)
                        oldIncs.insert(id, inc);
            }
        }, Qt::BlockingQueuedConnection);
    }

    for (const auto &id : deletes) {
        tx.addItem(new DeleteIncidenceItem(collectionId, id,
                                           oldIncs.value(id),
                                           m_backend));
        ++itemCount;
    }

    if (itemCount == 0) return true;

    qDebug() << "CalendarPluginWriter::apply -"
             << "items:" << itemCount
             << "txId:" << txId;

    Q_ASSERT_X(QThread::currentThread() != m_backend->thread(),
               "CalendarPluginWriter::apply",
               "BlockingQueuedConnection requires a different thread than backend");
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
