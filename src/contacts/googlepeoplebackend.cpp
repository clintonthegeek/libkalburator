#include "googlepeoplebackend.h"

#include "googleapiclient.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>
#include <QUrl>
#include <algorithm>
#include <memory>

using Kalburator::Google::GoogleApiClient;
using Kalburator::Google::GoogleError;

namespace {

// Every top-level Person field the promote stage
// (GooglePersonToCanonStage) reads — the single shared personFields
// projection for ALL listing walks. Changing this constant changes the
// query template and therefore forces a full resync (persistence pin).
constexpr auto kPersonFields =
    "names,nicknames,emailAddresses,phoneNumbers,addresses,urls,"
    "relations,externalIds,memberships,imClients,calendarUrls,interests,"
    "skills,occupations,locales,sipAddresses,birthdays,genders,"
    "biographies,photos,organizations,fileAses,clientData,metadata";

constexpr int kPageSize = 200;

QString connectionsQueryTemplate()
{
    return QStringLiteral(
               "/v1/people/me/connections?pageSize=%1&personFields=%2"
               "&requestSyncToken=true")
        .arg(kPageSize)
        .arg(QLatin1String(kPersonFields));
}

QByteArray compactWire(const QJsonObject &person)
{
    return QJsonDocument(person).toJson(QJsonDocument::Compact);
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

/// PATCH/DELETE address the resource WITHOUT the "people/" collection
/// prefix (the colon-verb path form embeds the bare resource id).
QString resourceNameSuffix(const QString &resourceName)
{
    const QString prefix = QStringLiteral("people/");
    return resourceName.startsWith(prefix)
        ? resourceName.mid(prefix.size())
        : resourceName;
}

QString percentEncode(const QString &value)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(value));
}

QDateTime personUpdateTime(const QJsonObject &person)
{
    const QJsonArray sources = person.value(QStringLiteral("metadata"))
                                   .toObject()
                                   .value(QStringLiteral("sources"))
                                   .toArray();
    for (const auto &sv : sources) {
        const QString t =
            sv.toObject().value(QStringLiteral("updateTime")).toString();
        if (!t.isEmpty())
            return QDateTime::fromString(t, Qt::ISODate);
    }
    return {};
}

} // namespace

namespace Kalburator::Sync {

GooglePeopleBackend::GooglePeopleBackend(QObject *parent)
    : SyncBackendBase(parent)
    , m_client(new GoogleApiClient(this))
{
    // Paths authored by this backend carry the /v1 prefix verbatim
    // (client joins base+path), so the default base is version-less.
    m_client->setBaseUrl(QStringLiteral("https://people.googleapis.com"));
}

GooglePeopleBackend::~GooglePeopleBackend() = default;

void GooglePeopleBackend::setBaseUrl(const QString &baseUrl)
{
    m_client->setBaseUrl(baseUrl);
}

void GooglePeopleBackend::setAccessToken(const QString &token)
{
    m_client->setAccessToken(token);
}

void GooglePeopleBackend::setCacheDir(const QString &dir)
{
    m_cacheDir = dir;
}

QString GooglePeopleBackend::defaultCollectionId()
{
    return QStringLiteral("connections");
}

QString GooglePeopleBackend::backendType() const
{
    return QStringLiteral("google-people");
}

QList<Kalburator::Shape::Shape> GooglePeopleBackend::nativeShapes() const
{
    return { Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("contacts")},
        Kalburator::Shape::EncodingId{QStringLiteral("google-person")} } };
}

QList<Kalburator::Sync::CollectionInfo>
GooglePeopleBackend::availableCollections()
{
    Kalburator::Sync::CollectionInfo info;
    info.id = defaultCollectionId();
    info.name = QStringLiteral("Connections");
    info.type = QStringLiteral("contacts");
    info.isDefault = true;
    info.readOnly = false;
    info.contentTypes = { QStringLiteral("VCARD") };
    return { info };
}

// ---------------------------------------------------------------------------
// Persistence (merged record cache + sync token, keyed to the query template)
// ---------------------------------------------------------------------------

void GooglePeopleBackend::ensurePersistedStateLoaded()
{
    if (m_persistenceLoaded || m_cacheDir.isEmpty())
        return;
    m_persistenceLoaded = true;

    QFile f(m_cacheDir + QStringLiteral("/google-people-state.json"));
    if (!f.open(QIODevice::ReadOnly))
        return;   // no state yet: first run
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return;
    // Template pin: a param change ⇒ documented full resync — persisted
    // caches/tokens from another template are discarded.
    const QString storedTemplate =
        doc.object().value(QStringLiteral("template")).toString();
    if (storedTemplate
        != QString::fromUtf8(
            sha256Hex(connectionsQueryTemplate().toUtf8())))
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
            r.type = QStringLiteral("contact");
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

void GooglePeopleBackend::persistState() const
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
    QTemporaryFile tmp(m_cacheDir + QStringLiteral("/google-people-XXXXXX"));
    if (!tmp.open())
        return;
    tmp.write(QJsonDocument(QJsonObject{
                                  { QStringLiteral("v"), 1 },
                                  { QStringLiteral("template"),
                                    QString::fromUtf8(sha256Hex(
                                        connectionsQueryTemplate()
                                            .toUtf8())) },
                                  { QStringLiteral("collections"),
                                    collections } })
                  .toJson(QJsonDocument::Compact));
    tmp.flush();
    const QString target =
        m_cacheDir + QStringLiteral("/google-people-state.json");
    tmp.close();
    QFile::remove(target);
    QDir().rename(tmp.fileName(), target);   // same-dir rename ⇒ atomic-ish
}

// ---------------------------------------------------------------------------
// Read path — sync-token walks with 410 self-heal; full merged set per commit
// ---------------------------------------------------------------------------

struct GooglePeopleBackend::FetchState {
    SyncOperation *op = nullptr;
    QString collectionId;
    QString basePath;
    QString token;
    QString pageToken;
    bool incremental = false;
    bool resynced = false;
    QHash<QString, BackendRecord> prevCache;
    QHash<QString, BackendRecord> merged;
    QString nextSyncToken;
};

SyncOperation *GooglePeopleBackend::fetchItems(const QString &collectionId)
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
        st->basePath = connectionsQueryTemplate();
        st->token = m_syncTokens.value(collectionId);
        st->incremental = !st->token.isEmpty();
        st->prevCache = m_cache.value(collectionId);
        if (st->incremental)
            st->merged = st->prevCache;
        startConnectionsWalk(st);
    });
    return op;
}

void GooglePeopleBackend::startConnectionsWalk(
    std::shared_ptr<FetchState> st)
{
    QString path = st->basePath;
    if (!st->token.isEmpty())
        path += QStringLiteral("&sync_token=") + percentEncode(st->token);
    if (!st->pageToken.isEmpty())
        path += QStringLiteral("&pageToken=") + percentEncode(st->pageToken);

    m_client->rawRequest(
        QByteArrayLiteral("GET"), path, {},
        [this, st](int status, const QByteArray &body, bool networkError) {
            auto *op = st->op;
            if (op->state() == SyncOperation::Cancelled)
                return;

            // O42-style self-healing: an expired/invalidated sync token
            // (HTTP 410 Gone) gets ONE fresh initial full walk. The fresh
            // listing is authoritative for membership; per-record
            // enrichment still draws on the prior cache below.
            if (status == 410 && !networkError && !st->resynced) {
                st->resynced = true;
                st->token.clear();
                st->incremental = false;
                st->merged.clear();
                st->pageToken.clear();
                startConnectionsWalk(st);
                return;
            }
            if (networkError || status < 200 || status >= 300) {
                GoogleError err;
                err.httpStatus = status;
                err.networkError = networkError;
                if (!networkError) {
                    const QJsonObject e =
                        QJsonDocument::fromJson(body).object()
                            .value(QStringLiteral("error")).toObject();
                    err.message = e.value(QStringLiteral("message")).toString();
                    err.reason = e.value(QStringLiteral("errors"))
                                     .toArray().at(0).toObject()
                                     .value(QStringLiteral("reason"))
                                     .toString();
                }
                const QString msg = errorMessageFor(err);
                op->fail(msg);
                emit fetchFinished(st->collectionId, false, msg);
                return;
            }

            const QJsonObject page =
                QJsonDocument::fromJson(body).object();
            for (const auto &pv :
                 page.value(QStringLiteral("connections")).toArray()) {
                const QJsonObject person = pv.toObject();
                const QString id = person.value(QStringLiteral("resourceName"))
                                       .toString();
                if (id.isEmpty())
                    continue;

                // Deletions surface as metadata.deleted on incremental
                // pages — tombstone from the merged view.
                if (person.value(QStringLiteral("metadata"))
                        .toObject()
                        .value(QStringLiteral("deleted"))
                        .toBool(false)) {
                    st->merged.remove(id);
                    continue;
                }

                // Defensive union-merge (O69 lesson): keys the incoming
                // projection lacks are filled from the cached copy instead
                // of clobbering it. Keys it DOES carry win wholesale.
                QJsonObject effective = person;
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
                r.type = QStringLiteral("contact");
                const QJsonArray names =
                    effective.value(QStringLiteral("names")).toArray();
                if (!names.isEmpty())
                    r.displayName = names.at(0).toObject()
                                        .value(QStringLiteral("displayName"))
                                        .toString();
                r.data = compactWire(effective);
                r.contentHash = sha256Hex(r.data);
                r.lastModified = personUpdateTime(effective);
                st->merged.insert(id, r);
            }

            const QString nextToken =
                page.value(QStringLiteral("nextPageToken")).toString();
            const QString syncToken =
                page.value(QStringLiteral("nextSyncToken")).toString();
            if (!syncToken.isEmpty())
                st->nextSyncToken = syncToken;
            if (!nextToken.isEmpty()) {
                st->pageToken = nextToken;
                startConnectionsWalk(st);
                return;
            }
            finishFetch(st);
        });
}

void GooglePeopleBackend::finishFetch(std::shared_ptr<FetchState> st)
{
    // Commit + report the FULL merged set (engine diffs expect whole views).
    m_syncTokens[st->collectionId] = st->nextSyncToken;
    m_cache[st->collectionId] = st->merged;
    persistState();

    QList<BackendRecord> records;
    for (const auto &r : st->merged)
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

bool GooglePeopleBackend::recordsFromLastFetch(
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

QList<BackendRecord> GooglePeopleBackend::loadRecords(
    const QString &collectionId)
{
    return m_lastFetchRecords.value(collectionId);
}

// ---------------------------------------------------------------------------
// Write path — inline clientData carriers, mask-derived PATCH, idempotent
// deletes
// ---------------------------------------------------------------------------

QByteArray GooglePeopleBackend::stripNonCreatableFields(
    const QByteArray &wireJson)
{
    // People has no pinned rejected read-only fields like Calendar's
    // created/updated (O67), but etag/metadata/nextSyncToken never belong
    // on an authored body — strip defensively. clientData carriers RIDE
    // INLINE (live-Reversible channel, O66 verdict table).
    QJsonObject obj = QJsonDocument::fromJson(wireJson).object();
    obj.remove(QStringLiteral("etag"));
    obj.remove(QStringLiteral("metadata"));
    obj.remove(QStringLiteral("nextSyncToken"));
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

struct GooglePeopleBackend::ApplyState {
    WriteOperation *op = nullptr;
    QString collectionId;
    QList<BackendRecord> creates;
    QList<BackendRecord> updates;
    QStringList deletes;
    int total = 0;
};

WriteOperation *GooglePeopleBackend::applyRecords(
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

void GooglePeopleBackend::applyStep(std::shared_ptr<ApplyState> st)
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
            QByteArrayLiteral("POST"),
            // O71 (live 2026-08-25): people.createContact is a
            // COLLECTION-level custom method — /v1/people:createContact.
            // The resource-level form /v1/people/me:createContact 404s at
            // Google's front-end (the P2.b mock pinned the wrong shape).
            QStringLiteral("/v1/people:createContact"),
            stripNonCreatableFields(r.data),
            [this, st, r, settleOne](int status, const QByteArray &body,
                          bool networkError) {
                if (networkError || status < 200 || status >= 300) {
                    settleOne(false, r.id);
                } else {
                    // Create mints the server-side resourceName; bridge
                    // requested→stored so the engine's next diff joins the
                    // sides (O55 aliasing machinery).
                    const QJsonObject created =
                        QJsonDocument::fromJson(body).object();
                    const QString storedId = created.value(
                        QStringLiteral("resourceName")).toString();
                    const QString finalId =
                        storedId.isEmpty() ? r.id : storedId;
                    if (finalId != r.id)
                        st->op->addIdAlias(r.id, finalId);
                    settleOne(true, finalId);
                }
                applyStep(st);
            });
        return;
    }

    if (!st->updates.isEmpty()) {
        // PATCH merge-in-place; updatePersonFields derives from the patch
        // body's own top-level keys minus metadata/etag (defensively also
        // resourceName/nextSyncToken — never valid masks).
        //
        // O72 (live 2026-08-25): :updateContact REQUIRES a concurrency
        // token — "Request must set person.etag or
        // person.metadata.sources.etag". The top-level etag therefore RIDES
        // the patch body (listings always deliver it, even unprojected) but
        // is excluded from the mask. metadata/nextSyncToken/resourceName
        // are still stripped.
        const BackendRecord r = st->updates.takeFirst();

        const QJsonObject fullBody =
            QJsonDocument::fromJson(r.data).object();
        QJsonObject body = fullBody;
        body.remove(QStringLiteral("metadata"));
        body.remove(QStringLiteral("nextSyncToken"));
        body.remove(QStringLiteral("resourceName"));

        QStringList mask;
        for (auto it = body.constBegin(); it != body.constEnd(); ++it) {
            if (it.key() == QLatin1String("etag"))
                continue;
            mask.append(it.key());
        }

        if (mask.isEmpty()) {
            settleOne(true, r.id);
            applyStep(st);
            return;
        }

        const QString path =
            QStringLiteral("/v1/people/%1:updateContact?updatePersonFields=%2")
                .arg(resourceNameSuffix(r.id),
                     percentEncode(mask.join(QLatin1Char(','))));
        m_client->rawRequest(
            QByteArrayLiteral("PATCH"), path, compactWire(body),
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
            QStringLiteral("/v1/people/%1:deleteContact")
                .arg(resourceNameSuffix(id)),
            {},
            [this, st, id, settleOne](int status, const QByteArray &body,
                           bool networkError) {
                Q_UNUSED(body);
                // Idempotent delete semantics: 200/204 fine; 404 means
                // already gone — success (People deletes are not flaky, so
                // no confirming re-list unlike the Graph pin).
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
