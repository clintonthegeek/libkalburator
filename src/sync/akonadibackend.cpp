#ifdef HAVE_AKONADI

#include "akonadibackend.h"
#include "backendcapabilities.h"
#include "discoveredcalendar.h"
#include "logicalcalendar.h"

#include <Akonadi/CollectionFetchJob>
#include <Akonadi/CollectionFetchScope>
#include <Akonadi/CollectionCreateJob>
#include <Akonadi/CollectionDeleteJob>
#include <Akonadi/ItemFetchJob>
#include <Akonadi/ItemFetchScope>
#include <Akonadi/ItemCreateJob>
#include <Akonadi/ItemModifyJob>
#include <Akonadi/ItemDeleteJob>
#include <Akonadi/TagFetchJob>
#include <Akonadi/TagFetchScope>
#include <Akonadi/TagModifyJob>
#include <Akonadi/TagCreateJob>
#include <Akonadi/TagAttribute>

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Event>
#include <KCalendarCore/Todo>
#include <KCalendarCore/Journal>
#include <KCalendarCore/MemoryCalendar>

#include "kalbconfigmanager.h"
#include "collectionsettings.h"
#include "tagsettings.h"

#include <QDebug>
#include <memory>

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
    Q_UNUSED(config);
    return new AkonadiBackend(parent);
}

const QString AkonadiBackend::BackendTypeName = QStringLiteral("akonadi");

QString AkonadiBackend::backendType() const
{
    return BackendTypeName;
}

// ============================================================================
// Monitor Setup
// ============================================================================

void AkonadiBackend::setupMonitor()
{
    m_session = new Akonadi::Session("PlanStan-Akonadi", this);

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
    connect(m_monitor, &Akonadi::Monitor::itemAdded,
            this, &AkonadiBackend::onItemAdded);
    connect(m_monitor, &Akonadi::Monitor::itemChanged,
            this, &AkonadiBackend::onItemChanged);
    connect(m_monitor, &Akonadi::Monitor::itemRemoved,
            this, &AkonadiBackend::onItemRemoved);
    connect(m_monitor, &Akonadi::Monitor::collectionAdded,
            this, &AkonadiBackend::onCollectionAdded);
    connect(m_monitor, qOverload<const Akonadi::Collection &, const QSet<QByteArray> &>(
                &Akonadi::Monitor::collectionChanged),
            this, &AkonadiBackend::onCollectionChanged);
    connect(m_monitor, &Akonadi::Monitor::collectionRemoved,
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

KCalendarCore::Incidence::Ptr AkonadiBackend::extractIncidence(const Akonadi::Item &item) const
{
    if (!item.hasPayload<KCalendarCore::Incidence::Ptr>())
        return {};
    return item.payload<KCalendarCore::Incidence::Ptr>();
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
            emit loadCalendarsFinished(collectionId, false, job->errorString());
            return;
        }

        const auto collections = job->collections();
        for (const auto &col : collections) {
            if (!isCalendarCollection(col))
                continue;

            const QString calId = calendarIdForCollection(col.id());
            m_collectionToCalId.insert(col.id(), calId);
            m_collections.insert(calId, col);

            emit calendarDiscovered(collectionId, calId);
        }

        emit loadCalendarsFinished(collectionId, true);

        // Silently pull tag colors from KDE PIM
        if (m_configManager)
            fetchTagColors(m_configManager);
    });
}

void AkonadiBackend::loadItems(KCalendarCore::MemoryCalendar *cal, bool suppressSignals)
{
    // Determine which Akonadi collection to load from via the calendar's ID
    // (set by CollectionController to the calendarId, e.g., "akonadi-57")
    const QString calId = cal->id();
    auto colIt = m_collections.find(calId);
    if (colIt == m_collections.end()) {
        qWarning() << "AkonadiBackend::loadItems: unknown calendar" << calId;
        if (!suppressSignals)
            emit calendarLoaded(cal);
        return;
    }

    auto *job = new Akonadi::ItemFetchJob(*colIt, m_session);
    job->fetchScope().fetchFullPayload(true);

    connect(job, &Akonadi::ItemFetchJob::finished, this,
            [this, cal, calId, suppressSignals, job]() {
        if (job->error()) {
            qWarning() << "AkonadiBackend: ItemFetchJob failed:" << job->errorString();
            if (!suppressSignals)
                emit calendarLoaded(cal);
            return;
        }

        const auto items = job->items();
        for (const auto &item : items) {
            auto incidence = extractIncidence(item);
            if (!incidence)
                continue;

            // Track the item for later modification/deletion
            m_itemsByCalendar[calId][incidence->uid()] = item;

            cal->addIncidence(incidence);
            if (!suppressSignals)
                emit itemLoaded(cal, incidence, QString::number(item.revision()));
        }

        if (!suppressSignals)
            emit calendarLoaded(cal);
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
    emit syncCompleted(collectionId);
}

void AkonadiBackend::storeItems(KCalendarCore::MemoryCalendar *cal,
                                 const QList<KCalendarCore::Incidence::Ptr> &items)
{
    const QString calId = cal->id();
    auto colIt = m_collections.find(calId);
    if (colIt == m_collections.end()) {
        qWarning() << "AkonadiBackend::storeItems: unknown calendar" << calId;
        return;
    }

    const Akonadi::Collection &col = *colIt;
    int totalItems = items.size();

    emit writeStarted(calId, totalItems);

    if (items.isEmpty()) {
        emit writeFinished(calId, true);
        return;
    }

    auto completedCount = std::make_shared<int>(0);

    for (const auto &incidence : items) {
        Akonadi::Item existing = findItemByUid(calId, incidence->uid());

        if (existing.isValid()) {
            // Update existing item
            existing.setPayload<KCalendarCore::Incidence::Ptr>(incidence);
            existing.setMimeType(incidence->mimeType());
            auto *modJob = new Akonadi::ItemModifyJob(existing, m_session);
            connect(modJob, &Akonadi::ItemModifyJob::finished, this,
                    [this, calId, incidence, completedCount, totalItems, modJob]() {
                if (modJob->error()) {
                    qWarning() << "AkonadiBackend: ItemModifyJob failed for"
                               << incidence->uid() << ":" << modJob->errorString();
                }
                int done = ++(*completedCount);
                emit writeProgressChanged(calId, done, totalItems);
                if (done >= totalItems)
                    emit writeFinished(calId, true);
            });
        } else {
            // Create new item
            Akonadi::Item newItem;
            newItem.setMimeType(incidence->mimeType());
            newItem.setPayload<KCalendarCore::Incidence::Ptr>(incidence);
            auto *createJob = new Akonadi::ItemCreateJob(newItem, col, m_session);
            connect(createJob, &Akonadi::ItemCreateJob::finished, this,
                    [this, calId, incidence, completedCount, totalItems, createJob]() {
                if (createJob->error()) {
                    qWarning() << "AkonadiBackend: ItemCreateJob failed for"
                               << incidence->uid() << ":" << createJob->errorString();
                } else {
                    // Track the newly created item
                    m_itemsByCalendar[calId][incidence->uid()] = createJob->item();
                }
                int done = ++(*completedCount);
                emit writeProgressChanged(calId, done, totalItems);
                if (done >= totalItems)
                    emit writeFinished(calId, true);
            });
        }
    }
}

void AkonadiBackend::updateItem(KCalendarCore::MemoryCalendar *cal,
                                 const KCalendarCore::Incidence::Ptr &item,
                                 const QString &icalData)
{
    Q_UNUSED(icalData);  // We use the Incidence::Ptr directly

    const QString calId = cal->id();
    Akonadi::Item existing = findItemByUid(calId, item->uid());

    if (!existing.isValid()) {
        qWarning() << "AkonadiBackend::updateItem: item not found" << item->uid();
        return;
    }

    existing.setPayload<KCalendarCore::Incidence::Ptr>(item);
    auto *job = new Akonadi::ItemModifyJob(existing, m_session);
    connect(job, &Akonadi::ItemModifyJob::finished, this,
            [this, calId, item, job]() {
        if (job->error()) {
            qWarning() << "AkonadiBackend: updateItem failed for"
                       << item->uid() << ":" << job->errorString();
            emit calendarError(QString(), calId, job->errorString());
        }
    });
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
        emit syncCompleted(collectionId);
        return;
    }

    const Akonadi::Collection &col = *colIt;
    int pending = stagedCreations.size() + stagedUpdates.size() + stagedDeletions.size();

    if (pending == 0) {
        emit syncCompleted(collectionId);
        return;
    }

    // Use a shared counter to track completion
    auto completedCount = std::make_shared<int>(0);

    auto checkDone = [this, collectionId, pending, completedCount]() {
        if (++(*completedCount) >= pending)
            emit syncCompleted(collectionId);
    };

    // Process creations
    for (const auto &incidence : stagedCreations) {
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
    for (const auto &incidence : stagedUpdates) {
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
            emit calendarError(QString(), calId, job->errorString());
        } else {
            m_itemsByCalendar[calId].remove(itemUid);
            emit itemRemoved(calId, itemUid);
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
        emit fetchFinished(calendarId, false, errorMsg);
        return op;
    }

    op->setState(SyncOperation::Running);
    emit fetchStarted(calendarId, -1);  // Unknown total until job completes

    auto *job = new Akonadi::ItemFetchJob(*colIt, m_session);
    job->fetchScope().fetchFullPayload(true);

    connect(job, &Akonadi::ItemFetchJob::finished, this,
            [this, calendarId, op, job]() {
        if (job->error()) {
            op->fail(job->errorString());
            emit fetchFinished(calendarId, false, job->errorString());
            return;
        }

        QList<KCalendarCore::Incidence::Ptr> fetched;
        const auto items = job->items();
        int total = items.size();
        int current = 0;

        emit fetchProgressChanged(calendarId, 0, total);

        for (const auto &item : items) {
            auto incidence = extractIncidence(item);
            if (!incidence)
                continue;

            // Update item tracking
            m_itemsByCalendar[calendarId][incidence->uid()] = item;
            fetched.append(incidence);

            emit itemFetched(calendarId, incidence);
            current++;
            emit fetchProgressChanged(calendarId, current, total);
        }

        op->setFetchedItems(fetched);
        op->complete();
        emit fetchFinished(calendarId, true);
    });

    return op;
}

PushOperation* AkonadiBackend::pushItems(const QString &calendarId,
                                          const QList<KCalendarCore::Incidence::Ptr> &items)
{
    auto *op = new PushOperation(calendarId, items, this);
    registerOperation(op);

    auto colIt = m_collections.find(calendarId);
    if (colIt == m_collections.end()) {
        op->fail(QStringLiteral("Unknown calendar: ") + calendarId);
        emit writeFinished(calendarId, false);
        return op;
    }

    const Akonadi::Collection &col = *colIt;
    op->setState(SyncOperation::Running);
    emit writeStarted(calendarId, items.size());

    if (items.isEmpty()) {
        op->complete();
        emit writeFinished(calendarId, true);
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
                emit writeProgressChanged(calendarId, done, total);
                if (done >= total) {
                    op->complete();
                    emit writeFinished(calendarId, true);
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
                emit writeProgressChanged(calendarId, done, total);
                if (done >= total) {
                    op->complete();
                    emit writeFinished(calendarId, true);
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

CalendarType AkonadiBackend::discoveredCalendarType(const QString &calendarId) const
{
    auto it = m_collections.find(calendarId);
    if (it == m_collections.end())
        return CalendarType::Hybrid;
    return calendarTypeForCollection(*it);
}

QColor AkonadiBackend::discoveredColor(const QString &calendarId) const
{
    auto it = m_collections.find(calendarId);
    if (it == m_collections.end())
        return QColor();

    // Akonadi stores color as an entity attribute
    if (it->hasAttribute(QByteArrayLiteral("collectioncolor"))) {
        const auto attr = it->attribute(QByteArrayLiteral("collectioncolor"));
        if (attr) {
            // CollectionColorAttribute stores the color as serialized data
            QColor color = QColor::fromString(QString::fromUtf8(attr->serialized()));
            if (color.isValid())
                return color;
        }
    }
    return QColor();
}

QString AkonadiBackend::discoveredDisplayName(const QString &calendarId) const
{
    auto it = m_collections.find(calendarId);
    if (it == m_collections.end())
        return QString();
    return it->displayName();
}

bool AkonadiBackend::discoveredWritable(const QString &calendarId) const
{
    auto it = m_collections.find(calendarId);
    if (it == m_collections.end())
        return true;

    const auto rights = it->rights();
    return rights & Akonadi::Collection::CanCreateItem;
}

// ============================================================================
// Calendar Property Getters
// ============================================================================

QColor AkonadiBackend::calendarColor(const QString &calendarId) const
{
    return discoveredColor(calendarId);
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
            emit calendarError(collectionId, QString(), job->errorString());
            return;
        }
        const auto created = job->collection();
        const QString calId = calendarIdForCollection(created.id());
        m_collectionToCalId.insert(created.id(), calId);
        m_collections.insert(calId, created);
        emit calendarCreated(collectionId, calId);
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
            emit calendarError(collectionId, calendarId, job->errorString());
            return;
        }
        auto akonadiId = collectionIdForCalendar(calendarId);
        m_collectionToCalId.remove(akonadiId);
        m_collections.remove(calendarId);
        m_itemsByCalendar.remove(calendarId);
        emit calendarDeleted(collectionId, calendarId);
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
// Tag Color Sync
// ============================================================================

void AkonadiBackend::setConfigManager(KalbConfigManager *mgr)
{
    m_configManager = mgr;
}

void AkonadiBackend::fetchTagColors(KalbConfigManager *configManager)
{
    if (!configManager)
        return;

    auto *job = new Akonadi::TagFetchJob(m_session);
    job->fetchScope().fetchAttribute<Akonadi::TagAttribute>();

    connect(job, &Akonadi::TagFetchJob::finished, this,
            [this, configManager, job]() {
        if (job->error()) {
            qWarning() << "AkonadiBackend: TagFetchJob failed:" << job->errorString();
            emit tagColorsSynced(0);
            return;
        }

        TagSettings &tagSettings = configManager->settings().tagSettings();
        int importedCount = 0;

        const auto tags = job->tags();
        for (const auto &tag : tags) {
            const QString name = tag.name();
            if (name.isEmpty())
                continue;

            // Only import colors for tags not already defined locally
            if (tagSettings.hasTag(name))
                continue;

            const auto *attr = tag.attribute<Akonadi::TagAttribute>();
            if (!attr)
                continue;

            QColor color = attr->backgroundColor();
            if (!color.isValid())
                continue;

            tagSettings.setTagColor(name, color);
            importedCount++;
            qDebug() << "AkonadiBackend: Imported tag color from KDE PIM:"
                     << name << "->" << color.name();
        }

        if (importedCount > 0) {
            configManager->saveCollectionConfig();
            qDebug() << "AkonadiBackend: Imported" << importedCount << "tag colors from KDE PIM";
        }

        emit tagColorsSynced(importedCount);
    });
}

void AkonadiBackend::pushTagColors(const TagSettings &tagSettings)
{
    // First fetch existing Akonadi tags to get their IDs
    auto *fetchJob = new Akonadi::TagFetchJob(m_session);
    fetchJob->fetchScope().fetchAttribute<Akonadi::TagAttribute>();

    connect(fetchJob, &Akonadi::TagFetchJob::finished, this,
            [this, tagSettings, fetchJob]() {
        if (fetchJob->error()) {
            qWarning() << "AkonadiBackend: Tag fetch for push failed:" << fetchJob->errorString();
            return;
        }

        // Build map of existing Akonadi tag name -> Tag
        QMap<QString, Akonadi::Tag> existingTags;
        const auto akonadiTags = fetchJob->tags();
        for (const auto &tag : akonadiTags) {
            existingTags.insert(tag.name(), tag);
        }

        const auto allDefs = tagSettings.allTags();
        for (const TagDefinition &def : allDefs) {
            if (!def.color.isValid())
                continue;

            if (existingTags.contains(def.id)) {
                // Update existing tag
                Akonadi::Tag tag = existingTags.value(def.id);
                auto *attr = tag.attribute<Akonadi::TagAttribute>(
                    Akonadi::Tag::AddIfMissing);
                attr->setBackgroundColor(def.color);
                new Akonadi::TagModifyJob(tag, m_session);
                qDebug() << "AkonadiBackend: Updated KDE PIM tag color:"
                         << def.id << "->" << def.color.name();
            } else {
                // Create new tag
                Akonadi::Tag tag(def.id);
                tag.setGid(def.id.toUtf8());
                auto *attr = tag.attribute<Akonadi::TagAttribute>(
                    Akonadi::Tag::AddIfMissing);
                attr->setBackgroundColor(def.color);
                auto *createJob = new Akonadi::TagCreateJob(tag, m_session);
                createJob->setMergeIfExisting(true);
                qDebug() << "AkonadiBackend: Created KDE PIM tag:"
                         << def.id << "with color" << def.color.name();
            }
        }
    });
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
    emit itemFetched(calId, incidence);
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
    emit itemFetched(calId, incidence);
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
                emit itemRemoved(calId, uid);
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
    emit calendarDiscovered(QString(), calId);
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
        emit calendarDeleted(QString(), calId);
    }
}

#endif // HAVE_AKONADI
