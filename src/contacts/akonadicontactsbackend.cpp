#ifdef HAVE_AKONADI

#include "akonadicontactsbackend.h"
#include "backendrecord.h"
#include "collectioninfo.h"
#include "../sync/akonadirevisiondigest.h"

#include <Akonadi/CollectionFetchJob>
#include <Akonadi/CollectionFetchScope>
#include <Akonadi/ItemFetchJob>
#include <Akonadi/ItemFetchScope>
#include <Akonadi/ItemCreateJob>
#include <Akonadi/ItemModifyJob>
#include <Akonadi/ItemDeleteJob>
#include <Akonadi/CollectionCreateJob>
#include <KContacts/Addressee>
#include <KContacts/VCardConverter>

#include <Akonadi/ServerManager>

#include <QDebug>
#include <QCryptographicHash>
#include <QDir>
#include <QStandardPaths>
#include <memory>

namespace Kalburator::Sync {

static const QString AKONADI_CONTACTS_PREFIX = QStringLiteral("akonadi-contacts-");

// ============================================================================
// Construction / Destruction
// ============================================================================

AkonadiContactsBackend::AkonadiContactsBackend(QObject *parent)
    : SyncBackend(parent)
{
    setupMonitor();
}

AkonadiContactsBackend::~AkonadiContactsBackend() = default;

SyncBackend* AkonadiContactsBackend::create(const QVariantMap &config, QObject *parent)
{
    auto *backend = new AkonadiContactsBackend(parent);
    const QString collId = config.value(QStringLiteral("akonadiCollectionId")).toString();
    if (!collId.isEmpty())
        backend->m_scopedCollectionId = collId;
    return backend;
}

const QString AkonadiContactsBackend::BackendTypeName = QStringLiteral("akonadi-contacts");

QString AkonadiContactsBackend::backendType() const
{
    return BackendTypeName;
}

QList<Kalburator::Shape::Shape> AkonadiContactsBackend::nativeShapes() const
{
    return { Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("contacts")},
        Kalburator::Shape::EncodingId{QStringLiteral("vcard4")} } };
}

// ============================================================================
// Monitor Setup
// ============================================================================

void AkonadiContactsBackend::setupMonitor()
{
    const QByteArray sessionName = m_scopedCollectionId.isEmpty()
        ? QByteArrayLiteral("kalburator-akonadi-contacts-backend")
        : ("kalburator-akonadi-contacts-backend-" + m_scopedCollectionId.toUtf8());
    m_session = new Akonadi::Session(sessionName, this);

    m_monitor = new Akonadi::Monitor(this);
    m_monitor->setSession(m_session);
    m_monitor->ignoreSession(m_session);  // Prevent feedback loops from our own writes

    // Filter to contacts MIME type
    m_monitor->setMimeTypeMonitored(KContacts::Addressee::mimeType(), true);

    // Fetch full payload so we can extract Addressee
    Akonadi::ItemFetchScope scope;
    scope.fetchFullPayload(true);
    m_monitor->setItemFetchScope(scope);

    // Monitor collection changes from the root down
    m_monitor->fetchCollection(true);
    m_monitor->setCollectionMonitored(Akonadi::Collection::root());

    // Connect monitor signals to our slots
    QObject::connect(m_monitor, &Akonadi::Monitor::itemAdded,
            this, &AkonadiContactsBackend::onItemAdded);
    QObject::connect(m_monitor, &Akonadi::Monitor::itemChanged,
            this, &AkonadiContactsBackend::onItemChanged);
    QObject::connect(m_monitor, &Akonadi::Monitor::itemRemoved,
            this, &AkonadiContactsBackend::onItemRemoved);
    QObject::connect(m_monitor, &Akonadi::Monitor::collectionAdded,
            this, &AkonadiContactsBackend::onCollectionAdded);
    QObject::connect(m_monitor, qOverload<const Akonadi::Collection &, const QSet<QByteArray> &>(
                &Akonadi::Monitor::collectionChanged),
            this, &AkonadiContactsBackend::onCollectionChanged);
    QObject::connect(m_monitor, &Akonadi::Monitor::collectionRemoved,
            this, &AkonadiContactsBackend::onCollectionRemoved);
}

// ============================================================================
// ID Mapping Helpers
// ============================================================================

QString AkonadiContactsBackend::collectionIdForAkonadiId(Akonadi::Collection::Id id) const
{
    return AKONADI_CONTACTS_PREFIX + QString::number(id);
}

Akonadi::Collection::Id AkonadiContactsBackend::akonadiIdForCollection(const QString &collectionId) const
{
    if (!collectionId.startsWith(AKONADI_CONTACTS_PREFIX))
        return -1;
    bool ok = false;
    auto id = collectionId.mid(AKONADI_CONTACTS_PREFIX.length()).toLongLong(&ok);
    return ok ? id : -1;
}

Akonadi::Item AkonadiContactsBackend::findItemByUid(const QString &collectionId, const QString &uid) const
{
    auto it = m_itemsByCollection.find(collectionId);
    if (it != m_itemsByCollection.end()) {
        auto itemIt = it->find(uid);
        if (itemIt != it->end())
            return *itemIt;
    }
    return Akonadi::Item();
}

Akonadi::Item AkonadiContactsBackend::findCachedItem(const QString &uid,
                                                     QString *outCollectionId) const
{
    for (auto cit = m_itemsByCollection.constBegin();
         cit != m_itemsByCollection.constEnd(); ++cit) {
        const auto &inner = cit.value();
        const auto innerIt = inner.constFind(uid);
        if (innerIt != inner.constEnd()) {
            if (outCollectionId) *outCollectionId = cit.key();
            return innerIt.value();
        }
    }
    return {};
}

// ============================================================================
// Operation-Based Async API
// ============================================================================

FetchOperation* AkonadiContactsBackend::fetchItems(const QString &collectionId)
{
    auto *op = new FetchOperation(collectionId, this);
    registerOperation(op);

    auto colIt = m_collections.find(collectionId);
    if (colIt == m_collections.end()) {
        QString errorMsg = QStringLiteral("Unknown collection: ") + collectionId;
        op->fail(errorMsg);
        Q_EMIT fetchFinished(collectionId, false, errorMsg);
        return op;
    }

    op->setState(SyncOperation::Running);
    Q_EMIT fetchStarted(collectionId, -1);

    auto *job = new Akonadi::ItemFetchJob(*colIt, m_session);
    job->fetchScope().fetchFullPayload(true);

    QObject::connect(job, &Akonadi::ItemFetchJob::finished, this,
            [this, collectionId, op, job]() {
        if (job->error()) {
            op->fail(job->errorString());
            Q_EMIT fetchFinished(collectionId, false, job->errorString());
            return;
        }

        const auto items = job->items();
        int total = items.size();
        int current = 0;
        Q_EMIT fetchProgressChanged(collectionId, 0, total);

        for (const auto &item : items) {
            if (!item.hasPayload<KContacts::Addressee>())
                continue;
            const KContacts::Addressee addressee = item.payload<KContacts::Addressee>();
            const QString uid = addressee.uid();
            m_itemsByCollection[collectionId][uid] = item;
            current++;
            Q_EMIT fetchProgressChanged(collectionId, current, total);
        }

        op->complete();
        Q_EMIT fetchFinished(collectionId, true);
    });

    return op;
}

PushOperation* AkonadiContactsBackend::pushItems(const QString &collectionId,
                                                   const QList<KCalendarCore::Incidence::Ptr> &items)
{
    auto *op = new PushOperation(collectionId, items, this);
    registerOperation(op);

    auto colIt = m_collections.find(collectionId);
    if (colIt == m_collections.end()) {
        op->fail(QStringLiteral("AkonadiContactsBackend::pushItems: unknown collection: ") + collectionId);
        return op;
    }

    qWarning() << "AkonadiContactsBackend::pushItems: Phase L.6 stub — "
                  "contacts push not yet implemented (collectionId:" << collectionId << ")";
    op->setState(SyncOperation::Running);
    op->complete();
    return op;
}

DeleteOperation* AkonadiContactsBackend::deleteItems(const QString &collectionId,
                                                      const QStringList &uids)
{
    auto *op = new DeleteOperation(collectionId, uids, this);
    registerOperation(op);

    if (uids.isEmpty()) {
        op->setState(SyncOperation::Running);
        op->complete();
        return op;
    }

    op->setState(SyncOperation::Running);

    auto completedCount = std::make_shared<int>(0);
    int total = uids.size();

    for (const auto &uid : uids) {
        Akonadi::Item existing = findItemByUid(collectionId, uid);
        if (!existing.isValid()) {
            op->addFailedUid(uid);
            int done = ++(*completedCount);
            if (done >= total)
                op->complete();
            continue;
        }

        auto *job = new Akonadi::ItemDeleteJob(existing, m_session);
        QObject::connect(job, &Akonadi::ItemDeleteJob::finished, this,
                [this, collectionId, uid, op, completedCount, total, job]() {
            if (job->error()) {
                op->addFailedUid(uid);
                qWarning() << "AkonadiContactsBackend: delete failed:" << uid << job->errorString();
            } else {
                op->addSucceededUid(uid);
                m_itemsByCollection[collectionId].remove(uid);
            }
            int done = ++(*completedCount);
            if (done >= total)
                op->complete();
        });
    }

    return op;
}

// ============================================================================
// Monitor Signal Handlers
// ============================================================================

void AkonadiContactsBackend::onItemAdded(const Akonadi::Item &item, const Akonadi::Collection &col)
{
    if (!item.hasPayload<KContacts::Addressee>())
        return;
    const QString colId = collectionIdForAkonadiId(col.id());
    const KContacts::Addressee addressee = item.payload<KContacts::Addressee>();
    m_itemsByCollection[colId][addressee.uid()] = item;
}

void AkonadiContactsBackend::onItemChanged(const Akonadi::Item &item, const QSet<QByteArray> &parts)
{
    Q_UNUSED(parts)
    if (!item.hasPayload<KContacts::Addressee>())
        return;
    const KContacts::Addressee addressee = item.payload<KContacts::Addressee>();
    const QString colId = collectionIdForAkonadiId(item.parentCollection().id());
    m_itemsByCollection[colId][addressee.uid()] = item;
}

void AkonadiContactsBackend::onItemRemoved(const Akonadi::Item &item)
{
    for (auto colIt = m_itemsByCollection.begin(); colIt != m_itemsByCollection.end(); ++colIt) {
        for (auto itemIt = colIt->begin(); itemIt != colIt->end(); ++itemIt) {
            if (itemIt->id() == item.id()) {
                colIt->erase(itemIt);
                return;
            }
        }
    }
}

void AkonadiContactsBackend::onCollectionAdded(const Akonadi::Collection &col,
                                                 const Akonadi::Collection &parent)
{
    Q_UNUSED(parent)
    if (!col.contentMimeTypes().contains(KContacts::Addressee::mimeType()))
        return;
    const QString colId = collectionIdForAkonadiId(col.id());
    m_collectionToContactId.insert(col.id(), colId);
    m_collections.insert(colId, col);
}

void AkonadiContactsBackend::onCollectionChanged(const Akonadi::Collection &col,
                                                   const QSet<QByteArray> &attrs)
{
    Q_UNUSED(attrs)
    const QString colId = collectionIdForAkonadiId(col.id());
    if (m_collections.contains(colId))
        m_collections[colId] = col;
}

void AkonadiContactsBackend::onCollectionRemoved(const Akonadi::Collection &col)
{
    const QString colId = collectionIdForAkonadiId(col.id());
    if (m_collections.contains(colId)) {
        m_collectionToContactId.remove(col.id());
        m_collections.remove(colId);
        m_itemsByCollection.remove(colId);
    }
}

// ============================================================================
// IBlobBackend implementation (Phase L.6 stubs)
// ============================================================================

QString AkonadiContactsBackend::backendId() const
{
    if (!m_scopedCollectionId.isEmpty())
        return QStringLiteral("akonadi-contacts:") + m_scopedCollectionId;
    return QStringLiteral("akonadi-contacts:default");
}

QString AkonadiContactsBackend::displayName() const
{
    return QStringLiteral("AkonadiContactsBackend");
}

bool AkonadiContactsBackend::isAvailable() const
{
    // m_session is always non-null (created in setupMonitor() from ctor),
    // so we use ServerManager to check actual server availability.
    return Akonadi::ServerManager::isRunning();
}

QList<CollectionInfo> AkonadiContactsBackend::availableCollections()
{
    QList<CollectionInfo> result;
    for (auto it = m_collections.constBegin(); it != m_collections.constEnd(); ++it) {
        CollectionInfo info;
        info.id   = it.key();
        info.name = it.value().displayName();
        info.type = QStringLiteral("contacts");
        result.append(info);
    }
    return result;
}

CollectionInfo AkonadiContactsBackend::collectionInfo(const QString &collectionId)
{
    CollectionInfo info;
    info.id   = collectionId;
    info.type = QStringLiteral("contacts");
    if (m_collections.contains(collectionId))
        info.name = m_collections.value(collectionId).displayName();
    else
        info.name = collectionId;
    return info;
}

QString AkonadiContactsBackend::createCollection(const CollectionInfo &info)
{
    const Akonadi::Collection::Id parentId = akonadiIdForCollection(info.path);
    if (parentId < 0) {
        qWarning() << "AkonadiContactsBackend::createCollection: unknown parent" << info.path;
        return {};
    }
    Akonadi::Collection col;
    col.setParentCollection(Akonadi::Collection(parentId));
    col.setName(info.name);
    col.setContentMimeTypes({KContacts::Addressee::mimeType()});
    auto *job = new Akonadi::CollectionCreateJob(col, m_session);
    if (!job->exec()) {
        qWarning() << "AkonadiContactsBackend::createCollection failed:" << job->errorString();
        return {};
    }
    return collectionIdForAkonadiId(job->collection().id());
}

QList<BackendRecord> AkonadiContactsBackend::loadRecords(const QString &collectionId)
{
    QList<BackendRecord> result;
    if (!m_itemsByCollection.contains(collectionId)) return result;

    KContacts::VCardConverter converter;
    const auto &itemMap = m_itemsByCollection.value(collectionId);
    for (auto it = itemMap.constBegin(); it != itemMap.constEnd(); ++it) {
        const Akonadi::Item &aItem = it.value();
        if (!aItem.hasPayload<KContacts::Addressee>()) continue;
        const KContacts::Addressee addressee = aItem.payload<KContacts::Addressee>();

        const QByteArray bytes = converter.createVCard(addressee, KContacts::VCardConverter::v4_0);

        BackendRecord rec;
        // vCard UID is the cross-backend-stable id (matches RemoteContactsBackend);
        // the Akonadi item id is local-only. The (uid -> Akonadi::Item) cache
        // resolves the local item for writes.
        rec.id          = addressee.uid();
        rec.type        = QStringLiteral("contacts");
        rec.data        = bytes;
        rec.contentHash = QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        rec.lastModified = aItem.modificationTime();
        rec.isDeleted    = false;
        result.append(rec);
    }
    return result;
}

std::optional<BackendRecord> AkonadiContactsBackend::loadRecord(const QString &recordId)
{
    // recordId is the vCard UID (cross-backend id scheme); the inner cache
    // map is keyed by UID, so look it up directly.
    KContacts::VCardConverter converter;
    for (auto colIt = m_itemsByCollection.constBegin();
         colIt != m_itemsByCollection.constEnd(); ++colIt) {
        const auto &inner = colIt.value();
        const auto itemIt = inner.constFind(recordId);
        if (itemIt == inner.constEnd()) continue;
        const Akonadi::Item &aItem = itemIt.value();
        if (!aItem.hasPayload<KContacts::Addressee>()) return std::nullopt;

        const KContacts::Addressee addressee = aItem.payload<KContacts::Addressee>();
        const QByteArray bytes = converter.createVCard(addressee, KContacts::VCardConverter::v4_0);

        BackendRecord rec;
        rec.id          = recordId;
        rec.type        = QStringLiteral("contacts");
        rec.data        = bytes;
        rec.contentHash = QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        rec.lastModified = aItem.modificationTime();
        rec.isDeleted    = false;
        return rec;
    }
    return std::nullopt;
}

KContacts::Addressee
AkonadiContactsBackend::addresseeFromRecord(const BackendRecord &record) const
{
    KContacts::VCardConverter converter;
    return converter.parseVCard(record.data);
}

QString AkonadiContactsBackend::createRecord(const QString &collectionId,
                                               const BackendRecord &record)
{
    auto colIt = m_collections.find(collectionId);
    if (colIt == m_collections.end()) {
        qWarning() << "AkonadiContactsBackend::createRecord: unknown collection" << collectionId;
        return {};
    }
    KContacts::Addressee addressee = addresseeFromRecord(record);
    if (addressee.isEmpty()) {
        qWarning() << "AkonadiContactsBackend::createRecord: vCard parse failed for" << record.id;
        return {};
    }
    Akonadi::Item item;
    item.setMimeType(KContacts::Addressee::mimeType());
    item.setPayload<KContacts::Addressee>(addressee);
    auto *job = new Akonadi::ItemCreateJob(item, *colIt, m_session);
    if (!job->exec()) {
        qWarning() << "AkonadiContactsBackend::createRecord: ItemCreateJob failed:" << job->errorString();
        return {};
    }
    m_itemsByCollection[collectionId][addressee.uid()] = job->item();
    return addressee.uid();
}

bool AkonadiContactsBackend::updateRecord(const BackendRecord &record)
{
    KContacts::Addressee addressee = addresseeFromRecord(record);
    if (addressee.isEmpty()) {
        qWarning() << "AkonadiContactsBackend::updateRecord: vCard parse failed for" << record.id;
        return false;
    }
    QString colId;
    Akonadi::Item existing = findCachedItem(record.id, &colId);
    if (!existing.isValid()) {
        qWarning() << "AkonadiContactsBackend::updateRecord: no cached item for" << record.id;
        return false;
    }
    existing.setPayload<KContacts::Addressee>(addressee);
    auto *job = new Akonadi::ItemModifyJob(existing, m_session);
    if (!job->exec()) {
        qWarning() << "AkonadiContactsBackend::updateRecord: ItemModifyJob failed:" << job->errorString();
        return false;
    }
    m_itemsByCollection[colId][addressee.uid()] = job->item();
    return true;
}

bool AkonadiContactsBackend::deleteRecord(const QString &recordId)
{
    QString colId;
    Akonadi::Item existing = findCachedItem(recordId, &colId);
    if (!existing.isValid()) {
        qWarning() << "AkonadiContactsBackend::deleteRecord: no cached item for" << recordId;
        return false;
    }
    auto *job = new Akonadi::ItemDeleteJob(existing, m_session);
    if (!job->exec()) {
        qWarning() << "AkonadiContactsBackend::deleteRecord: ItemDeleteJob failed:" << job->errorString();
        return false;
    }
    m_itemsByCollection[colId].remove(recordId);
    return true;
}

QList<BackendRecord> AkonadiContactsBackend::modifiedSince(const QString &collectionId,
                                                            const QDateTime &since)
{
    QList<BackendRecord> all = loadRecords(collectionId);
    if (!since.isValid()) return all;
    QList<BackendRecord> result;
    for (const auto &rec : all) {
        if (rec.lastModified > since) result.append(rec);
    }
    return result;
}

QStringList AkonadiContactsBackend::deletedSince(const QString &collectionId,
                                                   const QDateTime &since)
{
    Q_UNUSED(collectionId)
    Q_UNUSED(since)
    return {};
}

// ============================================================================
// Backend::ChangeDetection (Task 10)
//
// Fresh token: payload-free ItemFetchJob → computeRevisionDigest over
// (item id, revision) pairs. Cached token: persisted via AkonadiRevisionStore.
// ============================================================================

Kalburator::Sync::AkonadiRevisionStore *AkonadiContactsBackend::revisionStore() const
{
    if (!m_revisionStore) {
        const QString dir = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        m_revisionStore = std::make_unique<Kalburator::Sync::AkonadiRevisionStore>(
            dir + QStringLiteral("/akonadi-contacts-revisions.ini"));
    }
    return m_revisionStore.get();
}

QString AkonadiContactsBackend::collectionRevision(const QString &collectionId)
{
    const Akonadi::Collection::Id cid = akonadiIdForCollection(collectionId);
    if (cid < 0) return {};
    auto *job = new Akonadi::ItemFetchJob(Akonadi::Collection(cid), m_session);
    job->fetchScope().fetchFullPayload(false);  // ids + revisions only, no decode
    if (!job->exec()) {
        qWarning() << "AkonadiContactsBackend::collectionRevision: fetch failed:" << job->errorString();
        return {};
    }
    QList<QPair<qint64, int>> idRev;
    const auto items = job->items();
    idRev.reserve(items.size());
    for (const auto &it : items)
        idRev.append({it.id(), it.revision()});
    return computeRevisionDigest(idRev);
}

QString AkonadiContactsBackend::cachedCollectionRevision(const QString &collectionId) const
{
    return revisionStore()->token(collectionId);
}

void AkonadiContactsBackend::primeRevisionCache(const QMap<QString, QString> &cache)
{
    for (auto it = cache.constBegin(); it != cache.constEnd(); ++it)
        revisionStore()->setToken(it.key(), it.value());
}

} // namespace Kalburator::Sync

#endif // HAVE_AKONADI
