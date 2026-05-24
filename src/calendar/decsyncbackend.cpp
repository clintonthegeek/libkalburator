#include "decsyncbackend.h"
#include "decsyncactivecontroller.h"
#include "decsynccontrollerstore.h"
#include "syncthingmonitor.h"
#include "decsynclib.h"
#include "backendcapabilities.h"
#include "logicalcalendar.h"
#include "discoveredcalendar.h"
#include "backendrecord.h"
#include "collectioninfo.h"
#include <KCalendarCore/ICalFormat>
#include <QDir>
#include <QDebug>
#include <QTimer>
#include <QHostInfo>
#include <QCryptographicHash>

namespace Kalburator::Sync {

const QString DecSyncBackend::BackendTypeName = QStringLiteral("decsync");

QString DecSyncBackend::backendType() const { return BackendTypeName; }

QList<Kalburator::Shape::Shape> DecSyncBackend::nativeShapes() const
{
    return { Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("calendar")},
        Kalburator::Shape::EncodingId{QStringLiteral("ical")} } };
}

// ============================================================================
// Construction
// ============================================================================

DecSyncBackend::DecSyncBackend(const QString &decsyncDir, const QString &appId, QObject *parent)
    : SyncBackend(parent)
    , m_dir(new DecSyncDir(decsyncDir))
    , m_appId(appId)
{
    if (m_appId.isEmpty()) {
        m_appId = QStringLiteral("planstan-") + QHostInfo::localHostName();
    }
    m_controllerStore = new DecSyncControllerStore(
        decsyncDir + QStringLiteral("/.planstan-controller.db"));
    m_controllerStore->open();
}

DecSyncBackend::~DecSyncBackend()
{
    qDeleteAll(m_controllers);
    delete m_controllerStore;
    qDeleteAll(m_collections);
    delete m_dir;
}

SyncBackend* DecSyncBackend::create(const QVariantMap &config, QObject *parent)
{
    QString decsyncDir = config.value(QStringLiteral("decsyncDir")).toString();
    QString appId = config.value(QStringLiteral("appId")).toString();
    return new DecSyncBackend(decsyncDir, appId, parent);
}

// ============================================================================
// Active Controller
// ============================================================================

DecSyncActiveController* DecSyncBackend::ensureController(const QString &calendarId) const
{
    if (!m_controllers.contains(calendarId)) {
        DecSyncCollection *coll = collectionFor(calendarId);
        if (!coll) return nullptr;

        QString syncType, collId;
        parseSyncId(calendarId, syncType, collId);
        QString storeCollId = syncType + QStringLiteral("/") + collId;

        auto *controller = new DecSyncActiveController(
            coll, m_controllerStore, m_appId, storeCollId,
            const_cast<DecSyncBackend*>(this));
        m_controllers[calendarId] = controller;
    }
    return m_controllers[calendarId];
}

DecSyncActiveController* DecSyncBackend::activeController(const QString &calendarId) const
{
    return ensureController(calendarId);
}

void DecSyncBackend::checkForRemoteChanges(const QString &calendarId)
{
    auto *controller = ensureController(calendarId);
    if (controller) {
        controller->runActiveSync();
    }
}

void DecSyncBackend::setSyncthingMonitor(SyncthingMonitor *monitor)
{
    if (m_syncthingMonitor) {
        m_syncthingMonitor->disconnect(this);
    }

    m_syncthingMonitor = monitor;

    if (m_syncthingMonitor) {
        connect(m_syncthingMonitor, &SyncthingMonitor::remoteChangesReady,
                this, [this](const QString &/*folder*/) {
            // Trigger active sync for all known calendars
            for (auto it = m_controllers.constBegin();
                 it != m_controllers.constEnd(); ++it) {
                checkForRemoteChanges(it.key());
            }
        });

        qDebug() << "DecSyncBackend: Syncthing monitor connected";
    }
}

// ============================================================================
// Calendar Discovery & Loading
// ============================================================================

void DecSyncBackend::loadCalendars(const QString &collectionId)
{
    qDebug() << "DecSyncBackend::loadCalendars for" << collectionId;

    if (!m_dir->checkOrCreateInfo()) {
        qWarning() << "DecSyncBackend: Failed to initialize DecSync directory";
        emit loadCalendarsFinished(collectionId, false,
            QStringLiteral("Failed to initialize DecSync directory"));
        return;
    }

    // Scan calendars/
    const QStringList calendars = m_dir->listCollections(QStringLiteral("calendars"));
    QSet<QString> calendarIds;  // track for hybrid detection
    for (const QString &calId : calendars) {
        QMap<QString, QJsonValue> info = m_dir->getStaticInfo(QStringLiteral("calendars"), calId);
        // Skip soft-deleted collections
        if (info.value(QStringLiteral("deleted")).toBool(false)) {
            continue;
        }
        calendarIds.insert(calId);
        // If marked as hybrid, remember it (even if tasks/ side doesn't exist yet)
        if (info.value(QStringLiteral("hybrid")).toBool(false)) {
            m_hybridIds.insert(calId);
        }
        emit calendarDiscovered(collectionId, calId);
        qDebug() << "DecSyncBackend: discovered calendar" << calId;
    }

    // Scan tasks/
    const QStringList tasks = m_dir->listCollections(QStringLiteral("tasks"));
    for (const QString &taskId : tasks) {
        QMap<QString, QJsonValue> info = m_dir->getStaticInfo(QStringLiteral("tasks"), taskId);
        if (info.value(QStringLiteral("deleted")).toBool(false)) {
            continue;
        }

        // If a matching calendars/ collection exists, this is a hybrid pair.
        // Mark it as hybrid and skip emitting — the calendars/ side already emitted.
        if (calendarIds.contains(taskId)) {
            m_hybridIds.insert(taskId);
            qDebug() << "DecSyncBackend: discovered hybrid pair for" << taskId;
            continue;
        }

        // If the hybrid flag is set, this is a hybrid calendar with only tasks/.
        // Emit with bare ID (not "tasks/" prefixed) so it matches PlanStan's binding.
        if (info.value(QStringLiteral("hybrid")).toBool(false)) {
            m_hybridIds.insert(taskId);
            emit calendarDiscovered(collectionId, taskId);
            qDebug() << "DecSyncBackend: discovered hybrid (tasks-only)" << taskId;
            continue;
        }

        QString calId = QStringLiteral("tasks/") + taskId;
        emit calendarDiscovered(collectionId, calId);
        qDebug() << "DecSyncBackend: discovered task list" << calId;
    }

    emit loadCalendarsFinished(collectionId, true);
}

// ============================================================================
// Incidence CRUD
// ============================================================================

void DecSyncBackend::storeCalendars(const QString &collectionId,
                                     const QList<KCalendarCore::MemoryCalendar*> &calendars)
{
    Q_UNUSED(collectionId);
    Q_UNUSED(calendars);
}

void DecSyncBackend::removeItem(const QString &calId, const QString &itemUid)
{
    if (calId.isEmpty() || itemUid.isEmpty()) {
        qWarning() << "DecSyncBackend::removeItem: Empty calId or uid";
        return;
    }

    DecSyncCollection *coll = collectionFor(calId);
    if (coll) {
        // Write null value = deleted
        coll->setEntry({QStringLiteral("resources"), itemUid},
                       QJsonValue(),  // null key
                       QJsonValue()); // null value = deleted
    }

    // For hybrid calendars, also try removing from the tasks/ companion
    DecSyncCollection *todoColl = todoCollectionFor(calId);
    if (todoColl) {
        todoColl->setEntry({QStringLiteral("resources"), itemUid},
                           QJsonValue(),
                           QJsonValue());
    }
}

void DecSyncBackend::startSync(const QString &collectionId,
                                KCalendarCore::MemoryCalendar* calendar,
                                const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                                const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                                const QMap<QString, QString> &stagedDeletions)
{
    if (!calendar) {
        qWarning() << "DecSyncBackend::startSync: Null calendar";
        emit syncCompleted(collectionId);
        return;
    }

    const QString calId = calendar->id();
    bool hybrid = isHybridCalendar(calId);

    if (!hybrid) {
        DecSyncCollection *coll = collectionFor(calId);
        if (!coll) {
            emit syncCompleted(collectionId);
            return;
        }
    }

    // Apply deletions (for hybrid, removeItem already handles both collections)
    for (auto it = stagedDeletions.constBegin(); it != stagedDeletions.constEnd(); ++it) {
        removeItem(calId, it.key());
    }

    // Combine creations and updates into allWrites
    QList<KCalendarCore::Incidence::Ptr> allWrites;
    allWrites.reserve(stagedCreations.size() + stagedUpdates.size());
    allWrites.append(stagedCreations);
    allWrites.append(stagedUpdates);

    if (!allWrites.isEmpty()) {
        QList<DecSyncEntry> eventEntries;
        QList<DecSyncEntry> todoEntries;
        QString dt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

        for (const auto &item : allWrites) {
            if (item.isNull() || item->uid().isEmpty()) continue;

            if (!isTypeAllowed(calId, item)) {
                CalendarType expected = discoveredCalendarType(calId);
                CalendarType actual = incidenceCalendarType(item);
                qWarning() << "DecSyncBackend::startSync: Type violation -"
                            << item->uid() << "is"
                            << (actual == CalendarType::Todo ? "VTODO" : "VEVENT")
                            << "but collection" << calId << "expects"
                            << (expected == CalendarType::Todo ? "VTODO" : "VEVENT");
                emit calendarError(collectionId, calId,
                    QStringLiteral("Cannot sync %1 to %2 collection: type mismatch")
                        .arg(item->uid(), expected == CalendarType::Todo
                             ? QStringLiteral("tasks") : QStringLiteral("calendars")));
                continue;
            }

            DecSyncEntry entry;
            entry.path = {QStringLiteral("resources"), item->uid()};
            entry.datetime = dt;
            entry.key = QJsonValue();
            entry.value = QJsonValue(serializeIncidence(item));

            if (hybrid && incidenceCalendarType(item) == CalendarType::Todo) {
                todoEntries.append(entry);
            } else {
                eventEntries.append(entry);
            }
        }

        int total = eventEntries.size() + todoEntries.size();
        if (total > 0) {
            emit writeStarted(calId, total);
            if (!eventEntries.isEmpty()) {
                DecSyncCollection *coll = hybrid ? ensureEventCollection(calId) : collectionFor(calId);
                if (coll) coll->setEntries(eventEntries);
            }
            if (!todoEntries.isEmpty()) {
                DecSyncCollection *todoColl = hybrid ? ensureTodoCollection(calId) : nullptr;
                if (todoColl) todoColl->setEntries(todoEntries);
            }
        }
    }

    emit syncCompleted(collectionId);
}

// ============================================================================
// Operation-Based API
// ============================================================================

FetchOperation* DecSyncBackend::fetchItems(const QString &calendarId)
{
    auto *op = new FetchOperation(calendarId, this);
    registerOperation(op);

    // Phase 1: Read resources (one-shot, deferred to next event loop tick)
    QTimer::singleShot(0, this, [this, op, calendarId]() {
        if (op->state() == SyncOperation::Cancelled) return;
        op->setState(SyncOperation::Running);

        DecSyncCollection *todoColl = todoCollectionFor(calendarId);
        auto *controller = ensureController(calendarId);

        if (!controller && !todoColl) {
            op->fail(QStringLiteral("No collection for calendar: %1").arg(calendarId));
            emit fetchFinished(calendarId, false, op->errorString());
            return;
        }

        // Read resources — use simple path on initial load, controller path on subsequent
        QMap<QString, DecSyncEntry> resources;
        if (controller && !controller->isInitialLoad()) {
            resources = controller->preprocessFetch();
        } else {
            DecSyncCollection *coll = collectionFor(calendarId);
            if (coll) {
                resources = coll->readAllResources();
            }
        }

        // Read todo resources for hybrid calendars
        QMap<QString, DecSyncEntry> todoResources;
        if (todoColl) {
            todoResources = todoColl->readAllResources();
        }

        // Merge into a flat list for chunked processing
        using EntryPair = QPair<QString, DecSyncEntry>;
        auto *entries = new QList<EntryPair>();
        entries->reserve(resources.size() + todoResources.size());
        for (auto it = resources.constBegin(); it != resources.constEnd(); ++it) {
            entries->append({it.key(), it.value()});
        }
        for (auto it = todoResources.constBegin(); it != todoResources.constEnd(); ++it) {
            if (!resources.contains(it.key())) {  // Avoid duplicates in hybrid
                entries->append({it.key(), it.value()});
            }
        }

        const int total = entries->size();
        emit fetchStarted(calendarId, total);

        if (total == 0) {
            delete entries;
            op->setFetchedItems({});
            op->complete();
            emit fetchFinished(calendarId, true);
            return;
        }

        // Phase 2: Chunked deserialization
        // Shared state captured by the chunk lambda
        auto *items = new QList<KCalendarCore::Incidence::Ptr>();
        auto *pos = new int(0);
        bool hybrid = isHybridCalendar(calendarId);

        static constexpr int CHUNK_SIZE = 20;

        // Define the chunk processor as a std::function so it can re-schedule itself
        auto chunkProcessor = std::make_shared<std::function<void()>>();
        *chunkProcessor = [this, op, calendarId, entries, items, pos, total, hybrid, chunkProcessor]() {
            if (op->state() == SyncOperation::Cancelled) {
                delete entries;
                delete items;
                delete pos;
                emit fetchFinished(calendarId, false, QStringLiteral("Cancelled"));
                return;
            }

            int processed = 0;
            while (*pos < total && processed < CHUNK_SIZE) {
                const auto &[uid, entry] = entries->at(*pos);

                // Skip deletions (null value)
                if (!entry.value.isNull()) {
                    QString icalData = entry.value.toString();
                    QList<KCalendarCore::Incidence::Ptr> incidences = deserializeIcal(icalData);
                    for (const auto &inc : incidences) {
                        if (!hybrid && !isTypeAllowed(calendarId, inc)) {
                            CalendarType expected = discoveredCalendarType(calendarId);
                            CalendarType actual = incidenceCalendarType(inc);
                            qWarning() << "DecSyncBackend::fetchItems: Type violation -"
                                        << inc->uid() << "is"
                                        << (actual == CalendarType::Todo ? "VTODO" : "VEVENT")
                                        << "in" << calendarId << "which expects"
                                        << (expected == CalendarType::Todo ? "VTODO" : "VEVENT");
                            emit typeViolationDetected(calendarId, inc->uid(), expected, actual);
                        }

                        items->append(inc);
                        emit itemFetched(calendarId, inc);
                    }
                }

                (*pos)++;
                processed++;
                emit fetchProgressChanged(calendarId, *pos, total);
            }

            if (*pos < total) {
                // More items to process — yield and schedule next chunk
                QTimer::singleShot(0, this, *chunkProcessor);
            } else {
                // All done
                op->setFetchedItems(*items);
                op->complete();
                emit fetchFinished(calendarId, true);

                delete entries;
                delete items;
                delete pos;
            }
        };

        // Start the first chunk
        QTimer::singleShot(0, this, *chunkProcessor);
    });

    return op;
}

PushOperation* DecSyncBackend::pushItems(const QString &calendarId,
                                          const QList<KCalendarCore::Incidence::Ptr> &items)
{
    auto *op = new PushOperation(calendarId, items, this);
    registerOperation(op);

    QTimer::singleShot(0, this, [this, op, calendarId, items]() {
        if (op->state() == SyncOperation::Cancelled) return;
        op->setState(SyncOperation::Running);

        bool hybrid = isHybridCalendar(calendarId);

        if (!hybrid) {
            DecSyncCollection *coll = collectionFor(calendarId);
            if (!coll) {
                op->fail(QStringLiteral("No collection for calendar: %1").arg(calendarId));
                return;
            }
        }

        QList<DecSyncEntry> eventEntries;
        QList<DecSyncEntry> todoEntries;
        QStringList succeededUids;
        QStringList failedUids;
        QString dt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

        for (const auto &item : items) {
            if (item.isNull() || item->uid().isEmpty()) continue;

            if (!isTypeAllowed(calendarId, item)) {
                CalendarType expected = discoveredCalendarType(calendarId);
                CalendarType actual = incidenceCalendarType(item);
                qWarning() << "DecSyncBackend::pushItems: Type violation -"
                            << item->uid() << "is"
                            << (actual == CalendarType::Todo ? "VTODO" : "VEVENT")
                            << "but collection" << calendarId << "expects"
                            << (expected == CalendarType::Todo ? "VTODO" : "VEVENT");
                emit calendarError(QString(), calendarId,
                    QStringLiteral("Cannot push %1 to %2 collection: type mismatch")
                        .arg(item->uid(), expected == CalendarType::Todo
                             ? QStringLiteral("tasks") : QStringLiteral("calendars")));
                failedUids.append(item->uid());
                continue;
            }

            DecSyncEntry entry;
            entry.path = {QStringLiteral("resources"), item->uid()};
            entry.datetime = dt;
            entry.key = QJsonValue();
            entry.value = QJsonValue(serializeIncidence(item));

            if (hybrid && incidenceCalendarType(item) == CalendarType::Todo) {
                todoEntries.append(entry);
            } else {
                eventEntries.append(entry);
            }
            succeededUids.append(item->uid());
        }

        if (!eventEntries.isEmpty()) {
            DecSyncCollection *coll = hybrid ? ensureEventCollection(calendarId) : collectionFor(calendarId);
            if (coll) coll->setEntries(eventEntries);
        }
        if (!todoEntries.isEmpty()) {
            DecSyncCollection *todoColl = hybrid ? ensureTodoCollection(calendarId) : nullptr;
            if (todoColl) todoColl->setEntries(todoEntries);
        }
        op->setSucceededUids(succeededUids);
        op->complete();
    });

    return op;
}


DeleteOperation* DecSyncBackend::deleteItems(const QString &calendarId,
                                              const QStringList &uids)
{
    auto *op = new DeleteOperation(calendarId, uids, this);
    registerOperation(op);

    QTimer::singleShot(0, this, [this, op, calendarId, uids]() {
        if (op->state() == SyncOperation::Cancelled) return;
        op->setState(SyncOperation::Running);

        DecSyncCollection *coll = collectionFor(calendarId);
        DecSyncCollection *todoColl = todoCollectionFor(calendarId);

        if (!coll && !todoColl) {
            op->fail(QStringLiteral("No collection for calendar: %1").arg(calendarId));
            return;
        }

        QList<DecSyncEntry> entries;
        QStringList succeededUids;
        QString dt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

        for (const QString &uid : uids) {
            DecSyncEntry entry;
            entry.path = {QStringLiteral("resources"), uid};
            entry.datetime = dt;
            entry.key = QJsonValue();
            entry.value = QJsonValue();  // null = deleted
            entries.append(entry);
            succeededUids.append(uid);
        }

        if (coll) coll->setEntries(entries);
        if (todoColl) todoColl->setEntries(entries);

        op->setSucceededUids(succeededUids);
        op->complete();
    });

    return op;
}

// ============================================================================
// Calendar-Level CRUD
// ============================================================================

bool DecSyncBackend::supportsCalendarCreation() const
{
    return true;
}

bool DecSyncBackend::createCalendar(const QString &collectionId, const QString &calendarId,
                                     const QString &name, CalendarType type)
{
    if (calendarId.isEmpty()) {
        qWarning() << "DecSyncBackend::createCalendar: Empty calendar ID";
        return false;
    }

    // Determine sync type from CalendarType and calendarId
    QString syncType, collId;
    parseSyncId(calendarId, syncType, collId);

    // Hybrid: create tasks/ eagerly (so the calendar is discoverable on disk)
    // but defer calendars/ until VEVENTs actually arrive. This avoids empty
    // calendars/ directories for todo-only hybrids like "Waiting For".
    if (type == CalendarType::Hybrid) {
        if (syncType == QStringLiteral("tasks")) {
            // tasks/-prefixed ID requested as hybrid: fall back to Todo-only
            type = CalendarType::Todo;
            qDebug() << "DecSyncBackend::createCalendar: tasks/-prefixed ID"
                     << calendarId << "cannot be hybrid, creating as Todo";
        } else {
            m_hybridIds.insert(calendarId);

            // Create tasks/ side eagerly — ensures calendar is discoverable
            DecSyncCollection *taskColl = m_dir->openCollection(QStringLiteral("tasks"), collId, m_appId);
            taskColl->ensureDirectoryStructure();
            if (!name.isEmpty()) {
                taskColl->setEntry({QStringLiteral("info")},
                                   QJsonValue(QStringLiteral("name")),
                                   QJsonValue(name));
            }
            taskColl->setEntry({QStringLiteral("info")},
                               QJsonValue(QStringLiteral("deleted")),
                               QJsonValue(false));
            taskColl->setEntry({QStringLiteral("info")},
                               QJsonValue(QStringLiteral("hybrid")),
                               QJsonValue(true));

            QString todoCacheKey = QStringLiteral("~hybrid-tasks/") + collId;
            m_collections[todoCacheKey] = taskColl;

            // Store pending metadata for calendars/ side (created lazily)
            QVariantMap meta;
            if (!name.isEmpty()) {
                meta[QStringLiteral("name")] = name;
            }
            m_pendingHybridMeta[calendarId] = meta;

            qDebug() << "DecSyncBackend::createCalendar: Created hybrid tasks/"
                     << collId << "(calendars/ deferred until VEVENTs arrive)";
            return true;
        }
    }

    // Single-type: route to correct directory based on type
    if (type == CalendarType::Todo) {
        syncType = QStringLiteral("tasks");
    } else if (type == CalendarType::Event) {
        syncType = QStringLiteral("calendars");
    }

    DecSyncCollection *coll = m_dir->openCollection(syncType, collId, m_appId);
    coll->ensureDirectoryStructure();

    // Write info entries
    if (!name.isEmpty()) {
        coll->setEntry({QStringLiteral("info")},
                       QJsonValue(QStringLiteral("name")),
                       QJsonValue(name));
    }
    coll->setEntry({QStringLiteral("info")},
                   QJsonValue(QStringLiteral("deleted")),
                   QJsonValue(false));

    // Cache the collection
    m_collections[calendarId] = coll;

    qDebug() << "DecSyncBackend::createCalendar: Created" << syncType << "/" << collId;
    return true;
}

bool DecSyncBackend::updateCalendar(const QString &collectionId, const QString &calendarId,
                                     const QVariantMap &properties)
{
    Q_UNUSED(collectionId);

    DecSyncCollection *coll = collectionFor(calendarId);
    DecSyncCollection *todoColl = todoCollectionFor(calendarId);
    bool hybrid = isHybridCalendar(calendarId);

    if (!coll && !todoColl && !hybrid) {
        qWarning() << "DecSyncBackend::updateCalendar: No collection for" << calendarId;
        return false;
    }

    // For hybrid calendars with no dirs yet, update pending metadata
    if (hybrid && !coll && !todoColl) {
        if (!m_pendingHybridMeta.contains(calendarId)) {
            m_pendingHybridMeta[calendarId] = QVariantMap();
        }
        if (properties.contains(QStringLiteral("displayName"))) {
            m_pendingHybridMeta[calendarId][QStringLiteral("name")] =
                properties.value(QStringLiteral("displayName"));
        }
        if (properties.contains(QStringLiteral("color"))) {
            QColor color = properties.value(QStringLiteral("color")).value<QColor>();
            if (!color.isValid()) {
                color = QColor(properties.value(QStringLiteral("color")).toString());
            }
            if (color.isValid()) {
                m_pendingHybridMeta[calendarId][QStringLiteral("color")] = color.name();
            }
        }
        emit calendarUpdated(collectionId, calendarId);
        return true;
    }

    // Write to whichever collections exist on disk
    if (properties.contains(QStringLiteral("displayName"))) {
        QString nameVal = properties.value(QStringLiteral("displayName")).toString();
        if (coll) {
            coll->setEntry({QStringLiteral("info")},
                           QJsonValue(QStringLiteral("name")),
                           QJsonValue(nameVal));
        }
        if (todoColl) {
            todoColl->setEntry({QStringLiteral("info")},
                               QJsonValue(QStringLiteral("name")),
                               QJsonValue(nameVal));
        }
        // Also update pending metadata so future lazy-created dirs get the name
        if (hybrid && m_pendingHybridMeta.contains(calendarId)) {
            m_pendingHybridMeta[calendarId][QStringLiteral("name")] = nameVal;
        }
    }

    if (properties.contains(QStringLiteral("color"))) {
        QColor color = properties.value(QStringLiteral("color")).value<QColor>();
        if (!color.isValid()) {
            color = QColor(properties.value(QStringLiteral("color")).toString());
        }
        if (color.isValid()) {
            if (coll) {
                coll->setEntry({QStringLiteral("info")},
                               QJsonValue(QStringLiteral("color")),
                               QJsonValue(color.name()));
            }
            if (todoColl) {
                todoColl->setEntry({QStringLiteral("info")},
                                   QJsonValue(QStringLiteral("color")),
                                   QJsonValue(color.name()));
            }
            if (hybrid && m_pendingHybridMeta.contains(calendarId)) {
                m_pendingHybridMeta[calendarId][QStringLiteral("color")] = color.name();
            }
        }
    }

    emit calendarUpdated(collectionId, calendarId);
    return true;
}

bool DecSyncBackend::deleteCalendar(const QString &collectionId, const QString &calendarId)
{
    Q_UNUSED(collectionId);

    DecSyncCollection *coll = collectionFor(calendarId);
    DecSyncCollection *todoColl = todoCollectionFor(calendarId);
    bool hybrid = isHybridCalendar(calendarId);

    // For hybrid calendars with no dirs yet, just clean up in-memory state
    if (!coll && !todoColl) {
        if (hybrid) {
            m_hybridIds.remove(calendarId);
            m_pendingHybridMeta.remove(calendarId);
            qDebug() << "DecSyncBackend::deleteCalendar: Removed deferred hybrid" << calendarId;
            return true;
        }
        qWarning() << "DecSyncBackend::deleteCalendar: No collection for" << calendarId;
        return false;
    }

    // Soft delete whichever collections exist
    if (coll) {
        coll->setEntry({QStringLiteral("info")},
                       QJsonValue(QStringLiteral("deleted")),
                       QJsonValue(true));
    }
    if (todoColl) {
        todoColl->setEntry({QStringLiteral("info")},
                           QJsonValue(QStringLiteral("deleted")),
                           QJsonValue(true));
    }

    if (hybrid) {
        m_hybridIds.remove(calendarId);
        m_pendingHybridMeta.remove(calendarId);
        qDebug() << "DecSyncBackend::deleteCalendar: Soft-deleted hybrid"
                 << calendarId << "(dirs:" << (coll ? "calendars/" : "")
                 << (todoColl ? "tasks/" : "") << ")";
    } else {
        qDebug() << "DecSyncBackend::deleteCalendar: Soft-deleted" << calendarId;
    }

    return true;
}

// ============================================================================
// Calendar Property Discovery
// ============================================================================

CalendarType DecSyncBackend::discoveredCalendarType(const QString &calendarId) const
{
    QString syncType, collId;
    parseSyncId(calendarId, syncType, collId);

    if (syncType == QStringLiteral("tasks")) {
        return CalendarType::Todo;
    }

    // Check if this is a hybrid calendar (both calendars/ and tasks/ exist)
    if (isHybridCalendar(calendarId)) {
        return CalendarType::Hybrid;
    }

    return CalendarType::Event;
}

QColor DecSyncBackend::discoveredColor(const QString &calendarId) const
{
    return calendarColor(calendarId);
}

QString DecSyncBackend::discoveredDisplayName(const QString &calendarId) const
{
    QString syncType, collId;
    parseSyncId(calendarId, syncType, collId);

    // Try primary collection (calendars/ for bare IDs)
    QString collDir = m_dir->decsyncDir() + QStringLiteral("/") + syncType
                      + QStringLiteral("/") + collId;
    if (QDir(collDir).exists()) {
        QMap<QString, QJsonValue> info = m_dir->getStaticInfo(syncType, collId);
        QString name = info.value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) return name;
    }

    // For hybrid calendars, try the tasks/ side
    if (isHybridCalendar(calendarId) && syncType != QStringLiteral("tasks")) {
        QString taskDir = m_dir->decsyncDir() + QStringLiteral("/tasks/") + collId;
        if (QDir(taskDir).exists()) {
            QMap<QString, QJsonValue> taskInfo = m_dir->getStaticInfo(QStringLiteral("tasks"), collId);
            QString name = taskInfo.value(QStringLiteral("name")).toString();
            if (!name.isEmpty()) return name;
        }
    }

    // Fall back to pending metadata for deferred hybrid calendars
    if (m_pendingHybridMeta.contains(calendarId)) {
        return m_pendingHybridMeta[calendarId].value(QStringLiteral("name")).toString();
    }

    return QString();
}

QColor DecSyncBackend::calendarColor(const QString &calendarId) const
{
    QString syncType, collId;
    parseSyncId(calendarId, syncType, collId);

    // Try primary collection (calendars/ for bare IDs)
    QString collDir = m_dir->decsyncDir() + QStringLiteral("/") + syncType
                      + QStringLiteral("/") + collId;
    if (QDir(collDir).exists()) {
        QMap<QString, QJsonValue> info = m_dir->getStaticInfo(syncType, collId);
        QString colorStr = info.value(QStringLiteral("color")).toString();
        if (!colorStr.isEmpty()) return QColor(colorStr);
    }

    // For hybrid calendars, try the tasks/ side
    if (isHybridCalendar(calendarId) && syncType != QStringLiteral("tasks")) {
        QString taskDir = m_dir->decsyncDir() + QStringLiteral("/tasks/") + collId;
        if (QDir(taskDir).exists()) {
            QMap<QString, QJsonValue> taskInfo = m_dir->getStaticInfo(QStringLiteral("tasks"), collId);
            QString colorStr = taskInfo.value(QStringLiteral("color")).toString();
            if (!colorStr.isEmpty()) return QColor(colorStr);
        }
    }

    // Fall back to pending metadata for deferred hybrid calendars
    if (m_pendingHybridMeta.contains(calendarId)) {
        QString colorStr = m_pendingHybridMeta[calendarId].value(QStringLiteral("color")).toString();
        if (!colorStr.isEmpty()) return QColor(colorStr);
    }

    return QColor();
}

// ============================================================================
// Capabilities
// ============================================================================

BackendCapabilities DecSyncBackend::capabilities() const
{
    return BackendCapabilities::decsyncDefaults();
}

// ============================================================================
// Binding Metadata
// ============================================================================

QStringList DecSyncBackend::bindingMetadataKeys() const
{
    return {QStringLiteral("decsyncDir"), QStringLiteral("syncType"), QStringLiteral("collectionId")};
}

void DecSyncBackend::populateBindingMetadata(
    const DiscoveredCalendar &discovered,
    CalendarBackendBinding &binding) const
{
    QString syncType, collId;
    parseSyncId(discovered.calendarId, syncType, collId);

    binding.setMetadata(QStringLiteral("decsyncDir"), m_dir->decsyncDir());
    binding.setMetadata(QStringLiteral("syncType"), syncType);
    binding.setMetadata(QStringLiteral("collectionId"), collId);
}

void DecSyncBackend::prepareCreationMetadata(
    const QString &calendarId,
    CalendarBackendBinding &binding) const
{
    QString syncType, collId;
    parseSyncId(calendarId, syncType, collId);

    binding.setMetadata(QStringLiteral("decsyncDir"), m_dir->decsyncDir());
    binding.setMetadata(QStringLiteral("syncType"), syncType);
    binding.setMetadata(QStringLiteral("collectionId"), collId);
}

// ============================================================================
// Raw ICS Access
// ============================================================================

QString DecSyncBackend::getRawIcs(const QString &calendarId, const QString &uid) const
{
    DecSyncCollection *coll = collectionFor(calendarId);
    if (coll) {
        QMap<QString, DecSyncEntry> resources = coll->readAllResources();
        if (resources.contains(uid)) {
            return resources[uid].value.toString();
        }
    }

    // For hybrid calendars, also check the tasks/ companion
    DecSyncCollection *todoColl = todoCollectionFor(calendarId);
    if (todoColl) {
        QMap<QString, DecSyncEntry> todoResources = todoColl->readAllResources();
        if (todoResources.contains(uid)) {
            return todoResources[uid].value.toString();
        }
    }

    return QString();
}

bool DecSyncBackend::setRawIcs(const QString &calendarId, const QString &uid,
                                const QString &icsContent)
{
    // For hybrid calendars, determine which collection to write to by parsing the ICS.
    // If it contains VTODO, write to tasks/ companion; otherwise, calendars/.
    if (isHybridCalendar(calendarId)) {
        QList<KCalendarCore::Incidence::Ptr> incidences = deserializeIcal(icsContent);
        bool isTodo = false;
        for (const auto &inc : incidences) {
            if (incidenceCalendarType(inc) == CalendarType::Todo) {
                isTodo = true;
                break;
            }
        }

        DecSyncCollection *coll = isTodo ? ensureTodoCollection(calendarId) : ensureEventCollection(calendarId);
        if (!coll) return false;

        coll->setEntry({QStringLiteral("resources"), uid},
                       QJsonValue(),
                       QJsonValue(icsContent));
        return true;
    }

    DecSyncCollection *coll = collectionFor(calendarId);
    if (!coll) return false;

    coll->setEntry({QStringLiteral("resources"), uid},
                   QJsonValue(),
                   QJsonValue(icsContent));
    return true;
}

// ============================================================================
// Private Helpers
// ============================================================================

void DecSyncBackend::parseSyncId(const QString &calendarId,
                                  QString &syncType, QString &collectionId) const
{
    if (calendarId.startsWith(QStringLiteral("tasks/"))) {
        syncType = QStringLiteral("tasks");
        collectionId = calendarId.mid(6);  // strlen("tasks/") == 6
    } else {
        syncType = QStringLiteral("calendars");
        collectionId = calendarId;
    }
}

DecSyncCollection* DecSyncBackend::collectionFor(const QString &calendarId) const
{
    if (m_collections.contains(calendarId)) {
        return m_collections[calendarId];
    }

    QString syncType, collId;
    parseSyncId(calendarId, syncType, collId);

    // Check that the collection directory exists
    QString collDir = m_dir->decsyncDir() + QStringLiteral("/") + syncType
                      + QStringLiteral("/") + collId;
    if (!QDir(collDir).exists()) {
        return nullptr;
    }

    DecSyncCollection *coll = m_dir->openCollection(syncType, collId, m_appId);
    m_collections[calendarId] = coll;
    return coll;
}

bool DecSyncBackend::isTypeAllowed(const QString &calendarId,
                                    const KCalendarCore::Incidence::Ptr &incidence) const
{
    if (incidence.isNull()) return false;

    CalendarType actual = incidenceCalendarType(incidence);

    // Journals are never allowed in DecSync collections
    if (actual != CalendarType::Event && actual != CalendarType::Todo)
        return false;

    // Hybrid calendars accept both events and todos
    if (isHybridCalendar(calendarId))
        return true;

    CalendarType expected = discoveredCalendarType(calendarId);
    if (expected == actual)
        return true;

    // Auto-promote to hybrid: if a calendar receives items of a different type,
    // create the companion collection and mark as hybrid. This handles the common
    // case of CalDAV hybrid calendars syncing to DecSync, where events and todos
    // coexist in the same logical calendar but DecSync stores them in separate dirs.
    // TODO: Consider adding a "demotion" UX that allows users to move or purge
    // non-compliant items if the promotion was unintended, restoring single-type status.
    if (!calendarId.startsWith(QStringLiteral("tasks/"))) {
        qDebug() << "DecSyncBackend: Auto-promoting" << calendarId
                 << "to hybrid (received"
                 << (actual == CalendarType::Todo ? "VTODO" : "VEVENT")
                 << "in"
                 << (expected == CalendarType::Todo ? "VTODO" : "VEVENT")
                 << "collection)";
        m_hybridIds.insert(calendarId);

        // Ensure the companion collection exists
        auto *mutableThis = const_cast<DecSyncBackend*>(this);
        if (actual == CalendarType::Todo) {
            mutableThis->ensureTodoCollection(calendarId);
        } else {
            mutableThis->ensureEventCollection(calendarId);
        }
        return true;
    }

    return false;
}

bool DecSyncBackend::isHybridCalendar(const QString &calendarId) const
{
    // Already known hybrid
    if (m_hybridIds.contains(calendarId))
        return true;

    // Only bare IDs (calendars/ convention) can be hybrid.
    // tasks/-prefixed IDs are standalone task collections.
    if (calendarId.startsWith(QStringLiteral("tasks/")))
        return false;

    // Check if both calendars/X and tasks/X directories exist
    QString calDir = m_dir->decsyncDir() + QStringLiteral("/calendars/") + calendarId;
    QString taskDir = m_dir->decsyncDir() + QStringLiteral("/tasks/") + calendarId;

    if (QDir(calDir).exists() && QDir(taskDir).exists()) {
        m_hybridIds.insert(calendarId);
        return true;
    }

    return false;
}

DecSyncCollection* DecSyncBackend::todoCollectionFor(const QString &calendarId) const
{
    if (!isHybridCalendar(calendarId))
        return nullptr;

    // The todo companion uses the key "~hybrid-tasks/calendarId" in our cache
    // to avoid colliding with standalone "tasks/calendarId" entries
    QString cacheKey = QStringLiteral("~hybrid-tasks/") + calendarId;

    if (m_collections.contains(cacheKey))
        return m_collections[cacheKey];

    // Only open if the directory actually exists on disk
    QString taskDir = m_dir->decsyncDir() + QStringLiteral("/tasks/") + calendarId;
    if (!QDir(taskDir).exists())
        return nullptr;

    DecSyncCollection *coll = m_dir->openCollection(QStringLiteral("tasks"), calendarId, m_appId);
    m_collections[cacheKey] = coll;
    return coll;
}

DecSyncCollection* DecSyncBackend::ensureEventCollection(const QString &calendarId)
{
    // Return existing if the dir already exists
    DecSyncCollection *existing = collectionFor(calendarId);
    if (existing) return existing;

    QString syncType, collId;
    parseSyncId(calendarId, syncType, collId);

    DecSyncCollection *coll = m_dir->openCollection(QStringLiteral("calendars"), collId, m_appId);
    coll->ensureDirectoryStructure();

    writePendingMetadata(coll, calendarId);
    coll->setEntry({QStringLiteral("info")},
                   QJsonValue(QStringLiteral("deleted")),
                   QJsonValue(false));
    coll->setEntry({QStringLiteral("info")},
                   QJsonValue(QStringLiteral("hybrid")),
                   QJsonValue(true));

    m_collections[calendarId] = coll;
    return coll;
}

DecSyncCollection* DecSyncBackend::ensureTodoCollection(const QString &calendarId)
{
    // Return existing if the dir already exists
    DecSyncCollection *existing = todoCollectionFor(calendarId);
    if (existing) return existing;

    DecSyncCollection *coll = m_dir->openCollection(QStringLiteral("tasks"), calendarId, m_appId);
    coll->ensureDirectoryStructure();

    writePendingMetadata(coll, calendarId);
    coll->setEntry({QStringLiteral("info")},
                   QJsonValue(QStringLiteral("deleted")),
                   QJsonValue(false));
    coll->setEntry({QStringLiteral("info")},
                   QJsonValue(QStringLiteral("hybrid")),
                   QJsonValue(true));

    QString cacheKey = QStringLiteral("~hybrid-tasks/") + calendarId;
    m_collections[cacheKey] = coll;
    return coll;
}

void DecSyncBackend::writePendingMetadata(DecSyncCollection *coll, const QString &calendarId)
{
    if (!m_pendingHybridMeta.contains(calendarId)) return;

    const QVariantMap &meta = m_pendingHybridMeta[calendarId];
    if (meta.contains(QStringLiteral("name"))) {
        coll->setEntry({QStringLiteral("info")},
                       QJsonValue(QStringLiteral("name")),
                       QJsonValue(meta.value(QStringLiteral("name")).toString()));
    }
    if (meta.contains(QStringLiteral("color"))) {
        coll->setEntry({QStringLiteral("info")},
                       QJsonValue(QStringLiteral("color")),
                       QJsonValue(meta.value(QStringLiteral("color")).toString()));
    }
}

CalendarType DecSyncBackend::incidenceCalendarType(const KCalendarCore::Incidence::Ptr &incidence)
{
    if (incidence.isNull()) return CalendarType::Event;

    if (incidence->type() == KCalendarCore::Incidence::TypeTodo)
        return CalendarType::Todo;

    return CalendarType::Event;
}

QString DecSyncBackend::serializeIncidence(const KCalendarCore::Incidence::Ptr &incidence) const
{
    KCalendarCore::ICalFormat format;
    auto tempCal = QSharedPointer<KCalendarCore::MemoryCalendar>(
        new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone())
    );
    tempCal->addIncidence(incidence);
    return format.toString(tempCal);
}

QList<KCalendarCore::Incidence::Ptr> DecSyncBackend::deserializeIcal(const QString &icalData) const
{
    QList<KCalendarCore::Incidence::Ptr> result;
    if (icalData.isEmpty()) return result;

    KCalendarCore::ICalFormat format;
    auto tempCal = QSharedPointer<KCalendarCore::MemoryCalendar>(
        new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone())
    );

    if (!format.fromRawString(tempCal, icalData.toUtf8())) {
        qWarning() << "DecSyncBackend: Failed to parse iCalendar data";
        return result;
    }

    return tempCal->incidences();
}



// ============================================================================
// IBlobBackend implementation (Phase D Task 16)
//
// recordId     = uid (key in DecSync "resources" map)
// collectionId = calendarId (maps to a DecSync collection)
// data         = raw iCal bytes (DecSync stores iCal strings natively)
// contentHash  = SHA-256 of the iCal bytes
// lastModified = parsed from DecSyncEntry::datetime (ISO 8601)
// ============================================================================

namespace {

/// Build a BackendRecord from a DecSync uid + iCal string + timestamp.
static Kalburator::Sync::BackendRecord decSyncBlobRecord(
    const QString &uid,
    const QString &icalStr,
    const QString &datetimeIso)
{
    const QByteArray bytes = icalStr.toUtf8();
    Kalburator::Sync::BackendRecord rec;
    rec.id          = uid;
    rec.type        = QStringLiteral("calendar");
    rec.data        = bytes;
    rec.contentHash = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    rec.lastModified = QDateTime::fromString(datetimeIso, Qt::ISODate);
    if (!rec.lastModified.isValid())
        rec.lastModified = QDateTime::currentDateTimeUtc();
    rec.isDeleted   = false;
    return rec;
}

} // anonymous namespace

// --- Identity ---------------------------------------------------------------

QString DecSyncBackend::backendId() const
{
    const QString dir = m_dir ? m_dir->decsyncDir() : QString();
    const QByteArray h = QCryptographicHash::hash(
        (BackendTypeName + QLatin1Char(':') + dir + QLatin1Char(':') + m_appId).toUtf8(),
        QCryptographicHash::Sha256);
    return BackendTypeName + QLatin1Char(':') + QString::fromLatin1(h.toHex().left(16));
}

QString DecSyncBackend::displayName() const
{
    const QString dir = m_dir ? m_dir->decsyncDir() : QString();
    return QStringLiteral("DecSyncBackend(%1)").arg(dir);
}

bool DecSyncBackend::isAvailable() const
{
    if (!m_dir) return false;
    return QDir(m_dir->decsyncDir()).exists();
}

// --- Collections ------------------------------------------------------------

QList<CollectionInfo> DecSyncBackend::availableCollections()
{
    QList<CollectionInfo> result;
    if (!m_dir) return result;

    // Mirror loadCalendars() discovery logic.
    const QStringList calendars = m_dir->listCollections(QStringLiteral("calendars"));
    for (const QString &calId : calendars) {
        QMap<QString, QJsonValue> info = m_dir->getStaticInfo(QStringLiteral("calendars"), calId);
        if (info.value(QStringLiteral("deleted")).toBool(false)) continue;

        CollectionInfo ci;
        ci.id   = calId;
        ci.name = info.value(QStringLiteral("name")).toString(calId);
        ci.type = QStringLiteral("calendar");
        result.append(ci);
    }

    const QStringList tasks = m_dir->listCollections(QStringLiteral("tasks"));
    for (const QString &taskId : tasks) {
        QMap<QString, QJsonValue> info = m_dir->getStaticInfo(QStringLiteral("tasks"), taskId);
        if (info.value(QStringLiteral("deleted")).toBool(false)) continue;
        if (info.value(QStringLiteral("hybrid")).toBool(false)) continue;

        CollectionInfo ci;
        ci.id   = QStringLiteral("tasks/") + taskId;
        ci.name = info.value(QStringLiteral("name")).toString(taskId);
        ci.type = QStringLiteral("calendar");
        result.append(ci);
    }

    return result;
}

CollectionInfo DecSyncBackend::collectionInfo(const QString &collectionId)
{
    CollectionInfo info;
    info.id   = collectionId;
    info.name = collectionId;
    info.type = QStringLiteral("calendar");
    return info;
}

QString DecSyncBackend::createCollection(const CollectionInfo &info)
{
    if (createCalendar(QString(), info.id, info.name.isEmpty() ? info.id : info.name))
        return info.id;
    return {};
}

// --- Records ----------------------------------------------------------------

QList<BackendRecord> DecSyncBackend::loadRecords(const QString &collectionId)
{
    QList<BackendRecord> result;
    if (collectionId.isEmpty()) return result;

    auto loadFromColl = [&](DecSyncCollection *coll) {
        if (!coll) return;
        const QMap<QString, DecSyncEntry> resources = coll->readAllResources();
        for (auto it = resources.constBegin(); it != resources.constEnd(); ++it) {
            const QString icalStr = it.value().value.toString();
            if (icalStr.isEmpty()) continue;
            result.append(decSyncBlobRecord(it.key(), icalStr, it.value().datetime));
        }
    };

    loadFromColl(collectionFor(collectionId));
    loadFromColl(todoCollectionFor(collectionId));

    return result;
}

std::optional<BackendRecord> DecSyncBackend::loadRecord(const QString &recordId)
{
    // Search all known collections for this uid.
    if (recordId.isEmpty()) return std::nullopt;

    const QStringList calIds = m_collections.keys();
    for (const QString &calId : calIds) {
        DecSyncCollection *coll = m_collections.value(calId);
        if (!coll) continue;
        const QMap<QString, DecSyncEntry> resources = coll->readAllResources();
        if (!resources.contains(recordId)) continue;
        const DecSyncEntry &entry = resources.value(recordId);
        const QString icalStr = entry.value.toString();
        if (icalStr.isEmpty()) continue;
        return decSyncBlobRecord(recordId, icalStr, entry.datetime);
    }
    return std::nullopt;
}

QString DecSyncBackend::createRecord(const QString &collectionId,
                                      const BackendRecord &record)
{
    if (collectionId.isEmpty() || record.id.isEmpty() || record.data.isEmpty())
        return {};

    DecSyncCollection *coll = collectionFor(collectionId);
    if (!coll) {
        qWarning() << "DecSyncBackend::createRecord: no collection for" << collectionId;
        return {};
    }

    const QString icalStr = QString::fromUtf8(record.data);
    coll->setEntry({QStringLiteral("resources"), record.id},
                   QJsonValue(),
                   QJsonValue(icalStr));
    return record.id;
}

bool DecSyncBackend::updateRecord(const BackendRecord &record)
{
    if (record.id.isEmpty() || record.data.isEmpty()) return false;

    // Find which collection owns this uid.
    for (auto it = m_collections.constBegin(); it != m_collections.constEnd(); ++it) {
        DecSyncCollection *coll = it.value();
        if (!coll) continue;
        const QMap<QString, DecSyncEntry> resources = coll->readAllResources();
        if (!resources.contains(record.id)) continue;

        const QString icalStr = QString::fromUtf8(record.data);
        coll->setEntry({QStringLiteral("resources"), record.id},
                       QJsonValue(),
                       QJsonValue(icalStr));
        return true;
    }
    qWarning() << "DecSyncBackend::updateRecord: uid not found in any collection:" << record.id;
    return false;
}

bool DecSyncBackend::deleteRecord(const QString &recordId)
{
    if (recordId.isEmpty()) return false;

    for (auto it = m_collections.constBegin(); it != m_collections.constEnd(); ++it) {
        DecSyncCollection *coll = it.value();
        if (!coll) continue;
        const QMap<QString, DecSyncEntry> resources = coll->readAllResources();
        if (!resources.contains(recordId)) continue;

        // Write null value = deleted
        coll->setEntry({QStringLiteral("resources"), recordId},
                       QJsonValue(),
                       QJsonValue());
        return true;
    }
    return false;
}

// --- Change detection -------------------------------------------------------

QList<BackendRecord> DecSyncBackend::modifiedSince(const QString &collectionId,
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

QStringList DecSyncBackend::deletedSince(const QString &collectionId,
                                          const QDateTime &since)
{
    // DecSync marks deletions as null values in the resources map.
    // Scan per-app resources to find entries written after `since` with null value.
    QStringList result;
    if (collectionId.isEmpty()) return result;

    DecSyncCollection *coll = collectionFor(collectionId);
    if (!coll) return result;

    const auto perApp = coll->readPerAppResources();
    for (auto appIt = perApp.constBegin(); appIt != perApp.constEnd(); ++appIt) {
        for (auto it = appIt->constBegin(); it != appIt->constEnd(); ++it) {
            const DecSyncEntry &entry = it.value();
            if (!entry.value.isNull()) continue;  // not a deletion
            if (since.isValid()) {
                const QDateTime entryDt = QDateTime::fromString(entry.datetime, Qt::ISODate);
                if (entryDt.isValid() && entryDt <= since) continue;
            }
            if (!result.contains(it.key()))
                result.append(it.key());
        }
    }
    return result;
}

} // namespace Kalburator::Sync
