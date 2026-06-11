#ifdef HAVE_AKONADI

#include "akonadibackend.h"
#include "backendcapabilities.h"
#include "discoveredcalendar.h"
#include "logicalcalendar.h"
#include "backendrecord.h"
#include "collectioninfo.h"
#include "../sync/akonadirevisiondigest.h"

#include <Akonadi/CollectionFetchJob>
#include <Akonadi/CollectionFetchScope>
#include <Akonadi/CollectionCreateJob>
#include <Akonadi/CollectionDeleteJob>
#include <Akonadi/ItemFetchJob>
#include <Akonadi/ItemFetchScope>
#include <Akonadi/ItemCreateJob>
#include <Akonadi/ItemModifyJob>
#include <Akonadi/ItemDeleteJob>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Event>
#include <KCalendarCore/Todo>
#include <KCalendarCore/Journal>
#include <KCalendarCore/MemoryCalendar>

#include <Akonadi/ServerManager>

#include <QDebug>
#include <QCryptographicHash>
#include <QDir>
#include <QStandardPaths>
#include <memory>

namespace Kalburator::Sync {

static const QString AKONADI_PREFIX = QStringLiteral("akonadi-");

// Calendar MIME types we care about
static const QString EVENT_MIME   = KCalendarCore::Event::eventMimeType();
static const QString TODO_MIME    = KCalendarCore::Todo::todoMimeType();
static const QString JOURNAL_MIME = KCalendarCore::Journal::journalMimeType();

// ============================================================================
// Construction / Destruction
// ============================================================================

AkonadiBackend::AkonadiBackend(QObject *parent)
    : SyncBackend(parent)
{
    setupMonitor();
}

AkonadiBackend::~AkonadiBackend() = default;

SyncBackend* AkonadiBackend::create(const QVariantMap &config, QObject *parent)
{
    auto *backend = new AkonadiBackend(parent);
    // Phase L.5: scope this backend to a single collection when the caller
    // supplies "akonadiCollectionId" (e.g. "akonadi-42").
    const QString collId = config.value(QStringLiteral("akonadiCollectionId")).toString();
    if (!collId.isEmpty())
        backend->m_scopedCollectionId = collId;
    return backend;
}

const QString AkonadiBackend::BackendTypeName = QStringLiteral("akonadi");

QString AkonadiBackend::backendType() const
{
    return BackendTypeName;
}

QList<Kalburator::Shape::Shape> AkonadiBackend::nativeShapes() const
{
    return { Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("calendar")},
        Kalburator::Shape::EncodingId{QStringLiteral("ical")} } };
}

// ============================================================================
// Monitor Setup
// ============================================================================

void AkonadiBackend::setupMonitor()
{
    const QByteArray sessionName = m_scopedCollectionId.isEmpty()
        ? QByteArrayLiteral("kalburator-akonadi-backend")
        : ("kalburator-akonadi-backend-" + m_scopedCollectionId.toUtf8());
    m_session = new Akonadi::Session(sessionName, this);

    m_monitor = new Akonadi::Monitor(this);
    m_monitor->setSession(m_session);
    m_monitor->ignoreSession(m_session);  // Prevent feedback loops from our own writes

    // Filter to calendar MIME types
    m_monitor->setMimeTypeMonitored(EVENT_MIME, true);
    m_monitor->setMimeTypeMonitored(TODO_MIME, true);
    m_monitor->setMimeTypeMonitored(JOURNAL_MIME, true);

    // Fetch full payload so we can extract Incidence::Ptr
    Akonadi::ItemFetchScope scope;
    scope.fetchFullPayload(true);
    m_monitor->setItemFetchScope(scope);

    // Monitor collection changes from the root down
    m_monitor->fetchCollection(true);
    m_monitor->setCollectionMonitored(Akonadi::Collection::root());

    // Connect monitor signals to our slots
    QObject::connect(m_monitor, &Akonadi::Monitor::itemAdded,
            this, &AkonadiBackend::onItemAdded);
    QObject::connect(m_monitor, &Akonadi::Monitor::itemChanged,
            this, &AkonadiBackend::onItemChanged);
    QObject::connect(m_monitor, &Akonadi::Monitor::itemRemoved,
            this, &AkonadiBackend::onItemRemoved);
    QObject::connect(m_monitor, &Akonadi::Monitor::collectionAdded,
            this, &AkonadiBackend::onCollectionAdded);
    QObject::connect(m_monitor, qOverload<const Akonadi::Collection &, const QSet<QByteArray> &>(
                &Akonadi::Monitor::collectionChanged),
            this, &AkonadiBackend::onCollectionChanged);
    QObject::connect(m_monitor, &Akonadi::Monitor::collectionRemoved,
            this, &AkonadiBackend::onCollectionRemoved);
}

// ============================================================================
// ID Mapping Helpers
// ============================================================================

QString AkonadiBackend::calendarIdForCollection(Akonadi::Collection::Id id) const
{
    return AKONADI_PREFIX + QString::number(id);
}

Akonadi::Collection::Id AkonadiBackend::collectionIdForCalendar(const QString &calendarId) const
{
    if (!calendarId.startsWith(AKONADI_PREFIX))
        return -1;
    bool ok = false;
    auto id = calendarId.mid(AKONADI_PREFIX.length()).toLongLong(&ok);
    return ok ? id : -1;
}

Akonadi::Item AkonadiBackend::findItemByUid(const QString &calendarId, const QString &uid) const
{
    auto it = m_itemsByCalendar.find(calendarId);
    if (it != m_itemsByCalendar.end()) {
        auto itemIt = it->find(uid);
        if (itemIt != it->end())
            return *itemIt;
    }
    return Akonadi::Item();
}

Akonadi::Item AkonadiBackend::findCachedItem(const QString &uid,
                                             QString *outCalendarId) const
{
    for (auto cit = m_itemsByCalendar.constBegin();
         cit != m_itemsByCalendar.constEnd(); ++cit) {
        const auto &inner = cit.value();
        const auto innerIt = inner.constFind(uid);
        if (innerIt != inner.constEnd()) {
            if (outCalendarId) *outCalendarId = cit.key();
            return innerIt.value();
        }
    }
    return {};
}

KCalendarCore::Incidence::Ptr AkonadiBackend::extractIncidence(const Akonadi::Item &item) const
{
    if (!item.hasPayload<KCalendarCore::Incidence::Ptr>())
        return {};
    return item.payload<KCalendarCore::Incidence::Ptr>();
}

KCalendarCore::Incidence::Ptr
AkonadiBackend::incidenceFromRecord(const BackendRecord &record) const
{
    KCalendarCore::ICalFormat fmt;
    KCalendarCore::MemoryCalendar::Ptr cal(
        new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone()));
    if (!fmt.fromString(cal, QString::fromUtf8(record.data)))
        return {};
    const auto incidences = cal->incidences();
    if (incidences.isEmpty())
        return {};
    return incidences.first();
}

bool AkonadiBackend::isCalendarCollection(const Akonadi::Collection &col) const
{
    const auto mimeTypes = col.contentMimeTypes();
    return mimeTypes.contains(EVENT_MIME) ||
           mimeTypes.contains(TODO_MIME) ||
           mimeTypes.contains(JOURNAL_MIME);
}

CalendarType AkonadiBackend::calendarTypeForCollection(const Akonadi::Collection &col) const
{
    const auto mimeTypes = col.contentMimeTypes();
    bool hasEvents = mimeTypes.contains(EVENT_MIME);
    bool hasTodos  = mimeTypes.contains(TODO_MIME);

    if (hasEvents && hasTodos)
        return CalendarType::Hybrid;
    if (hasTodos)
        return CalendarType::Todo;
    return CalendarType::Event;
}

// ============================================================================
// Calendar Discovery & Loading
// ============================================================================

void AkonadiBackend::loadCalendars(const QString &collectionId)
{
    auto *job = new Akonadi::CollectionFetchJob(
        Akonadi::Collection::root(),
        Akonadi::CollectionFetchJob::Recursive,
        m_session);

    // Filter to collections containing calendar data
    job->fetchScope().setContentMimeTypes({EVENT_MIME, TODO_MIME, JOURNAL_MIME});

    connect(job, &Akonadi::CollectionFetchJob::finished, this,
            [this, collectionId, job]() {
        if (job->error()) {
            qWarning() << "AkonadiBackend: CollectionFetchJob failed:" << job->errorString();
            Q_EMIT loadCalendarsFinished(collectionId, false, job->errorString());
            return;
        }

        const auto collections = job->collections();
        for (const auto &col : collections) {
            if (!isCalendarCollection(col))
                continue;

            const QString calId = calendarIdForCollection(col.id());
            m_collectionToCalId.insert(col.id(), calId);
            m_collections.insert(calId, col);

            Q_EMIT calendarDiscovered(collectionId, calId);
        }

        Q_EMIT loadCalendarsFinished(collectionId, true);
    });
}

// ============================================================================
// Incidence CRUD Operations
// ============================================================================

void AkonadiBackend::storeCalendars(const QString &collectionId,
                                     const QList<KCalendarCore::MemoryCalendar*> &calendars)
{
    // Akonadi manages collections via resources - nothing to do here
    Q_UNUSED(collectionId);
    Q_UNUSED(calendars);
    Q_EMIT syncCompleted(collectionId);
}

void AkonadiBackend::startSync(const QString &collectionId,
                                KCalendarCore::MemoryCalendar *calendar,
                                const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                                const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                                const QMap<QString, QString> &stagedDeletions)
{
    const QString calId = calendar->id();
    auto colIt = m_collections.find(calId);
    if (colIt == m_collections.end()) {
        qWarning() << "AkonadiBackend::startSync: unknown calendar" << calId;
        Q_EMIT syncCompleted(collectionId);
        return;
    }

    const QList<KCalendarCore::Incidence::Ptr> &finalCreations = stagedCreations;
    const QList<KCalendarCore::Incidence::Ptr> &finalUpdates = stagedUpdates;

    const Akonadi::Collection &col = *colIt;
    int pending = finalCreations.size() + finalUpdates.size() + stagedDeletions.size();

    if (pending == 0) {
        Q_EMIT syncCompleted(collectionId);
        return;
    }

    // Use a shared counter to track completion
    auto completedCount = std::make_shared<int>(0);

    auto checkDone = [this, collectionId, pending, completedCount]() {
        if (++(*completedCount) >= pending)
            Q_EMIT syncCompleted(collectionId);
    };

    // Process creations
    for (const auto &incidence : finalCreations) {
        Akonadi::Item newItem;
        newItem.setMimeType(incidence->mimeType());
        newItem.setPayload<KCalendarCore::Incidence::Ptr>(incidence);
        auto *job = new Akonadi::ItemCreateJob(newItem, col, m_session);
        connect(job, &Akonadi::ItemCreateJob::finished, this,
                [this, calId, incidence, checkDone, job]() {
            if (job->error()) {
                qWarning() << "AkonadiBackend: sync create failed:" << job->errorString();
            } else {
                m_itemsByCalendar[calId][incidence->uid()] = job->item();
            }
            checkDone();
        });
    }

    // Process updates
    for (const auto &incidence : finalUpdates) {
        Akonadi::Item existing = findItemByUid(calId, incidence->uid());
        if (!existing.isValid()) {
            qWarning() << "AkonadiBackend: sync update - item not found:" << incidence->uid();
            checkDone();
            continue;
        }
        existing.setPayload<KCalendarCore::Incidence::Ptr>(incidence);
        auto *job = new Akonadi::ItemModifyJob(existing, m_session);
        connect(job, &Akonadi::ItemModifyJob::finished, this,
                [checkDone, job]() {
            if (job->error())
                qWarning() << "AkonadiBackend: sync update failed:" << job->errorString();
            checkDone();
        });
    }

    // Process deletions
    for (auto it = stagedDeletions.constBegin(); it != stagedDeletions.constEnd(); ++it) {
        const QString &uid = it.key();
        Akonadi::Item existing = findItemByUid(calId, uid);
        if (!existing.isValid()) {
            qWarning() << "AkonadiBackend: sync delete - item not found:" << uid;
            checkDone();
            continue;
        }
        auto *job = new Akonadi::ItemDeleteJob(existing, m_session);
        connect(job, &Akonadi::ItemDeleteJob::finished, this,
                [this, calId, uid, checkDone, job]() {
            if (job->error()) {
                qWarning() << "AkonadiBackend: sync delete failed:" << job->errorString();
            } else {
                m_itemsByCalendar[calId].remove(uid);
            }
            checkDone();
        });
    }
}

void AkonadiBackend::removeItem(const QString &calId, const QString &itemUid)
{
    Akonadi::Item existing = findItemByUid(calId, itemUid);
    if (!existing.isValid()) {
        qWarning() << "AkonadiBackend::removeItem: item not found" << itemUid << "in" << calId;
        return;
    }

    auto *job = new Akonadi::ItemDeleteJob(existing, m_session);
    connect(job, &Akonadi::ItemDeleteJob::finished, this,
            [this, calId, itemUid, job]() {
        if (job->error()) {
            qWarning() << "AkonadiBackend: removeItem failed for"
                       << itemUid << ":" << job->errorString();
            Q_EMIT calendarError(QString(), calId, job->errorString());
        } else {
            m_itemsByCalendar[calId].remove(itemUid);
            Q_EMIT itemRemoved(calId, itemUid);
        }
    });
}

// ============================================================================
// Operation-Based Async API
// ============================================================================

FetchOperation* AkonadiBackend::fetchItems(const QString &calendarId)
{
    auto *op = new FetchOperation(calendarId, this);
    registerOperation(op);

    auto colIt = m_collections.find(calendarId);
    if (colIt == m_collections.end()) {
        QString errorMsg = QStringLiteral("Unknown calendar: ") + calendarId;
        op->fail(errorMsg);
        Q_EMIT fetchFinished(calendarId, false, errorMsg);
        return op;
    }

    op->setState(SyncOperation::Running);
    Q_EMIT fetchStarted(calendarId, -1);  // Unknown total until job completes

    auto *job = new Akonadi::ItemFetchJob(*colIt, m_session);
    job->fetchScope().fetchFullPayload(true);

    connect(job, &Akonadi::ItemFetchJob::finished, this,
            [this, calendarId, op, job]() {
        if (job->error()) {
            op->fail(job->errorString());
            Q_EMIT fetchFinished(calendarId, false, job->errorString());
            return;
        }

        QList<KCalendarCore::Incidence::Ptr> fetched;
        const auto items = job->items();
        int total = items.size();
        int current = 0;

        Q_EMIT fetchProgressChanged(calendarId, 0, total);

        for (const auto &item : items) {
            auto incidence = extractIncidence(item);
            if (!incidence)
                continue;

            // Update item tracking
            m_itemsByCalendar[calendarId][incidence->uid()] = item;
            fetched.append(incidence);

            Q_EMIT itemFetched(calendarId, incidence);
            current++;
            Q_EMIT fetchProgressChanged(calendarId, current, total);
        }

        op->setFetchedItems(fetched);
        op->complete();
        Q_EMIT fetchFinished(calendarId, true);
    });

    return op;
}

// VESTIGIAL (2026-05-26): the unified SyncEngine drives all writes through the
// per-record IBlobBackend ops (createRecord/updateRecord/deleteRecord), never
// pushItems/startSync/deleteItems. These remain only because they are
// SyncBackend ABI overrides; full removal is deferred to the SyncBackend ABI
// cleanup. Do not wire new write logic here — see
// docs/2026-05-26-akonadi-full-functionality-design.md §1.
PushOperation* AkonadiBackend::pushItems(const QString &calendarId,
                                          const QList<KCalendarCore::Incidence::Ptr> &items)
{
    auto *op = new PushOperation(calendarId, items, this);
    registerOperation(op);

    auto colIt = m_collections.find(calendarId);
    if (colIt == m_collections.end()) {
        op->fail(QStringLiteral("Unknown calendar: ") + calendarId);
        return op;
    }

    const Akonadi::Collection &col = *colIt;
    op->setState(SyncOperation::Running);
    Q_EMIT writeStarted(calendarId, items.size());

    if (items.isEmpty()) {
        op->complete();
        return op;
    }

    auto completedCount = std::make_shared<int>(0);
    int total = items.size();

    for (const auto &incidence : items) {
        Akonadi::Item existing = findItemByUid(calendarId, incidence->uid());

        if (existing.isValid()) {
            // Update
            existing.setPayload<KCalendarCore::Incidence::Ptr>(incidence);
            auto *job = new Akonadi::ItemModifyJob(existing, m_session);
            connect(job, &Akonadi::ItemModifyJob::finished, this,
                    [this, calendarId, incidence, op, completedCount, total, job]() {
                if (job->error()) {
                    op->addFailedUid(incidence->uid());
                    qWarning() << "AkonadiBackend: push modify failed:" << job->errorString();
                } else {
                    op->addSucceededUid(incidence->uid());
                }
                int done = ++(*completedCount);
                Q_EMIT writeProgressChanged(calendarId, done, total);
                if (done >= total) {
                    op->complete();
                }
            });
        } else {
            // Create
            Akonadi::Item newItem;
            newItem.setMimeType(incidence->mimeType());
            newItem.setPayload<KCalendarCore::Incidence::Ptr>(incidence);
            auto *job = new Akonadi::ItemCreateJob(newItem, col, m_session);
            connect(job, &Akonadi::ItemCreateJob::finished, this,
                    [this, calendarId, incidence, op, completedCount, total, job]() {
                if (job->error()) {
                    op->addFailedUid(incidence->uid());
                    qWarning() << "AkonadiBackend: push create failed:" << job->errorString();
                } else {
                    op->addSucceededUid(incidence->uid());
                    m_itemsByCalendar[calendarId][incidence->uid()] = job->item();
                }
                int done = ++(*completedCount);
                Q_EMIT writeProgressChanged(calendarId, done, total);
                if (done >= total) {
                    op->complete();
                }
            });
        }
    }

    return op;
}


DeleteOperation* AkonadiBackend::deleteItems(const QString &calendarId,
                                              const QStringList &uids)
{
    auto *op = new DeleteOperation(calendarId, uids, this);
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
        Akonadi::Item existing = findItemByUid(calendarId, uid);
        if (!existing.isValid()) {
            op->addFailedUid(uid);
            int done = ++(*completedCount);
            if (done >= total)
                op->complete();
            continue;
        }

        auto *job = new Akonadi::ItemDeleteJob(existing, m_session);
        connect(job, &Akonadi::ItemDeleteJob::finished, this,
                [this, calendarId, uid, op, completedCount, total, job]() {
            if (job->error()) {
                op->addFailedUid(uid);
                qWarning() << "AkonadiBackend: delete failed:" << uid << job->errorString();
            } else {
                op->addSucceededUid(uid);
                m_itemsByCalendar[calendarId].remove(uid);
            }
            int done = ++(*completedCount);
            if (done >= total)
                op->complete();
        });
    }

    return op;
}

// ============================================================================
// Discovery Metadata
// ============================================================================

bool AkonadiBackend::discoveredWritable(const QString &calendarId) const
{
    auto it = m_collections.find(calendarId);
    if (it == m_collections.end())
        return true;

    const auto rights = it->rights();
    return rights & Akonadi::Collection::CanCreateItem;
}

DiscoveredCalendar AkonadiBackend::discoveredCalendar(const QString &calendarId) const
{
    DiscoveredCalendar d;
    d.calendarId = calendarId;

    auto it = m_collections.find(calendarId);
    if (it == m_collections.end()) {
        d.writable = true;   // mirrors discoveredWritable's not-found default
        return d;            // Hybrid type, invalid color, empty name (DTO defaults)
    }

    const CalendarType t = calendarTypeForCollection(*it);
    d.supportsVEvent = (t != CalendarType::Todo);
    d.supportsVTodo  = (t != CalendarType::Event);

    if (it->hasAttribute(QByteArrayLiteral("collectioncolor"))) {
        const auto attr = it->attribute(QByteArrayLiteral("collectioncolor"));
        if (attr) {
            const QColor color = QColor::fromString(QString::fromUtf8(attr->serialized()));
            if (color.isValid())
                d.color = color;
        }
    }
    d.name = it->displayName();
    d.writable = (it->rights() & Akonadi::Collection::CanCreateItem);
    return d;
}

// ============================================================================
// Calendar Property Getters
// ============================================================================

QColor AkonadiBackend::calendarColor(const QString &calendarId) const
{
    return discoveredCalendar(calendarId).color;
}

QString AkonadiBackend::calendarDescription(const QString &calendarId) const
{
    Q_UNUSED(calendarId);
    return QString();  // Akonadi doesn't expose calendar description as a standard attribute
}

// ============================================================================
// Calendar CRUD
// ============================================================================

bool AkonadiBackend::supportsCalendarCreation() const
{
    return true;
}

bool AkonadiBackend::createCalendar(const QString &collectionId,
                                     const QString &calendarId,
                                     const QString &name,
                                     CalendarType type)
{
    Q_UNUSED(calendarId);

    // Find parent collection to create under
    // We need a resource to create under - use root as parent
    Akonadi::Collection parent = Akonadi::Collection::root();

    Akonadi::Collection newCol;
    newCol.setParentCollection(parent);
    newCol.setName(name);

    // Set content MIME types based on CalendarType
    QStringList mimeTypes;
    mimeTypes << QStringLiteral("inode/directory");  // Required for collection
    switch (type) {
    case CalendarType::Event:
        mimeTypes << EVENT_MIME;
        break;
    case CalendarType::Todo:
        mimeTypes << TODO_MIME;
        break;
    case CalendarType::Hybrid:
        mimeTypes << EVENT_MIME << TODO_MIME;
        break;
    }
    newCol.setContentMimeTypes(mimeTypes);

    auto *job = new Akonadi::CollectionCreateJob(newCol, m_session);
    connect(job, &Akonadi::CollectionCreateJob::finished, this,
            [this, collectionId, job]() {
        if (job->error()) {
            qWarning() << "AkonadiBackend: createCalendar failed:" << job->errorString();
            Q_EMIT calendarError(collectionId, QString(), job->errorString());
            return;
        }
        const auto created = job->collection();
        const QString calId = calendarIdForCollection(created.id());
        m_collectionToCalId.insert(created.id(), calId);
        m_collections.insert(calId, created);
        Q_EMIT calendarCreated(collectionId, calId);
    });

    return true;  // Async - true means request was submitted
}

bool AkonadiBackend::deleteCalendar(const QString &collectionId, const QString &calendarId)
{
    auto colIt = m_collections.find(calendarId);
    if (colIt == m_collections.end()) {
        qWarning() << "AkonadiBackend::deleteCalendar: unknown calendar" << calendarId;
        return false;
    }

    auto *job = new Akonadi::CollectionDeleteJob(*colIt, m_session);
    connect(job, &Akonadi::CollectionDeleteJob::finished, this,
            [this, collectionId, calendarId, job]() {
        if (job->error()) {
            qWarning() << "AkonadiBackend: deleteCalendar failed:" << job->errorString();
            Q_EMIT calendarError(collectionId, calendarId, job->errorString());
            return;
        }
        auto akonadiId = collectionIdForCalendar(calendarId);
        m_collectionToCalId.remove(akonadiId);
        m_collections.remove(calendarId);
        m_itemsByCalendar.remove(calendarId);
        Q_EMIT calendarDeleted(collectionId, calendarId);
    });

    return true;
}

// ============================================================================
// Capabilities & Metadata
// ============================================================================

BackendCapabilities AkonadiBackend::capabilities() const
{
    return BackendCapabilities::akonadiDefaults();
}

QStringList AkonadiBackend::bindingMetadataKeys() const
{
    return {QStringLiteral("akonadiCollectionId")};
}

void AkonadiBackend::populateBindingMetadata(const DiscoveredCalendar &discovered,
                                              CalendarBackendBinding &binding) const
{
    binding.calendarId = discovered.calendarId;
    // Extract the numeric ID from "akonadi-42" format
    auto akonadiId = collectionIdForCalendar(discovered.calendarId);
    binding.setMetadata(QStringLiteral("akonadiCollectionId"), QString::number(akonadiId));
}

void AkonadiBackend::prepareCreationMetadata(const QString &calendarId,
                                              CalendarBackendBinding &binding) const
{
    auto akonadiId = collectionIdForCalendar(calendarId);
    if (akonadiId >= 0)
        binding.setMetadata(QStringLiteral("akonadiCollectionId"), QString::number(akonadiId));
}

// ============================================================================
// Monitor Signal Handlers
// ============================================================================

void AkonadiBackend::onItemAdded(const Akonadi::Item &item, const Akonadi::Collection &col)
{
    if (!isCalendarCollection(col))
        return;

    const QString calId = calendarIdForCollection(col.id());
    auto incidence = extractIncidence(item);
    if (!incidence)
        return;

    // Track the item
    m_itemsByCalendar[calId][incidence->uid()] = item;

    // Emit as fetched item for live updates
    Q_EMIT itemFetched(calId, incidence);
}

void AkonadiBackend::onItemChanged(const Akonadi::Item &item, const QSet<QByteArray> &parts)
{
    Q_UNUSED(parts);

    auto incidence = extractIncidence(item);
    if (!incidence)
        return;

    // Find which calendar this item belongs to
    const auto parentCol = item.parentCollection();
    const QString calId = calendarIdForCollection(parentCol.id());

    // Update tracked item
    m_itemsByCalendar[calId][incidence->uid()] = item;

    // Emit as fetched item (same as added - updated data)
    Q_EMIT itemFetched(calId, incidence);
}

void AkonadiBackend::onItemRemoved(const Akonadi::Item &item)
{
    // Find the item in our tracking maps
    for (auto calIt = m_itemsByCalendar.begin(); calIt != m_itemsByCalendar.end(); ++calIt) {
        for (auto itemIt = calIt->begin(); itemIt != calIt->end(); ++itemIt) {
            if (itemIt->id() == item.id()) {
                const QString uid = itemIt.key();
                const QString calId = calIt.key();
                calIt->erase(itemIt);
                Q_EMIT itemRemoved(calId, uid);
                return;
            }
        }
    }
}

void AkonadiBackend::onCollectionAdded(const Akonadi::Collection &col,
                                        const Akonadi::Collection &parent)
{
    Q_UNUSED(parent);

    if (!isCalendarCollection(col))
        return;

    const QString calId = calendarIdForCollection(col.id());
    m_collectionToCalId.insert(col.id(), calId);
    m_collections.insert(calId, col);

    // Use empty string for collectionId since we don't know which logical collection
    // this belongs to - the caller will match it by calendarId
    Q_EMIT calendarDiscovered(QString(), calId);
}

void AkonadiBackend::onCollectionChanged(const Akonadi::Collection &col,
                                          const QSet<QByteArray> &attrs)
{
    Q_UNUSED(attrs);

    const QString calId = calendarIdForCollection(col.id());
    if (m_collections.contains(calId)) {
        m_collections[calId] = col;  // Update cached collection
    }
}

void AkonadiBackend::onCollectionRemoved(const Akonadi::Collection &col)
{
    const QString calId = calendarIdForCollection(col.id());
    if (m_collections.contains(calId)) {
        m_collectionToCalId.remove(col.id());
        m_collections.remove(calId);
        m_itemsByCalendar.remove(calId);
        Q_EMIT calendarDeleted(QString(), calId);
    }
}


// ============================================================================
// IBlobBackend implementation (real; KJob::exec() bridge)
//
// Akonadi requires a running D-Bus session and Akonadi daemon.
//
// recordId     = iCal UID (cross-backend-stable; Akonadi::Item::id() is
//                local-only and is never exposed as the record id)
// collectionId = calendarId ("akonadi-<Akonadi::Collection::Id>")
// data         = serialized iCal bytes via KCalendarCore::ICalFormat
// contentHash  = SHA-256 of the iCal bytes
// lastModified = Akonadi::Item::modificationTime()
// ============================================================================

QString AkonadiBackend::backendId() const
{
    // When scoped to a single collection (Phase L.5+), include the collection ID
    // so the engine can distinguish backends for different Akonadi collections.
    if (!m_scopedCollectionId.isEmpty())
        return QStringLiteral("akonadi:") + m_scopedCollectionId;
    // Fallback: unscoped backend (pre-L.5 or BackendRegistry path).
    return QStringLiteral("akonadi:default");
}

QString AkonadiBackend::displayName() const
{
    return QStringLiteral("AkonadiBackend");
}

bool AkonadiBackend::isAvailable() const
{
    // Report true only when the Akonadi server is actually running.
    // m_session is always non-null (created in setupMonitor()), so we can't
    // use it as an availability proxy — use ServerManager instead.
    return Akonadi::ServerManager::isRunning();
}

QList<CollectionInfo> AkonadiBackend::availableCollections()
{
    QList<CollectionInfo> result;
    for (auto it = m_collections.constBegin(); it != m_collections.constEnd(); ++it) {
        CollectionInfo info;
        info.id   = it.key();
        info.name = it.value().displayName();
        info.type = QStringLiteral("calendar");
        result.append(info);
    }
    return result;
}

CollectionInfo AkonadiBackend::collectionInfo(const QString &collectionId)
{
    CollectionInfo info;
    info.id   = collectionId;
    info.type = QStringLiteral("calendar");
    if (m_collections.contains(collectionId)) {
        info.name = m_collections.value(collectionId).displayName();
    } else {
        info.name = collectionId;
    }
    return info;
}

QString AkonadiBackend::createCollection(const CollectionInfo &info)
{
    const Akonadi::Collection::Id parentId = collectionIdForCalendar(info.path);
    if (parentId < 0) {
        qWarning() << "AkonadiBackend::createCollection: unknown parent" << info.path;
        return {};
    }
    Akonadi::Collection col;
    col.setParentCollection(Akonadi::Collection(parentId));
    col.setName(info.name);
    col.setContentMimeTypes({
        KCalendarCore::Event::eventMimeType(),
        KCalendarCore::Todo::todoMimeType(),
        KCalendarCore::Journal::journalMimeType()});
    auto *job = new Akonadi::CollectionCreateJob(col, m_session);
    if (!job->exec()) {
        qWarning() << "AkonadiBackend::createCollection failed:" << job->errorString();
        return {};
    }
    return calendarIdForCollection(job->collection().id());
}

QList<BackendRecord> AkonadiBackend::loadRecords(const QString &collectionId)
{
    // Build BackendRecord list from the in-memory item cache.
    QList<BackendRecord> result;
    if (!m_itemsByCalendar.contains(collectionId)) return result;

    KCalendarCore::ICalFormat fmt;
    const auto &itemMap = m_itemsByCalendar.value(collectionId);
    for (auto it = itemMap.constBegin(); it != itemMap.constEnd(); ++it) {
        const Akonadi::Item &aItem = it.value();
        const KCalendarCore::Incidence::Ptr incidence = extractIncidence(aItem);
        if (!incidence) continue;

        auto tmpCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
        tmpCal->addIncidence(incidence);
        QSharedPointer<KCalendarCore::Calendar> tmpCalPtr(tmpCal, [](KCalendarCore::Calendar*){});
        const QByteArray bytes = fmt.toString(tmpCalPtr).toUtf8();

        BackendRecord rec;
        // BackendRecord.id is the cross-backend-stable domain UID (the iCal
        // UID), matching RemoteCalendarBackend. The Akonadi item id is local
        // only and would never match a peer backend's records. The (uid ->
        // Akonadi::Item) cache (m_itemsByCalendar) resolves the local item for
        // write jobs.
        rec.id   = incidence->uid();
        rec.type = QStringLiteral("calendar");
        rec.data = bytes;

        // Memoize contentHash by Item::revision(): if the revision is unchanged
        // the bytes are unchanged and we can skip re-hashing.
        const QString uid = incidence->uid();
        QString hash;
        const auto memoIt = m_hashMemo.constFind(uid);
        if (memoIt != m_hashMemo.constEnd() && memoIt->first == aItem.revision()) {
            hash = memoIt->second;
        } else {
            hash = QString::fromLatin1(
                QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
            m_hashMemo[uid] = qMakePair(aItem.revision(), hash);
        }
        rec.contentHash = hash;

        rec.lastModified = aItem.modificationTime();
        rec.isDeleted   = false;
        result.append(rec);
    }
    return result;
}

std::optional<BackendRecord> AkonadiBackend::loadRecord(const QString &recordId)
{
    // recordId is the iCal UID (cross-backend id scheme); the inner cache
    // map is keyed by UID, so look it up directly instead of scanning by
    // Akonadi item id (which is no longer the id scheme after Task 1).
    KCalendarCore::ICalFormat fmt;
    for (auto calIt = m_itemsByCalendar.constBegin();
         calIt != m_itemsByCalendar.constEnd(); ++calIt) {
        const auto &inner = calIt.value();
        const auto itemIt = inner.constFind(recordId);
        if (itemIt == inner.constEnd()) continue;

        const Akonadi::Item &aItem = itemIt.value();
        const KCalendarCore::Incidence::Ptr incidence = extractIncidence(aItem);
        if (!incidence) return std::nullopt;

        auto tmpCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
        tmpCal->addIncidence(incidence);
        QSharedPointer<KCalendarCore::Calendar> tmpCalPtr(tmpCal, [](KCalendarCore::Calendar*){});
        const QByteArray bytes = fmt.toString(tmpCalPtr).toUtf8();

        BackendRecord rec;
        rec.id          = recordId;
        rec.type        = QStringLiteral("calendar");
        rec.data        = bytes;
        rec.contentHash = QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        rec.lastModified = aItem.modificationTime();
        rec.isDeleted   = false;
        return rec;
    }
    return std::nullopt;
}

QString AkonadiBackend::createRecord(const QString &collectionId,
                                      const BackendRecord &record)
{
    auto colIt = m_collections.find(collectionId);
    if (colIt == m_collections.end()) {
        qWarning() << "AkonadiBackend::createRecord: unknown collection" << collectionId;
        return {};
    }
    KCalendarCore::Incidence::Ptr incidence = incidenceFromRecord(record);
    if (!incidence) {
        qWarning() << "AkonadiBackend::createRecord: iCal parse failed for" << record.id;
        return {};
    }
    Akonadi::Item item;
    item.setMimeType(incidence->mimeType());
    item.setPayload<KCalendarCore::Incidence::Ptr>(incidence);
    auto *job = new Akonadi::ItemCreateJob(item, *colIt, m_session);
    if (!job->exec()) {
        qWarning() << "AkonadiBackend::createRecord: ItemCreateJob failed:" << job->errorString();
        return {};
    }
    m_itemsByCalendar[collectionId][incidence->uid()] = job->item();
    return incidence->uid();
}

bool AkonadiBackend::updateRecord(const BackendRecord &record)
{
    KCalendarCore::Incidence::Ptr incidence = incidenceFromRecord(record);
    if (!incidence) {
        qWarning() << "AkonadiBackend::updateRecord: iCal parse failed for" << record.id;
        return false;
    }
    QString calId;
    Akonadi::Item existing = findCachedItem(record.id, &calId);
    if (!existing.isValid()) {
        qWarning() << "AkonadiBackend::updateRecord: no cached item for" << record.id;
        return false;
    }
    existing.setPayload<KCalendarCore::Incidence::Ptr>(incidence);
    auto *job = new Akonadi::ItemModifyJob(existing, m_session);
    if (!job->exec()) {
        qWarning() << "AkonadiBackend::updateRecord: ItemModifyJob failed:" << job->errorString();
        return false;
    }
    // Cache under the incidence's own UID (matches createRecord), so the
    // entry stays coherent even if the payload's UID differs from record.id.
    m_itemsByCalendar[calId][incidence->uid()] = job->item();
    return true;
}

bool AkonadiBackend::deleteRecord(const QString &recordId)
{
    QString calId;
    Akonadi::Item existing = findCachedItem(recordId, &calId);
    if (!existing.isValid()) {
        qWarning() << "AkonadiBackend::deleteRecord: no cached item for" << recordId;
        return false;
    }
    auto *job = new Akonadi::ItemDeleteJob(existing, m_session);
    if (!job->exec()) {
        qWarning() << "AkonadiBackend::deleteRecord: ItemDeleteJob failed:" << job->errorString();
        return false;
    }
    m_itemsByCalendar[calId].remove(recordId);
    return true;
}

QList<BackendRecord> AkonadiBackend::modifiedSince(const QString &collectionId,
                                                    const QDateTime &since)
{
    // Filter in-memory cache by modification time.
    QList<BackendRecord> all = loadRecords(collectionId);
    if (!since.isValid()) return all;
    QList<BackendRecord> result;
    for (const auto &rec : all) {
        if (rec.lastModified > since) result.append(rec);
    }
    return result;
}

QStringList AkonadiBackend::deletedSince(const QString &collectionId,
                                          const QDateTime &since)
{
    // Akonadi tracks deletions via Monitor notifications; no persistent log.
    // Tombstone-based deletion tracking is deferred to a future cleanup.
    Q_UNUSED(collectionId)
    Q_UNUSED(since)
    return {};
}

// ============================================================================
// Sync::ChangeDetection (Task 9)
//
// Fresh token: payload-free ItemFetchJob → computeRevisionDigest over
// (item id, revision) pairs. Cached token: persisted via AkonadiRevisionStore.
// ============================================================================

Kalburator::Sync::AkonadiRevisionStore *AkonadiBackend::revisionStore() const
{
    if (!m_revisionStore) {
        const QString dir = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        m_revisionStore = std::make_unique<Kalburator::Sync::AkonadiRevisionStore>(
            dir + QStringLiteral("/akonadi-calendar-revisions.ini"));
    }
    return m_revisionStore.get();
}

QString AkonadiBackend::collectionRevision(const QString &collectionId)
{
    const Akonadi::Collection::Id cid = collectionIdForCalendar(collectionId);
    if (cid < 0) return {};
    auto *job = new Akonadi::ItemFetchJob(Akonadi::Collection(cid), m_session);
    job->fetchScope().fetchFullPayload(false);  // ids + revisions only, no decode
    if (!job->exec()) {
        qWarning() << "AkonadiBackend::collectionRevision: fetch failed:" << job->errorString();
        return {};
    }
    QList<QPair<qint64, int>> idRev;
    const auto items = job->items();
    idRev.reserve(items.size());
    for (const auto &it : items)
        idRev.append({it.id(), it.revision()});
    return computeRevisionDigest(idRev);
}

QString AkonadiBackend::cachedCollectionRevision(const QString &collectionId) const
{
    return revisionStore()->token(collectionId);
}

void AkonadiBackend::primeRevisionCache(const QMap<QString, QString> &cache)
{
    for (auto it = cache.constBegin(); it != cache.constEnd(); ++it)
        revisionStore()->setToken(it.key(), it.value());
}

} // namespace Kalburator::Sync

#endif // HAVE_AKONADI
