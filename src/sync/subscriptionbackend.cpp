#include "subscriptionbackend.h"
#include "backendcapabilities.h"
#include "syncoperation.h"
#include <KCalendarCore/ICalFormat>
#include <QDebug>
#include <QTimer>

namespace Kalburator::Sync {

const QString SubscriptionBackend::BackendTypeName = QStringLiteral("subscription");

QString SubscriptionBackend::backendType() const { return BackendTypeName; }

SubscriptionBackend::SubscriptionBackend(QObject *parent)
    : SyncBackend(parent)
{
}

void SubscriptionBackend::loadCalendars(const QString &collectionId)
{
    m_currentCollectionId = collectionId;

    // Each source creates a separate calendar
    for (const QString &sourceId : m_sources.keys()) {
        emit calendarDiscovered(collectionId, sourceId);
    }
}

void SubscriptionBackend::loadItems(KCalendarCore::MemoryCalendar* cal, bool suppressSignals)
{
    qDebug() << "=== SubscriptionBackend::loadItems called ===";
    qDebug() << "  Calendar ID:" << (cal ? cal->id() : "null");
    qDebug() << "  Suppress signals:" << suppressSignals;

    if (!cal) {
        qWarning() << "SubscriptionBackend::loadItems: null calendar pointer";
        return;
    }

    const QString calendarId = cal->id();
    qDebug() << "  Available sources:" << m_sources.keys();

    if (!m_sources.contains(calendarId)) {
        qWarning() << "SubscriptionBackend::loadItems: unknown source" << calendarId;
        emit calendarLoaded(cal);
        return;
    }

    // Fetch events for a reasonable time range
    // For initial load, fetch 1 year in the past and 2 years in the future
    const QDate today = QDate::currentDate();
    const QDate startDate = today.addYears(-1);
    const QDate endDate = today.addYears(2);

    qDebug() << "  Fetching events from" << startDate << "to" << endDate;
    QList<KCalendarCore::Incidence::Ptr> events = fetchEventsForSource(calendarId, startDate, endDate);
    qDebug() << "  Fetched" << events.size() << "events";

    // Add events to calendar
    for (const auto &event : events) {
        if (event) {
            // Ensure event is marked read-only
            event->setReadOnly(true);

            // Add to calendar
            cal->addIncidence(event);

            // Emit signal if not suppressed
            if (!suppressSignals) {
                // Generate a version identifier (could use last-modified or hash)
                QString versionId = event->lastModified().toString(Qt::ISODate);
                if (versionId.isEmpty()) {
                    versionId = QStringLiteral("subscription-v1");
                }
                emit itemLoaded(cal, event, versionId);
            }
        }
    }

    emit calendarLoaded(cal);
}

FetchOperation* SubscriptionBackend::fetchItems(const QString &calendarId)
{
    auto *op = new FetchOperation(calendarId, this);
    registerOperation(op);

    // Use deferred execution so signals can be connected before firing
    QTimer::singleShot(0, this, [this, op, calendarId]() {
        if (op->state() == SyncOperation::Cancelled) {
            return;
        }

        op->setState(SyncOperation::Running);

        if (!m_sources.contains(calendarId)) {
            QString errorMsg = QStringLiteral("Unknown subscription source: %1").arg(calendarId);
            qWarning() << "SubscriptionBackend::fetchItems:" << errorMsg;
            op->fail(errorMsg);
            emit fetchFinished(calendarId, false, errorMsg);
            return;
        }

        // Fetch events for a reasonable time range
        const QDate today = QDate::currentDate();
        const QDate startDate = today.addYears(-1);
        const QDate endDate = today.addYears(2);

        qDebug() << "SubscriptionBackend::fetchItems: Fetching events for" << calendarId
                 << "from" << startDate << "to" << endDate;

        QList<KCalendarCore::Incidence::Ptr> events = fetchEventsForSource(calendarId, startDate, endDate);

        qDebug() << "SubscriptionBackend::fetchItems: Got" << events.size() << "events for" << calendarId;

        // Emit itemFetched for each event (same pattern as LocalBackend)
        for (const auto &event : events) {
            if (event) {
                event->setReadOnly(true);
                emit itemFetched(calendarId, event);
            }
        }

        op->setFetchedItems(events);
        op->complete();
        emit fetchFinished(calendarId, true);
    });

    return op;
}

void SubscriptionBackend::storeCalendars(const QString &collectionId,
                                         const QList<KCalendarCore::MemoryCalendar*> &calendars)
{
    Q_UNUSED(collectionId);
    Q_UNUSED(calendars);
    // Read-only backend - do nothing
    qDebug() << "SubscriptionBackend::storeCalendars: no-op (read-only backend)";
}

void SubscriptionBackend::storeItems(KCalendarCore::MemoryCalendar* cal,
                                     const QList<KCalendarCore::Incidence::Ptr> &items)
{
    Q_UNUSED(cal);
    Q_UNUSED(items);
    // Read-only backend - do nothing
    qDebug() << "SubscriptionBackend::storeItems: no-op (read-only backend)";
}

void SubscriptionBackend::updateItem(KCalendarCore::MemoryCalendar* cal,
                                     const KCalendarCore::Incidence::Ptr &item,
                                     const QString &icalData)
{
    Q_UNUSED(cal);
    Q_UNUSED(item);
    Q_UNUSED(icalData);
    // Read-only backend - do nothing
    qDebug() << "SubscriptionBackend::updateItem: no-op (read-only backend)";
}

void SubscriptionBackend::startSync(const QString &collectionId,
                                    KCalendarCore::MemoryCalendar* calendar,
                                    const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                                    const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                                    const QMap<QString, QString> &stagedDeletions)
{
    Q_UNUSED(calendar);
    Q_UNUSED(stagedCreations);
    Q_UNUSED(stagedUpdates);
    Q_UNUSED(stagedDeletions);
    // Read-only backend - just emit sync completed
    qDebug() << "SubscriptionBackend::startSync: no-op (read-only backend)";
    emit syncCompleted(collectionId);
}

void SubscriptionBackend::removeItem(const QString &calId, const QString &itemUid)
{
    Q_UNUSED(calId);
    Q_UNUSED(itemUid);
    // Read-only backend - do nothing
    qDebug() << "SubscriptionBackend::removeItem: no-op (read-only backend)";
}

bool SubscriptionBackend::discoveredWritable(const QString &calendarId) const
{
    Q_UNUSED(calendarId);
    // Subscription calendars are always read-only
    return false;
}

BackendCapabilities SubscriptionBackend::capabilities() const
{
    BackendCapabilities caps;
    caps.backendType = QStringLiteral("subscription");
    caps.displayName = QObject::tr("Subscription Calendar");
    caps.description = QObject::tr("Read-only calendars from external sources (holidays, webcal feeds)");

    // Full incidence support (read-only, but can contain any type)
    caps.incidenceSupport.supportsEvents = true;
    caps.incidenceSupport.supportsTodos = false;  // Holidays are typically events
    caps.incidenceSupport.supportsJournals = false;
    caps.incidenceSupport.supportsHybrid = false;

    // Full recurrence support (holidays may have recurrence rules)
    caps.recurrence.supportsDaily = true;
    caps.recurrence.supportsWeekly = true;
    caps.recurrence.supportsMonthly = true;
    caps.recurrence.supportsYearly = true;
    caps.recurrence.backendType = "subscription";
    caps.recurrence.displayName = "Subscription Calendar";

    // Property support (read-only, but supports all standard properties)
    // Use defaults (all true)

    // No structural capabilities
    caps.structural.supportsHierarchy = false;

    // Calendar CRUD: all disabled (read-only)
    caps.calendarCrud.supportsCreate = false;
    caps.calendarCrud.supportsDelete = false;
    caps.calendarCrud.supportsRename = false;
    caps.calendarCrud.supportsColor = false;
    caps.calendarCrud.supportsDescription = false;
    caps.calendarCrud.supportsOrder = false;

    // Sync characteristics: no sync (read-only)
    caps.syncCharacteristics.supportsDeltaSync = false;
    caps.syncCharacteristics.supportsEtags = false;
    caps.syncCharacteristics.supportsBatching = false;
    caps.syncCharacteristics.requiresFullFetch = false;

    return caps;
}

void SubscriptionBackend::addSource(const QString &sourceId, const QString &sourceType, const QVariantMap &config)
{
    SourceInfo info;
    info.sourceId = sourceId;
    info.sourceType = sourceType;
    info.config = config;

    m_sources[sourceId] = info;

    // If we already have a collection loaded, discover this new calendar
    if (!m_currentCollectionId.isEmpty()) {
        emit calendarDiscovered(m_currentCollectionId, sourceId);
    }
}

void SubscriptionBackend::removeSource(const QString &sourceId)
{
    m_sources.remove(sourceId);
}

QStringList SubscriptionBackend::sources() const
{
    return m_sources.keys();
}

QString SubscriptionBackend::sourceType(const QString &sourceId) const
{
    if (m_sources.contains(sourceId)) {
        return m_sources[sourceId].sourceType;
    }
    return QString();
}

QVariantMap SubscriptionBackend::sourceConfig(const QString &sourceId) const
{
    if (m_sources.contains(sourceId)) {
        return m_sources[sourceId].config;
    }
    return QVariantMap();
}

QString SubscriptionBackend::sourceDisplayName(const QString &sourceId) const
{
    // Default implementation - subclasses can override
    if (m_sources.contains(sourceId)) {
        // Check if config has a display name
        QVariantMap config = m_sources[sourceId].config;
        if (config.contains("name")) {
            return config["name"].toString();
        }
    }
    return sourceId;
}


} // namespace Kalburator::Sync
