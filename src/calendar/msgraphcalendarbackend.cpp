#include "msgraphcalendarbackend.h"

#include "graphapiclient.h"
#include "mseventcanonstages.h"
#include "icalcanonstages.h"
#include "icalcodec.h"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonParseError>
#include <QTemporaryFile>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
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

void MSGraphCalendarBackend::setCacheDir(const QString &dir)
{
    m_cacheDir = dir;
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
// Persistence (delta tokens + merged record caches)
// ---------------------------------------------------------------------------

QString MSGraphCalendarBackend::pathForCalendar(
    const QString &calendarId) const
{
    const auto it = m_calendarPaths.constFind(calendarId);
    return it == m_calendarPaths.constEnd() ? m_collectionPath : it.value();
}

void MSGraphCalendarBackend::ensurePersistedStateLoaded()
{
    if (m_persistenceLoaded || m_cacheDir.isEmpty())
        return;
    m_persistenceLoaded = true;

    QFile f(m_cacheDir + QStringLiteral("/msgraph-delta-state.json"));
    if (!f.open(QIODevice::ReadOnly))
        return;   // no state yet: first run
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return;
    const QJsonObject collections =
        doc.object().value(QStringLiteral("collections")).toObject();
    for (auto cit = collections.constBegin(); cit != collections.constEnd();
         ++cit) {
        const QJsonObject c = cit.value().toObject();
        m_deltaTokens.insert(cit.key(),
                             c.value(QStringLiteral("token")).toString());
        QHash<QString, BackendRecord> cache;
        for (const auto &rv : c.value(QStringLiteral("records")).toArray()) {
            const QJsonObject ro = rv.toObject();
            BackendRecord r;
            r.id = ro.value(QStringLiteral("id")).toString();
            r.type = QStringLiteral("event");
            r.displayName = ro.value(QStringLiteral("displayName")).toString();
            r.data = QJsonDocument(
                         ro.value(QStringLiteral("wire")).toObject())
                         .toJson(QJsonDocument::Compact);
            r.contentHash = sha256Hex(r.data);
            r.lastModified = QDateTime::fromString(
                ro.value(QStringLiteral("lastModified")).toString(),
                Qt::ISODate);
            cache.insert(r.id, r);
        }
        m_cache.insert(cit.key(), cache);
    }
}

void MSGraphCalendarBackend::persistState() const
{
    if (m_cacheDir.isEmpty())
        return;
    QDir().mkpath(m_cacheDir);

    QJsonObject collections;
    for (auto it = m_cache.constBegin(); it != m_cache.constEnd(); ++it) {
        QJsonArray records;
        for (auto rit = it.value().constBegin();
             rit != it.value().constEnd(); ++rit) {
            const BackendRecord &r = rit.value();
            QJsonObject ro;
            ro.insert(QStringLiteral("id"), r.id);
            ro.insert(QStringLiteral("displayName"), r.displayName);
            ro.insert(QStringLiteral("lastModified"),
                      r.lastModified.toString(Qt::ISODate));
            ro.insert(QStringLiteral("wire"),
                      QJsonDocument::fromJson(r.data).object());
            records.append(ro);
        }
        collections.insert(it.key(),
                           QJsonObject{
                               { QStringLiteral("token"),
                                 m_deltaTokens.value(it.key()) },
                               { QStringLiteral("records"), records } });
    }

    // Atomic replace: temp file in the same directory, then rename.
    QTemporaryFile tmp(m_cacheDir + QStringLiteral("/msgraph-delta-XXXXXX"));
    if (!tmp.open())
        return;
    tmp.write(QJsonDocument(QJsonObject{
                                  { QStringLiteral("v"), 1 },
                                  { QStringLiteral("collections"),
                                    collections } })
                  .toJson(QJsonDocument::Compact));
    tmp.flush();
    const QString target =
        m_cacheDir + QStringLiteral("/msgraph-delta-state.json");
    tmp.close();
    QFile::remove(target);
    QDir().rename(tmp.fileName(), target);   // same-dir rename ⇒ atomic-ish
}

// ---------------------------------------------------------------------------
// Read path
// ---------------------------------------------------------------------------

/// Heap-owned delta-walk state (see header): all mutable walk state lives
/// here so async continuations never touch dead frames (O62).
struct MSGraphCalendarBackend::FetchState {
    FetchOperation *op = nullptr;
    QString calendarId;
    QString path;          // resolved once; stable across the whole walk
    QString token;
    QHash<QString, BackendRecord> cache;
    bool resynced = false;
};

FetchOperation *MSGraphCalendarBackend::fetchItems(const QString &calendarId)
{
    auto *op = new FetchOperation(calendarId, this);

    enqueueOperation(calendarId, op, [this, op, calendarId]() {
        if (op->state() == SyncOperation::Cancelled)
            return;
        op->setState(SyncOperation::Running);
        emit fetchStarted(calendarId, 0);

        ensurePersistedStateLoaded();
        auto st = std::make_shared<FetchState>();
        st->op = op;
        st->calendarId = calendarId;
        st->path = pathForCalendar(calendarId);
        // Resume from the stored token (empty ⇒ initial full walk) and the
        // merged cache; the walk upserts changes and reports everything.
        st->token = m_deltaTokens.value(calendarId);
        st->cache = m_cache.value(calendarId);
        startDeltaFetch(st);
    });
    return op;
}

void MSGraphCalendarBackend::startDeltaFetch(std::shared_ptr<FetchState> st)
{
    auto *op = st->op;
    m_client->deltaStep(
        st->path, st->token,
        [this, st](const GraphApiClient::DeltaPage &page,
                   const GraphError &err) {
            auto *op = st->op;
            if (op->state() == SyncOperation::Cancelled)
                return;

            // O42-style self-healing: an expired/unknown token gets ONE
            // fresh initial walk (which re-seeds the whole cache).
            if (err.isResyncRequired() && !st->resynced) {
                st->resynced = true;
                st->token.clear();
                st->cache.clear();
                startDeltaFetch(st);
                return;
            }
            if (!err.ok()) {
                const QString msg = err.networkError
                    ? QStringLiteral("network error: %1").arg(err.message)
                    : QStringLiteral("graph error %1 (%2): %3")
                          .arg(err.httpStatus).arg(err.code, err.message);
                op->fail(msg);
                emit fetchFinished(st->calendarId, false, msg);
                return;
            }

            for (const auto &iv : page.items) {
                const QJsonObject ev = iv.toObject();
                const QString id =
                    ev.value(QStringLiteral("id")).toString();
                // Real Graph marks deletions with an @removed annotation.
                if (ev.contains(QStringLiteral("@removed"))) {
                    st->cache.remove(id);
                    continue;
                }

                // O69: consumer delta pages can deliver SKELETON projections
                // ({id,start,end,type,etag} only). When a richer cached copy
                // exists, union-merge the skeleton OVER it instead of
                // replacing — otherwise an incremental walk would clobber
                // full records. Tombstones handled above; declared
                // limitation: subject-only edits/field deletions are not
                // observable through a skeleton page.
                QJsonObject effective = ev;
                const auto existing = st->cache.constFind(id);
                if (!ev.contains(QStringLiteral("createdDateTime"))
                    && existing != st->cache.constEnd()) {
                    const QJsonObject cached =
                        QJsonDocument::fromJson(existing->data).object();
                    for (auto it = ev.constBegin(); it != ev.constEnd(); ++it)
                        effective.insert(it.key(), it.value());
                    for (auto it = cached.constBegin(); it != cached.constEnd(); ++it)
                        if (!effective.contains(it.key()))
                            effective.insert(it.key(), it.value());
                }

                BackendRecord r;
                r.id = id;
                r.type = QStringLiteral("event");
                r.displayName =
                    effective.value(QStringLiteral("subject")).toString();
                r.data = compactWire(effective);
                r.contentHash = sha256Hex(r.data);
                r.lastModified = QDateTime::fromString(
                    effective.value(QStringLiteral("lastModifiedDateTime"))
                        .toString(),
                    Qt::ISODate);
                st->cache.insert(id, r);
            }

            if (!page.complete) {
                // Non-empty change page answered nextLink — keep stepping to
                // the fixpoint (empty set + deltaLink), per the wire nuance
                // pinned by tst_graph_api_client.
                st->token = page.deltaToken;
                startDeltaFetch(st);
                return;
            }

            // Fixpoint reached: persist the merged view + resume token and
            // report the FULL collection (engine diffs expect whole views).
            m_deltaTokens[st->calendarId] = page.deltaToken;
            m_cache[st->calendarId] = st->cache;
            persistState();

            QList<BackendRecord> records;
            for (const auto &r : st->cache)
                records.append(r);
            std::sort(records.begin(), records.end(),
                      [](const BackendRecord &a, const BackendRecord &b) {
                          return a.id < b.id;
                      });

            QList<KCalendarCore::Incidence::Ptr> incidences;
            for (const auto &r : records) {
                for (const auto &inc : incidencesForRecord(r.data))
                    incidences.append(inc);
            }

            emit itemsFetched(st->calendarId, incidences);
            op->setFetchedItems(incidences);
            m_lastFetchRecords[st->calendarId] = records;
            op->complete();
            emit fetchFinished(st->calendarId, true);
            emit syncCompleted(st->calendarId);
        });
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
// Discovery
// ---------------------------------------------------------------------------

void MSGraphCalendarBackend::loadCalendars(const QString &collectionId)
{
    m_client->fetchCollection(
        QStringLiteral("/me/calendars"),
        [this, collectionId](std::optional<QJsonArray> items,
                             const GraphError &err) {
            if (!items.has_value()) {
                const QString msg = err.networkError
                    ? QStringLiteral("network error: %1").arg(err.message)
                    : QStringLiteral("graph error %1 (%2): %3")
                          .arg(err.httpStatus).arg(err.code, err.message);
                emit loadCalendarsFinished(collectionId, false, msg);
                return;
            }
            for (const auto &cv : *items) {
                const QJsonObject cal = cv.toObject();
                CalMeta meta;
                meta.name = cal.value(QStringLiteral("name")).toString();
                meta.colorHex =
                    cal.value(QStringLiteral("color")).toString();
                meta.canEdit = cal.value(QStringLiteral("canEdit")).toBool(true);
                meta.isDefault =
                    cal.value(QStringLiteral("isDefaultCalendar")).toBool(false);
                const QString id = cal.value(QStringLiteral("id")).toString();
                if (id.isEmpty())
                    continue;
                m_calendars.insert(id, meta);
                m_calendarPaths.insert(
                    id, QStringLiteral("/me/calendars/%1/events").arg(id));
                emit calendarDiscovered(collectionId, id);
            }
            emit loadCalendarsFinished(collectionId, true);
        });
}

QList<Kalburator::Sync::CollectionInfo>
MSGraphCalendarBackend::availableCollections()
{
    QList<Kalburator::Sync::CollectionInfo> out;
    for (auto it = m_calendars.constBegin(); it != m_calendars.constEnd(); ++it) {
        Kalburator::Sync::CollectionInfo info;
        info.id = it.key();
        info.name = it.value().name;
        info.type = QStringLiteral("calendar");
        info.isDefault = it.value().isDefault;
        info.readOnly = !it.value().canEdit;
        info.contentTypes = { QStringLiteral("VEVENT") };
        out.append(info);
    }
    return out;
}

Kalburator::Sync::DiscoveredCalendar MSGraphCalendarBackend::discoveredCalendar(
    const QString &calendarId) const
{
    Kalburator::Sync::DiscoveredCalendar d;
    d.calendarId = calendarId;
    d.backendType = QStringLiteral("msgraph");
    d.backendId = resourceId();
    // Graph calendars hold events; tasks live in /me/todo/lists.
    d.supportsVEvent = true;
    d.supportsVTodo = false;
    d.supportsVJournal = false;

    const auto it = m_calendars.constFind(calendarId);
    if (it != m_calendars.constEnd()) {
        d.name = it.value().name;
        d.writable = it.value().canEdit;
        d.color = QColor(it.value().colorHex);
        if (!d.color.isValid())
            d.color = QColor();
    } else {
        d.writable = discoveredWritable(calendarId);
    }
    return d;
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
    QString path;
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
        st->path = pathForCalendar(collectionId);
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
            QByteArrayLiteral("POST"), st->path, r.data,
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
            st->path + QLatin1Char('/') + r.id, r.data,
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
            st->path + QLatin1Char('/') + id, {},
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
