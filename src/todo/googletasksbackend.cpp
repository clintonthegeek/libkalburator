#include "googletasksbackend.h"

#include "googleapiclient.h"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>
#include <algorithm>
#include <memory>

using Kalburator::Google::GoogleApiClient;
using Kalburator::Google::GoogleError;

namespace {

constexpr int kMaxResults = 100;

QByteArray compactWire(const QJsonObject &task)
{
    return QJsonDocument(task).toJson(QJsonDocument::Compact);
}

QByteArray sha256Hex(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes,
                                    QCryptographicHash::Sha256).toHex();
}

QString errorMessageFor(const GoogleError &err)
{
    return err.networkError
        ? QStringLiteral("network error: %1").arg(err.message)
        : QStringLiteral("google error %1 (%2): %3")
              .arg(err.httpStatus).arg(err.reason, err.message);
}

} // namespace

namespace Kalburator::Sync {

GoogleTasksBackend::GoogleTasksBackend(QObject *parent)
    : SyncBackendBase(parent)
    , m_client(new GoogleApiClient(this))
{
    m_client->setBaseUrl(QStringLiteral("https://tasks.googleapis.com"));
}

GoogleTasksBackend::~GoogleTasksBackend() = default;

void GoogleTasksBackend::setBaseUrl(const QString &baseUrl)
{
    m_client->setBaseUrl(baseUrl);
}

void GoogleTasksBackend::setAccessToken(const QString &token)
{
    m_client->setAccessToken(token);
}

void GoogleTasksBackend::setCacheDir(const QString &dir)
{
    m_cacheDir = dir;
}

QString GoogleTasksBackend::backendType() const
{
    return QStringLiteral("google-tasks");
}

QList<Kalburator::Shape::Shape> GoogleTasksBackend::nativeShapes() const
{
    return { Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("todo")},
        Kalburator::Shape::EncodingId{QStringLiteral("google-task")} } };
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

QString GoogleTasksBackend::tasksPathFor(const QString &collectionId)
{
    return QStringLiteral("/v1/lists/%1/tasks").arg(collectionId);
}

// ---------------------------------------------------------------------------
// Persistence (merged record caches; no tokens — full-listing strategy)
// ---------------------------------------------------------------------------

void GoogleTasksBackend::ensurePersistedStateLoaded()
{
    if (m_persistenceLoaded || m_cacheDir.isEmpty())
        return;
    m_persistenceLoaded = true;

    QFile f(m_cacheDir + QStringLiteral("/google-tasks-state.json"));
    if (!f.open(QIODevice::ReadOnly))
        return;   // no state yet: first run
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return;
    const QJsonObject collections =
        doc.object().value(QStringLiteral("collections")).toObject();
    for (auto cit = collections.constBegin(); cit != collections.constEnd();
         ++cit) {
        QHash<QString, BackendRecord> cache;
        for (const auto &rv : cit.value().toObject()
                                  .value(QStringLiteral("records"))
                                  .toArray()) {
            const QJsonObject ro = rv.toObject();
            BackendRecord r;
            r.id = ro.value(QStringLiteral("id")).toString();
            r.type = QStringLiteral("todo");
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

void GoogleTasksBackend::persistState() const
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
                               { QStringLiteral("records"), records } });
    }

    // Atomic replace: temp file in the same directory, then rename.
    QTemporaryFile tmp(m_cacheDir + QStringLiteral("/google-tasks-XXXXXX"));
    if (!tmp.open())
        return;
    tmp.write(QJsonDocument(QJsonObject{
                                  { QStringLiteral("v"), 1 },
                                  { QStringLiteral("collections"),
                                    collections } })
                  .toJson(QJsonDocument::Compact));
    tmp.flush();
    const QString target =
        m_cacheDir + QStringLiteral("/google-tasks-state.json");
    tmp.close();
    QFile::remove(target);
    QDir().rename(tmp.fileName(), target);   // same-dir rename ⇒ atomic-ish
}

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

void GoogleTasksBackend::loadTaskLists(const QString &requestId)
{
    m_client->fetchCollection(
        QStringLiteral("/v1/users/me/lists"),
        [this, requestId](std::optional<QJsonArray> items,
                          const QString &,
                          const GoogleError &err) {
            if (!items.has_value()) {
                emit listsLoadFinished(requestId, false,
                                       errorMessageFor(err));
                return;
            }
            for (const auto &lv : *items) {
                const QJsonObject list = lv.toObject();
                const QString id =
                    list.value(QStringLiteral("id")).toString();
                if (id.isEmpty())
                    continue;
                ListMeta meta;
                meta.name = list.value(QStringLiteral("title")).toString();
                m_lists.insert(id, meta);
                emit listDiscovered(requestId, id);
            }
            emit listsLoadFinished(requestId, true);
        });
}

QList<Kalburator::Sync::CollectionInfo>
GoogleTasksBackend::availableCollections()
{
    QList<Kalburator::Sync::CollectionInfo> out;
    for (auto it = m_lists.constBegin(); it != m_lists.constEnd(); ++it) {
        Kalburator::Sync::CollectionInfo info;
        info.id = it.key();
        info.name = it.value().name;
        info.type = QStringLiteral("todo");
        info.isDefault = false;
        info.readOnly = false;   // v1: all discovered task lists writable
        info.contentTypes = { QStringLiteral("VTODO") };
        out.append(info);
    }
    return out;
}

Kalburator::Sync::DiscoveredCalendar GoogleTasksBackend::discoveredTaskList(
    const QString &collectionId) const
{
    Kalburator::Sync::DiscoveredCalendar d;
    d.calendarId = collectionId;
    d.backendType = backendType();
    d.backendId = resourceId();
    d.supportsVEvent = false;
    d.supportsVTodo = true;
    d.supportsVJournal = false;

    const auto it = m_lists.constFind(collectionId);
    if (it != m_lists.constEnd()) {
        d.name = it.value().name;
        d.writable = true;
    } else {
        d.writable = discoveredWritable(collectionId);
    }
    return d;
}

// ---------------------------------------------------------------------------
// Read path — full paged listings every fetch; the Tasks API has NO sync
// tokens, so there is no incremental machinery to expire or persist
// ---------------------------------------------------------------------------

/// Heap-owned listing-walk state (O62). `prevCache` is the prior merged view
/// (in-memory or persisted resume); `fresh` is rebuilt from the
/// authoritative full listing each walk.
struct GoogleTasksBackend::FetchState {
    SyncOperation *op = nullptr;
    QString collectionId;
    QString path;
    QHash<QString, BackendRecord> prevCache;
    QHash<QString, BackendRecord> fresh;
};

SyncOperation *GoogleTasksBackend::fetchItems(const QString &collectionId)
{
    auto *op = new SyncOperation(collectionId, this);

    enqueueOperation(collectionId, op, [this, op, collectionId]() {
        if (op->state() == SyncOperation::Cancelled)
            return;
        op->setState(SyncOperation::Running);
        emit fetchStarted(collectionId, 0);

        ensurePersistedStateLoaded();
        auto st = std::make_shared<FetchState>();
        st->op = op;
        st->collectionId = collectionId;
        st->path = tasksPathFor(collectionId);
        st->prevCache = m_cache.value(collectionId);
        startListingFetch(st);
    });
    return op;
}

void GoogleTasksBackend::startListingFetch(std::shared_ptr<FetchState> st)
{
    // showCompleted + showHidden are MANDATORY: default listings omit
    // completed and deleted rows, and deleted rows must arrive so they are
    // tombstoned from the merged view rather than silently retained.
    const QString path = st->path
        + QStringLiteral("?showCompleted=true&showHidden=true&maxResults=%1")
              .arg(kMaxResults);

    m_client->fetchCollection(path,
        [this, st](std::optional<QJsonArray> items,
                   const QString &,
                   const GoogleError &err) {
            auto *op = st->op;
            if (op->state() == SyncOperation::Cancelled)
                return;

            if (!items.has_value()) {
                const QString msg = errorMessageFor(err);
                op->fail(msg);
                emit fetchFinished(st->collectionId, false, msg);
                return;
            }

            for (const auto &tv : *items) {
                const QJsonObject task = tv.toObject();
                const QString id =
                    task.value(QStringLiteral("id")).toString();
                if (id.isEmpty())
                    continue;

                // Tombstone: a deleted:true row is gone from the server's
                // authoritative set.
                if (task.value(QStringLiteral("deleted")).toBool()) {
                    st->fresh.remove(id);
                    continue;
                }

                // Defensive union-merge (O69 lesson): when a richer cached
                // copy exists, keys the incoming row lacks are filled from
                // the cache instead of being clobbered. Keys the row DOES
                // carry win wholesale.
                QJsonObject effective = task;
                const auto existing = st->prevCache.constFind(id);
                if (existing != st->prevCache.constEnd()) {
                    const QJsonObject cached =
                        QJsonDocument::fromJson(existing->data).object();
                    for (auto cit = cached.constBegin();
                         cit != cached.constEnd(); ++cit) {
                        if (!effective.contains(cit.key()))
                            effective.insert(cit.key(), cit.value());
                    }
                }

                BackendRecord r;
                r.id = id;
                r.type = QStringLiteral("todo");
                r.displayName =
                    effective.value(QStringLiteral("title")).toString();
                r.data = compactWire(effective);
                r.contentHash = sha256Hex(r.data);
                r.lastModified = QDateTime::fromString(
                    effective.value(QStringLiteral("updated")).toString(),
                    Qt::ISODate);
                st->fresh.insert(id, r);
            }

            finishFetch(st);
        });
}

void GoogleTasksBackend::finishFetch(std::shared_ptr<FetchState> st)
{
    // Full-set commit + report (engine diffs expect whole views). The
    // listing is authoritative: ids absent from it are gone.
    m_cache[st->collectionId] = st->fresh;
    persistState();

    QList<BackendRecord> records;
    for (const auto &r : st->fresh)
        records.append(r);
    std::sort(records.begin(), records.end(),
              [](const BackendRecord &a, const BackendRecord &b) {
                  return a.id < b.id;
              });

    m_lastFetchRecords[st->collectionId] = records;
    st->op->complete();
    emit fetchFinished(st->collectionId, true);
    emit syncCompleted(st->collectionId);
}

bool GoogleTasksBackend::recordsFromLastFetch(
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

QList<BackendRecord> GoogleTasksBackend::loadRecords(
    const QString &collectionId)
{
    return m_lastFetchRecords.value(collectionId);
}

// ---------------------------------------------------------------------------
// Write path — strip-at-create, PATCH-in-place, idempotent deletes
// ---------------------------------------------------------------------------

QByteArray GoogleTasksBackend::stripNonCreatableFields(
    const QByteArray &wireJson)
{
    // O68 family: tasks.insert REJECTS read-only created/updated AND a
    // client-supplied transport `id` — the server mints its own. Strip all
    // three at the POST seam. No carrier channel exists (O66(c)); nothing
    // else to route.
    QJsonObject obj = QJsonDocument::fromJson(wireJson).object();
    obj.remove(QStringLiteral("created"));
    obj.remove(QStringLiteral("updated"));
    obj.remove(QStringLiteral("id"));
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

/// Heap-owned sequential-apply state (O62).
struct GoogleTasksBackend::ApplyState {
    WriteOperation *op = nullptr;
    QString collectionId;
    QString path;
    QList<BackendRecord> creates;
    QList<BackendRecord> updates;
    QStringList deletes;
    int total = 0;
};

WriteOperation *GoogleTasksBackend::applyRecords(
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
        st->path = tasksPathFor(collectionId);
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

void GoogleTasksBackend::applyStep(std::shared_ptr<ApplyState> st)
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
        if (!st->op->succeededUids().isEmpty()
            || st->op->failedUids().isEmpty())
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
            stripNonCreatableFields(r.data),
            [this, st, r, settleOne](int status, const QByteArray &body,
                          bool networkError) {
                if (networkError || status < 200 || status >= 300) {
                    settleOne(false, r.id);
                } else {
                    // Tasks.insert mints the server-side transport id which
                    // updates/deletes must address → bridge requested→stored
                    // (O55 aliasing machinery).
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
        // PATCH in place under the existing transport id; partial bodies ride
        // verbatim (same convention as GoogleCalendarBackend updates).
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
                // Idempotent delete semantics: 200/204 fine; 404 means
                // already deleted — success.
                const bool ok = !networkError
                    && (status == 404 || (status >= 200 && status < 300));
                settleOne(ok, id);
                applyStep(st);
            });
        return;
    }

    finishIfSettled();
}

} // namespace Kalburator::Sync
