#include "msgraphcalendarbackend.h"

#include "graphapiclient.h"
#include "mseventcanonstages.h"
#include "icalcanonstages.h"
#include "icalcodec.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>

using Kalburator::Graph::GraphApiClient;
using Kalburator::Graph::GraphError;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::Shape;

namespace {

/// Compact wire bytes for one event object (records store exactly what the
/// server sent — the edge stages own every mapping decision).
QByteArray compactWire(const QJsonObject &event)
{
    return QJsonDocument(event).toJson(QJsonDocument::Compact);
}

QByteArray sha256Hex(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes,
                                    QCryptographicHash::Sha256).toHex();
}

} // namespace

namespace Kalburator::Sync {

MSGraphCalendarBackend::MSGraphCalendarBackend(QObject *parent)
    : SyncBackend(parent)
    , m_client(new GraphApiClient(this))
{
}

MSGraphCalendarBackend::~MSGraphCalendarBackend() = default;

void MSGraphCalendarBackend::setBaseUrl(const QString &baseUrl)
{
    m_client->setBaseUrl(baseUrl);
}

void MSGraphCalendarBackend::setAccessToken(const QString &token)
{
    m_client->setAccessToken(token);
}

void MSGraphCalendarBackend::setCollectionPath(const QString &path)
{
    m_collectionPath = path;
}

QString MSGraphCalendarBackend::backendType() const
{
    return QStringLiteral("msgraph");
}

QList<Kalburator::Shape::Shape> MSGraphCalendarBackend::nativeShapes() const
{
    return { Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("calendar")},
        Kalburator::Shape::EncodingId{QStringLiteral("ms-event")} } };
}

// ---------------------------------------------------------------------------
// Read path
// ---------------------------------------------------------------------------

FetchOperation *MSGraphCalendarBackend::fetchItems(const QString &calendarId)
{
    auto *op = new FetchOperation(calendarId, this);

    enqueueOperation(calendarId, op, [this, op, calendarId]() {
        if (op->state() == SyncOperation::Cancelled)
            return;
        op->setState(SyncOperation::Running);
        emit fetchStarted(calendarId, 0);

        m_client->fetchCollection(
            m_collectionPath,
            [this, op, calendarId](std::optional<QJsonArray> items,
                                   const GraphError &err) {
                if (op->state() == SyncOperation::Cancelled)
                    return;
                if (!items.has_value()) {
                    const QString msg = err.networkError
                        ? QStringLiteral("network error: %1").arg(err.message)
                        : QStringLiteral("graph error %1 (%2): %3")
                              .arg(err.httpStatus).arg(err.code, err.message);
                    op->fail(msg);
                    emit fetchFinished(calendarId, false, msg);
                    return;
                }

                QList<BackendRecord> records;
                for (const auto &iv : *items) {
                    const QJsonObject ev = iv.toObject();
                    BackendRecord r;
                    r.id = ev.value(QStringLiteral("id")).toString();
                    r.type = QStringLiteral("event");
                    r.displayName =
                        ev.value(QStringLiteral("subject")).toString();
                    r.data = compactWire(ev);
                    r.contentHash = sha256Hex(r.data);
                    r.lastModified = QDateTime::fromString(
                        ev.value(QStringLiteral("lastModifiedDateTime"))
                            .toString(),
                        Qt::ISODate);
                    records.append(r);
                }

                QList<KCalendarCore::Incidence::Ptr> incidences;
                for (const auto &r : records) {
                    for (const auto &inc : incidencesForRecord(r.data))
                        incidences.append(inc);
                }

                emit itemsFetched(calendarId, incidences);
                op->setFetchedItems(incidences);
                m_lastFetchRecords[calendarId] = records;
                op->complete();
                emit fetchFinished(calendarId, true);
                emit syncCompleted(calendarId);
            });
    });
    return op;
}

bool MSGraphCalendarBackend::recordsFromLastFetch(
    const QString &collectionId, QList<BackendRecord> &records,
    QString &errorMessage)
{
    const auto it = m_lastFetchRecords.constFind(collectionId);
    if (it == m_lastFetchRecords.constEnd()) {
        errorMessage = QStringLiteral("no fetch memo for collection %1")
                           .arg(collectionId);
        return false;
    }
    records = it.value();
    m_lastFetchRecords.remove(collectionId);   // single-shot (H5/O23)
    return true;
}

QList<BackendRecord> MSGraphCalendarBackend::loadRecords(
    const QString &collectionId)
{
    return m_lastFetchRecords.value(collectionId);
}

QList<KCalendarCore::Incidence::Ptr>
MSGraphCalendarBackend::incidencesForRecord(const QByteArray &wireJson) const
{
    // Legacy-surface conversion: ms-event → canon → iCal → Incidence.
    // The engine's unified path never needs this (it consumes records and
    // promotes via the registered edge); this serves FetchOperation's
    // Incidence-typed contract only.
    const Calendar::MsEventToCanonStage promote;
    const QByteArray canonBytes = promote.transform(wireJson);
    if (canonBytes.isEmpty())
        return {};
    const Calendar::CanonToICalStage demoteIcal;
    const QByteArray icalBytes = demoteIcal.transform(canonBytes);
    if (icalBytes.isEmpty())
        return {};
    return incidencesFromIcal(QString::fromUtf8(icalBytes));
}

// ---------------------------------------------------------------------------
// Write path
// ---------------------------------------------------------------------------

/// Heap-owned sequential-apply state (see header): one HTTP request in
/// flight at a time; callbacks capture the shared state so nothing references
/// dead stack frames.
struct MSGraphCalendarBackend::ApplyState {
    WriteOperation *op = nullptr;
    QString collectionId;
    QList<BackendRecord> creates;
    QList<BackendRecord> updates;
    QStringList deletes;
    int total = 0;
};

WriteOperation *MSGraphCalendarBackend::applyRecords(
    const QString &collectionId, const WriterBatch &batch)
{
    auto *op = new WriteOperation(collectionId, this);

    enqueueOperation(collectionId, op, [this, op, collectionId, batch]() {
        if (op->state() == SyncOperation::Cancelled)
            return;
        op->setState(SyncOperation::Running);
        auto st = std::make_shared<ApplyState>();
        st->op = op;
        st->collectionId = collectionId;
        st->creates = batch.creates;
        st->updates = batch.updates;
        st->deletes = batch.deletes;
        st->total = st->creates.size() + st->updates.size()
            + st->deletes.size();
        emit writeStarted(collectionId, st->total);
        applyStep(st);
    });
    return op;
}

void MSGraphCalendarBackend::applyStep(std::shared_ptr<ApplyState> st)
{
    auto *op = st->op;
    if (op->state() == SyncOperation::Cancelled)
        return;

    auto settleOne = [this, st](bool ok, const QString &id) {
        if (ok)
            st->op->addSucceededUid(id);
        else
            st->op->addFailedUid(id);
        emit writeProgressChanged(
            st->collectionId,
            st->op->succeededUids().size() + st->op->failedUids().size(),
            st->total);
    };

    auto finishIfSettled = [this, st]() {
        // Terminal-state contract: complete() when ANY record succeeded or
        // the batch was empty; fail() only when ALL attempted failed.
        if (!st->op->succeededUids().isEmpty() || st->op->failedUids().isEmpty())
            st->op->complete();
        else
            st->op->fail(QStringLiteral("all %1 record(s) failed")
                             .arg(st->op->failedUids().size()));
        emit syncCompleted(st->collectionId);
    };

    if (!st->creates.isEmpty()) {
        const BackendRecord r = st->creates.takeFirst();
        m_client->rawRequest(
            QByteArrayLiteral("POST"), m_collectionPath, r.data,
            [this, st, r, settleOne](int status, const QByteArray &body,
                          bool networkError) {
                if (networkError || status < 200 || status >= 300) {
                    settleOne(false, r.id);
                } else {
                    // Create mints the server-side Graph id; bridge
                    // requested→stored so the engine's next diff joins the
                    // sides (O55 aliasing machinery).
                    const QJsonObject created =
                        QJsonDocument::fromJson(body).object();
                    const QString storedId =
                        created.value(QStringLiteral("id")).toString();
                    const QString finalId =
                        storedId.isEmpty() ? r.id : storedId;
                    settleOne(true, finalId);
                    st->op->addIdAlias(r.id, finalId);
                }
                applyStep(st);
            });
        return;
    }
    if (!st->updates.isEmpty()) {
        // O61(e): carriers do not survive re-creates on consumer
        // Outlook.com — updates MUST be PATCH-in-place.
        const BackendRecord r = st->updates.takeFirst();
        m_client->rawRequest(
            QByteArrayLiteral("PATCH"),
            m_collectionPath + QLatin1Char('/') + r.id, r.data,
            [this, st, r, settleOne](int status, const QByteArray &body,
                          bool networkError) {
                Q_UNUSED(body);
                settleOne(!(networkError || status < 200 || status >= 300),
                          r.id);
                applyStep(st);
            });
        return;
    }
    if (!st->deletes.isEmpty()) {
        const QString id = st->deletes.takeFirst();
        m_client->rawRequest(
            QByteArrayLiteral("DELETE"),
            m_collectionPath + QLatin1Char('/') + id, {},
            [this, st, id, settleOne](int status, const QByteArray &body,
                           bool networkError) {
                Q_UNUSED(body);
                // Graph DELETE answers 204 No Content.
                const bool ok = !networkError
                    && (status == 204 || (status >= 200 && status < 300));
                settleOne(ok, id);
                applyStep(st);
            });
        return;
    }
    finishIfSettled();
}

} // namespace Kalburator::Sync
