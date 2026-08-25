#include "googlecalendarbackend.h"

#include "googleapiclient.h"
#include "googleauth.h"
#include "googlecanonstages.h"
#include "icalcanonstages.h"
#include "icalcodec.h"
#include "blockinghttp.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>
#include <algorithm>
#include <memory>

using Kalburator::Google::GoogleApiClient;
using Kalburator::Google::GoogleError;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::Shape;

namespace {

constexpr int kMaxResults = 250;

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

QString describeError(const GoogleError &err)
{
    return err.networkError
        ? QStringLiteral("network error: %1").arg(err.message)
        : QStringLiteral("google error %1 (%2): %3")
              .arg(err.httpStatus).arg(err.reason, err.message);
}

} // namespace

namespace Kalburator::Sync {

GoogleCalendarBackend::GoogleCalendarBackend(QObject *parent)
    : SyncBackend(parent)
    , m_client(new GoogleApiClient(this))
{
    m_client->setBaseUrl(QStringLiteral("https://www.googleapis.com/calendar/v3"));
}

GoogleCalendarBackend::~GoogleCalendarBackend() = default;

void GoogleCalendarBackend::setBaseUrl(const QString &baseUrl)
{
    m_client->setBaseUrl(baseUrl);
}

void GoogleCalendarBackend::setAccessToken(const QString &token)
{
    m_client->setAccessToken(token);
}

void GoogleCalendarBackend::setCacheDir(const QString &dir)
{
    m_cacheDir = dir;
}

QString GoogleCalendarBackend::backendType() const
{
    return QStringLiteral("google");
}

QList<Kalburator::Shape::Shape> GoogleCalendarBackend::nativeShapes() const
{
    return { Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("calendar")},
        Kalburator::Shape::EncodingId{QStringLiteral("google-event")} } };
}

// ---------------------------------------------------------------------------
// Persistence (sync tokens + merged record caches)
// ---------------------------------------------------------------------------

QString GoogleCalendarBackend::eventsPathForCalendar(
    const QString &calendarId) const
{
    return QStringLiteral("/calendars/%1/events")
        .arg(Kalburator::Net::urlEncodePathSegment(calendarId));
}

void GoogleCalendarBackend::ensurePersistedStateLoaded()
{
    if (m_persistenceLoaded || m_cacheDir.isEmpty())
        return;
    m_persistenceLoaded = true;

    QFile f(m_cacheDir + QStringLiteral("/google-sync-state.json"));
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
        m_syncTokens.insert(cit.key(),
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

void GoogleCalendarBackend::persistState() const
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
                                 m_syncTokens.value(it.key()) },
                               { QStringLiteral("records"), records } });
    }

    // Atomic replace: temp file in the same directory, then rename.
    QTemporaryFile tmp(m_cacheDir + QStringLiteral("/google-sync-XXXXXX"));
    if (!tmp.open())
        return;
    tmp.write(QJsonDocument(QJsonObject{
                                  { QStringLiteral("v"), 1 },
                                  { QStringLiteral("collections"),
                                    collections } })
                  .toJson(QJsonDocument::Compact));
    tmp.flush();
    const QString target =
        m_cacheDir + QStringLiteral("/google-sync-state.json");
    tmp.close();
    QFile::remove(target);
    QDir().rename(tmp.fileName(), target);   // same-dir rename ⇒ atomic-ish
}

// ---------------------------------------------------------------------------
// Read path
// ---------------------------------------------------------------------------

/// Heap-owned sync-walk state (O62): the client aggregates all pages
/// internally, so one callback completes the walk — but the callback still
/// outlives this frame.
struct GoogleCalendarBackend::FetchState {
    FetchOperation *op = nullptr;
    QString calendarId;
    QString path;
    QString token;
    QHash<QString, BackendRecord> cache;
    bool resynced = false;
};

FetchOperation *GoogleCalendarBackend::fetchItems(const QString &calendarId)
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
        st->path = eventsPathForCalendar(calendarId);
        st->token = m_syncTokens.value(calendarId);
        st->cache = m_cache.value(calendarId);
        startSyncFetch(st);
    });
    return op;
}

void GoogleCalendarBackend::startSyncFetch(std::shared_ptr<FetchState> st)
{
    auto *op = st->op;
    // Sync tokens are query-sensitive: the incremental listing must repeat
    // the EXACT template the initial listing used. maxResults is our only
    // param and it is constant — keep them consistent by construction.
    QString path = st->path
        + QStringLiteral("?maxResults=%1").arg(kMaxResults);
    if (!st->token.isEmpty())
        path += QStringLiteral("&syncToken=") + st->token;

    m_client->fetchCollection(path,
        [this, st](std::optional<QJsonArray> items,
                   const QString &nextSyncToken,
                   const GoogleError &err) {
            auto *op = st->op;
            if (op->state() == SyncOperation::Cancelled)
                return;

            // O42-style self-healing: an expired/invalidated sync token
            // (HTTP 410 Gone) gets ONE fresh initial full listing.
            if (err.isGone() && !st->resynced) {
                st->resynced = true;
                st->token.clear();
                st->cache.clear();
                startSyncFetch(st);
                return;
            }
            if (!items.has_value()) {
                const QString msg = describeError(err);
                op->fail(msg);
                emit fetchFinished(st->calendarId, false, msg);
                return;
            }

            for (const auto &iv : *items) {
                const QJsonObject ev = iv.toObject();
                const QString id =
                    ev.value(QStringLiteral("id")).toString();
                // Google marks deletions with status "cancelled" on
                // incremental listings (no @removed annotation).
                if (ev.value(QStringLiteral("status")).toString()
                    == QLatin1String("cancelled")) {
                    st->cache.remove(id);
                    continue;
                }
                BackendRecord r;
                r.id = id;
                r.type = QStringLiteral("event");
                r.displayName =
                    ev.value(QStringLiteral("summary")).toString();
                r.data = compactWire(ev);
                r.contentHash = sha256Hex(r.data);
                r.lastModified = QDateTime::fromString(
                    ev.value(QStringLiteral("updated")).toString(),
                    Qt::ISODate);
                st->cache.insert(id, r);
            }

            // The client walked every pageToken before calling us; the
            // fixpoint IS this callback. Commit + report the full set.
            st->token = nextSyncToken;
            finishFetch(st);
        });
}

void GoogleCalendarBackend::finishFetch(std::shared_ptr<FetchState> st)
{
    // Persist the merged view + resume token and report the FULL collection.
    m_syncTokens[st->calendarId] = st->token;
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
    st->op->setFetchedItems(incidences);
    m_lastFetchRecords[st->calendarId] = records;
    st->op->complete();
    emit fetchFinished(st->calendarId, true);
    emit syncCompleted(st->calendarId);
}

bool GoogleCalendarBackend::recordsFromLastFetch(
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

QList<BackendRecord> GoogleCalendarBackend::loadRecords(
    const QString &collectionId)
{
    return m_lastFetchRecords.value(collectionId);
}

QList<KCalendarCore::Incidence::Ptr>
GoogleCalendarBackend::incidencesForRecord(const QByteArray &wireJson) const
{
    // Legacy-surface conversion: google-event → canon → iCal → Incidence.
    // The engine's unified path never needs this (it consumes records and
    // promotes via the registered edge); this serves FetchOperation's
    // Incidence-typed contract only.
    const Calendar::GoogleEventToCanonStage promote;
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

void GoogleCalendarBackend::loadCalendars(const QString &collectionId)
{
    m_client->fetchCollection(
        QStringLiteral("/users/me/calendarList"),
        [this, collectionId](std::optional<QJsonArray> items,
                             const QString &,
                             const GoogleError &err) {
            if (!items.has_value()) {
                emit loadCalendarsFinished(collectionId, false,
                                           describeError(err));
                return;
            }
            for (const auto &cv : *items) {
                const QJsonObject cal = cv.toObject();
                CalMeta meta;
                meta.name = cal.value(QStringLiteral("summary")).toString();
                meta.colorHex =
                    cal.value(QStringLiteral("backgroundColor")).toString();
                // accessRole vocabulary: owner/writer are writable;
                // reader/freeBusyReader are not.
                const QString role =
                    cal.value(QStringLiteral("accessRole")).toString();
                meta.canEdit = role == QLatin1String("owner")
                    || role == QLatin1String("writer");
                meta.isDefault =
                    cal.value(QStringLiteral("primary")).toBool(false);
                const QString id = cal.value(QStringLiteral("id")).toString();
                if (id.isEmpty())
                    continue;
                m_calendars.insert(id, meta);
                emit calendarDiscovered(collectionId, id);
            }
            emit loadCalendarsFinished(collectionId, true);
        });
}

QList<Kalburator::Sync::CollectionInfo>
GoogleCalendarBackend::availableCollections()
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

Kalburator::Sync::DiscoveredCalendar GoogleCalendarBackend::discoveredCalendar(
    const QString &calendarId) const
{
    Kalburator::Sync::DiscoveredCalendar d;
    d.calendarId = calendarId;
    d.backendType = QStringLiteral("google");
    d.backendId = resourceId();
    // Google calendars hold events; tasks live in tasks/v1.
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

QByteArray GoogleCalendarBackend::stripReadOnlyFields(const QByteArray &wireJson)
{
    // O67(b)(1): Google events.insert REJECTS read-only created/updated
    // (400 Bad Request) even though the canon→google-event demote emits
    // them from canon created/lastModified. Strip at the transport seam.
    QJsonObject obj = QJsonDocument::fromJson(wireJson).object();
    obj.remove(QStringLiteral("created"));
    obj.remove(QStringLiteral("updated"));
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

/// Heap-owned sequential-apply state (O62).
struct GoogleCalendarBackend::ApplyState {
    WriteOperation *op = nullptr;
    QString collectionId;
    QString path;
    QList<BackendRecord> creates;
    QList<BackendRecord> updates;
    QStringList deletes;
    int total = 0;
};

WriteOperation *GoogleCalendarBackend::applyRecords(
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
        st->path = eventsPathForCalendar(collectionId);
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

void GoogleCalendarBackend::applyStep(std::shared_ptr<ApplyState> st)
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
            QByteArrayLiteral("POST"), st->path,
            stripReadOnlyFields(r.data),
            [this, st, r, settleOne](int status, const QByteArray &body,
                          bool networkError) {
                if (networkError || status < 200 || status >= 300) {
                    settleOne(false, r.id);
                } else {
                    // Google honors the client iCalUID anchor (uid survives),
                    // but mints a fresh TRANSPORT id which updates/deletes
                    // must address → bridge requested→stored (O55 machinery).
                    const QJsonObject created =
                        QJsonDocument::fromJson(body).object();
                    const QString storedId =
                        created.value(QStringLiteral("id")).toString();
                    const QString finalId =
                        storedId.isEmpty() ? r.id : storedId;
                    settleOne(true, finalId);
                    if (finalId != r.id)
                        st->op->addIdAlias(r.id, finalId);
                }
                applyStep(st);
            });
        return;
    }
    if (!st->updates.isEmpty()) {
        // PATCH in place — keeps iCalUID continuity; re-creates would fork
        // the series identity (same structural rule as O61(e) on Graph).
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
                // Idempotent delete semantics: 200/204 fine; 410 Gone means
                // already deleted — success (unlike the strict Graph pin).
                const bool ok = !networkError
                    && (status == 410 || (status >= 200 && status < 300));
                settleOne(ok, id);
                applyStep(st);
            });
        return;
    }
    finishIfSettled();
}

} // namespace Kalburator::Sync
