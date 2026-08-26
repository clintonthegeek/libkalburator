#include "graphcontactsbackend.h"

#include "graphapiclient.h"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTemporaryFile>
#include <QUrlQuery>
#include <algorithm>
#include <memory>

using Kalburator::Graph::GraphApiClient;
using Kalburator::Graph::GraphError;

namespace {

constexpr auto kCanonExtensionId =
    "Microsoft.OutlookServices.OpenTypeExtension.kalburator.canon";

QByteArray compactWire(const QJsonObject &contact)
{
    return QJsonDocument(contact).toJson(QJsonDocument::Compact);
}

QByteArray sha256Hex(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes,
                                    QCryptographicHash::Sha256).toHex();
}

QString errorMessageFor(const GraphError &err)
{
    return err.networkError
        ? QStringLiteral("network error: %1").arg(err.message)
        : QStringLiteral("graph error %1 (%2): %3")
              .arg(err.httpStatus).arg(err.code, err.message);
}

} // namespace

namespace Kalburator::Sync {

GraphContactsBackend::GraphContactsBackend(QObject *parent)
    : SyncBackendBase(parent)
    , m_client(new GraphApiClient(this))
{
}

GraphContactsBackend::~GraphContactsBackend() = default;

void GraphContactsBackend::setBaseUrl(const QString &baseUrl)
{
    m_client->setBaseUrl(baseUrl);
}

void GraphContactsBackend::setAccessToken(const QString &token)
{
    m_client->setAccessToken(token);
}

void GraphContactsBackend::setCacheDir(const QString &dir)
{
    m_cacheDir = dir;
}

QString GraphContactsBackend::defaultCollectionId()
{
    return QStringLiteral("default");
}

QString GraphContactsBackend::backendType() const
{
    return QStringLiteral("msgraph-contacts");
}

QList<Kalburator::Shape::Shape> GraphContactsBackend::nativeShapes() const
{
    return { Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("contacts")},
        Kalburator::Shape::EncodingId{QStringLiteral("ms-contact")} } };
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

QString GraphContactsBackend::collectionPathFor(
    const QString &collectionId) const
{
    if (collectionId.isEmpty() || collectionId == defaultCollectionId())
        return QStringLiteral("/v1.0/me/contacts");
    return QStringLiteral("/v1.0/me/contactFolders/%1/contacts")
        .arg(collectionId);
}

QString GraphContactsBackend::expandedListingPath(const QString &basePath)
{
    // O66-correction: filter on the RETURNED full-id prefix; the mock (and
    // real Graph) 500 on a wrong prefix. FullyEncoded keeps the query
    // wire-legal without ever touching record ids.
    QUrlQuery query;
    query.addQueryItem(
        QStringLiteral("$expand"),
        QStringLiteral("extensions($filter=Id eq '%1')")
            .arg(QLatin1String(kCanonExtensionId)));
    return basePath + QLatin1Char('?') + query.toString(QUrl::FullyEncoded);
}

// ---------------------------------------------------------------------------
// Persistence (merged record caches; no tokens — full-listing strategy)
// ---------------------------------------------------------------------------

void GraphContactsBackend::ensurePersistedStateLoaded()
{
    if (m_persistenceLoaded || m_cacheDir.isEmpty())
        return;
    m_persistenceLoaded = true;

    QFile f(m_cacheDir + QStringLiteral("/msgraph-contacts-state.json"));
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

void GraphContactsBackend::persistState() const
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
    QTemporaryFile tmp(m_cacheDir + QStringLiteral("/msgraph-contacts-XXXXXX"));
    if (!tmp.open())
        return;
    tmp.write(QJsonDocument(QJsonObject{
                                  { QStringLiteral("v"), 1 },
                                  { QStringLiteral("collections"),
                                    collections } })
                  .toJson(QJsonDocument::Compact));
    tmp.flush();
    const QString target =
        m_cacheDir + QStringLiteral("/msgraph-contacts-state.json");
    tmp.close();
    QFile::remove(target);
    QDir().rename(tmp.fileName(), target);   // same-dir rename ⇒ atomic-ish
}

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

void GraphContactsBackend::loadFolders(const QString &requestId)
{
    m_client->fetchCollection(
        QStringLiteral("/v1.0/me/contactFolders"),
        [this, requestId](std::optional<QJsonArray> items,
                          const GraphError &err) {
            if (!items.has_value()) {
                emit foldersLoadFinished(requestId, false,
                                         errorMessageFor(err));
                return;
            }
            for (const auto &fv : *items) {
                const QJsonObject folder = fv.toObject();
                const QString id =
                    folder.value(QStringLiteral("id")).toString();
                if (id.isEmpty())
                    continue;
                FolderMeta meta;
                meta.name =
                    folder.value(QStringLiteral("displayName")).toString();
                m_folders.insert(id, meta);
                emit folderDiscovered(requestId, id);
            }
            emit foldersLoadFinished(requestId, true);
        });
}

QList<Kalburator::Sync::CollectionInfo>
GraphContactsBackend::availableCollections()
{
    QList<Kalburator::Sync::CollectionInfo> out;
    for (auto it = m_folders.constBegin(); it != m_folders.constEnd(); ++it) {
        Kalburator::Sync::CollectionInfo info;
        info.id = it.key();
        info.name = it.value().name;
        info.type = QStringLiteral("contacts");
        info.isDefault = false;
        info.readOnly = false;   // v1: all discovered folders writable
        info.contentTypes = { QStringLiteral("VCARD") };
        out.append(info);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Read path — expanded FULL listings, never delta (O70)
// ---------------------------------------------------------------------------

/// Heap-owned listing-walk state: all mutable walk state lives here so
/// async continuations never touch dead frames (O62). `prevCache` is the
/// prior merged view (in-memory or persisted resume); `fresh` is rebuilt
/// from the authoritative full listing each walk.
struct GraphContactsBackend::FetchState {
    SyncOperation *op = nullptr;
    QString collectionId;
    QString path;
    QHash<QString, BackendRecord> prevCache;
    QHash<QString, BackendRecord> fresh;
};

SyncOperation *GraphContactsBackend::fetchItems(const QString &collectionId)
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
        st->path = collectionPathFor(collectionId);
        st->prevCache = m_cache.value(collectionId);
        startListingFetch(st);
    });
    return op;
}

void GraphContactsBackend::startListingFetch(std::shared_ptr<FetchState> st)
{
    m_client->fetchCollection(
        expandedListingPath(st->path),
        [this, st](std::optional<QJsonArray> items, const GraphError &err) {
            auto *op = st->op;
            if (op->state() == SyncOperation::Cancelled)
                return;

            if (!items.has_value()) {
                const QString msg = errorMessageFor(err);
                op->fail(msg);
                emit fetchFinished(st->collectionId, false, msg);
                return;
            }

            for (const auto &cv : *items) {
                const QJsonObject contact = cv.toObject();
                const QString id =
                    contact.value(QStringLiteral("id")).toString();
                if (id.isEmpty())
                    continue;

                // Defensive union-merge (O69 lesson): when a richer cached
                // copy exists, keys the incoming projection lacks are filled
                // from the cache instead of being clobbered. Keys the
                // incoming item DOES carry win wholesale (extensions[] too).
                QJsonObject effective = contact;
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
                r.displayName =
                    effective.value(QStringLiteral("displayName")).toString();
                r.data = compactWire(effective);
                r.contentHash = sha256Hex(r.data);
                r.lastModified = QDateTime::fromString(
                    effective.value(QStringLiteral("lastModifiedDateTime"))
                        .toString(),
                    Qt::ISODate);
                st->fresh.insert(id, r);
            }

            // Full-set commit + report (engine diffs expect whole views).
            // The listing is authoritative: ids absent from it are gone.
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
            op->complete();
            emit fetchFinished(st->collectionId, true);
            emit syncCompleted(st->collectionId);
        });
}

bool GraphContactsBackend::recordsFromLastFetch(
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

QList<BackendRecord> GraphContactsBackend::loadRecords(
    const QString &collectionId)
{
    return m_lastFetchRecords.value(collectionId);
}

// ---------------------------------------------------------------------------
// Write path — strip-then-nav-POST carriers, PATCH-in-place, idempotent
// deletes with a confirming re-list
// ---------------------------------------------------------------------------

struct GraphContactsBackend::ApplyState {
    WriteOperation *op = nullptr;
    QString collectionId;
    QString path;
    QList<BackendRecord> creates;
    QList<BackendRecord> updates;
    QStringList deletes;
    int total = 0;

    // Nav-carrier drain between the plain-field request and settling its
    // record: one POST per extension row to /me/contacts/{id}/extensions.
    struct CarrierDrain {
        bool active = false;
        bool failed = false;
        QString contactId;
        QString settleId;
        QJsonArray rows;
        int index = 0;
    };
    CarrierDrain carrier;
};

WriteOperation *GraphContactsBackend::applyRecords(
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
        st->path = collectionPathFor(collectionId);
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

void GraphContactsBackend::applyStep(std::shared_ptr<ApplyState> st)
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

        // Never inline-at-create: strip extensions[] from the POST body and
        // route every stripped row through the nav channel afterwards.
        QJsonObject body =
            QJsonDocument::fromJson(r.data).object();
        const QJsonArray carrierRows =
            body.take(QStringLiteral("extensions")).toArray();
        const QByteArray plainBody = compactWire(body);

        m_client->rawRequest(
            QByteArrayLiteral("POST"), st->path, plainBody,
            [this, st, r, carrierRows, settleOne](
                int status, const QByteArray &respBody, bool networkError) {
                if (networkError || status < 200 || status >= 300) {
                    settleOne(false, r.id);
                    applyStep(st);
                    return;
                }
                // Create mints the server-side Graph id; bridge
                // requested→stored so the engine's next diff joins the
                // sides (O55 aliasing machinery).
                const QJsonObject created =
                    QJsonDocument::fromJson(respBody).object();
                const QString storedId =
                    created.value(QStringLiteral("id")).toString();
                const QString finalId =
                    storedId.isEmpty() ? r.id : storedId;
                st->op->addIdAlias(r.id, finalId);
                if (!carrierRows.isEmpty()) {
                    st->carrier = ApplyState::CarrierDrain{};
                    st->carrier.active = true;
                    st->carrier.contactId = finalId;
                    st->carrier.settleId = finalId;
                    st->carrier.rows = carrierRows;
                    postNextCarrier(st);
                    return;
                }
                settleOne(true, finalId);
                applyStep(st);
            });
        return;
    }

    if (!st->updates.isEmpty()) {
        // PATCH-in-place with plain fields only (a PATCH-borne extensions[]
        // key is rejected server-side); carrier changes ride the nav channel.
        const BackendRecord r = st->updates.takeFirst();

        QJsonObject body =
            QJsonDocument::fromJson(r.data).object();
        const QJsonArray carrierRows =
            body.take(QStringLiteral("extensions")).toArray();
        const QByteArray plainBody = compactWire(body);

        m_client->rawRequest(
            QByteArrayLiteral("PATCH"), st->path + QLatin1Char('/') + r.id,
            plainBody,
            [this, st, r, carrierRows, settleOne](
                int status, const QByteArray &body, bool networkError) {
                Q_UNUSED(body);
                if (networkError || status < 200 || status >= 300) {
                    settleOne(false, r.id);
                    applyStep(st);
                    return;
                }
                if (!carrierRows.isEmpty()) {
                    st->carrier = ApplyState::CarrierDrain{};
                    st->carrier.active = true;
                    st->carrier.contactId = r.id;
                    st->carrier.settleId = r.id;
                    st->carrier.rows = carrierRows;
                    postNextCarrier(st);
                    return;
                }
                settleOne(true, r.id);
                applyStep(st);
            });
        return;
    }

    if (!st->deletes.isEmpty()) {
        const QString id = st->deletes.takeFirst();
        m_client->rawRequest(
            QByteArrayLiteral("DELETE"), st->path + QLatin1Char('/') + id,
            {},
            [this, st, id, settleOne](int status, const QByteArray &body,
                                      bool networkError) {
                Q_UNUSED(body);
                if (!networkError && status >= 200 && status < 300) {
                    settleOne(true, id);
                    applyStep(st);
                    return;
                }
                if (networkError || status != 404) {
                    settleOne(false, id);
                    applyStep(st);
                    return;
                }
                // O66(f): consumer delete can 404 flakily. ONE confirming
                // re-list decides: gone ⇒ success, still present ⇒ fail loud.
                m_client->fetchCollection(
                    st->path,
                    [this, st, id, settleOne](
                        std::optional<QJsonArray> items,
                        const GraphError &) {
                        bool stillPresent = false;
                        if (items.has_value()) {
                            for (const auto &cv : *items) {
                                if (cv.toObject()
                                        .value(QStringLiteral("id"))
                                        .toString() == id) {
                                    stillPresent = true;
                                    break;
                                }
                            }
                        }
                        settleOne(items.has_value() && !stillPresent, id);
                        applyStep(st);
                    });
            });
        return;
    }

    finishIfSettled();
}

void GraphContactsBackend::postNextCarrier(std::shared_ptr<ApplyState> st)
{
    if (st->carrier.index >= st->carrier.rows.size()) {
        // A lost carrier row means reversible canon props were dropped on
        // the wire — fail loud rather than silently succeed.
        const bool ok = !st->carrier.failed && !st->carrier.settleId.isEmpty();
        const QString settleId = st->carrier.settleId;
        st->carrier.active = false;
        if (ok)
            st->op->addSucceededUid(settleId);
        else
            st->op->addFailedUid(settleId);
        emit writeProgressChanged(
            st->collectionId,
            st->op->succeededUids().size() + st->op->failedUids().size(),
            st->total);
        applyStep(st);
        return;
    }

    const QJsonObject row = st->carrier.rows.at(st->carrier.index).toObject();
    m_client->rawRequest(
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1.0/me/contacts/%1/extensions")
            .arg(st->carrier.contactId),
        compactWire(row),
        [this, st](int status, const QByteArray &body, bool networkError) {
            Q_UNUSED(body);
            if (networkError || status < 200 || status >= 300)
                st->carrier.failed = true;
            ++st->carrier.index;
            postNextCarrier(st);
        });
}

} // namespace Kalburator::Sync
