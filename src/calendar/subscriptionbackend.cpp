#include "subscriptionbackend.h"
#include "backendcapabilities.h"
#include "syncoperation.h"
#include "transcodingplan.h"
#include <KCalendarCore/ICalFormat>
#include <QCryptographicHash>
#include <QDebug>
#include <QTimer>

namespace Kalburator::Sync {

const QString SubscriptionBackend::BackendTypeName = QStringLiteral("subscription");

QString SubscriptionBackend::backendType() const { return BackendTypeName; }

QList<Kalburator::Shape::Shape> SubscriptionBackend::nativeShapes() const
{
    return { Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("calendar")},
        Kalburator::Shape::EncodingId{QStringLiteral("ical")} } };
}

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

void SubscriptionBackend::startSync(const QString &collectionId,
                                    KCalendarCore::MemoryCalendar* calendar,
                                    const QList<KCalendarCore::Incidence::Ptr>& stagedCreations,
                                    const QList<KCalendarCore::Incidence::Ptr>& stagedUpdates,
                                    const QMap<QString, QString>& stagedDeletions,
                                    const TranscodingPlan& plan)
{
    Q_UNUSED(calendar);
    Q_UNUSED(stagedCreations);
    Q_UNUSED(stagedUpdates);
    Q_UNUSED(stagedDeletions);
    Q_UNUSED(plan);
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

PushOperation* SubscriptionBackend::pushItems(const QString &calendarId,
                                              const QList<KCalendarCore::Incidence::Ptr> &items,
                                              const TranscodingPlan &plan)
{
    // F2 Task 12: read-only backend; pushes are always rejected.
    // The transcoding plan is intentionally ignored — items never
    // reach storage so there's nothing to transcode and no warnings
    // to emit. Behaviour mirrors the previous storeItems() / base
    // pushItems() rejection paths. HolidaySubscriptionBackend
    // inherits this override unchanged.
    Q_UNUSED(plan);
    auto *op = new PushOperation(calendarId, items, this);
    registerOperation(op);
    QTimer::singleShot(0, op, [op]() {
        op->fail(QStringLiteral("read-only backend: pushItems rejected"));
    });
    return op;
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

// =============================================================================
// IBlobBackend implementation (Phase D Task 17) — read-only subscription view
// =============================================================================

namespace {

/// Build a BackendRecord from a single incidence serialised to iCal.
/// Uses toICalString() to produce a complete VCALENDAR-wrapped payload so
/// that the stored blob can be fed back into any compliant iCal parser.
static BackendRecord subscriptionBlobRecord(const KCalendarCore::Incidence::Ptr &inc)
{
    KCalendarCore::ICalFormat fmt;
    const QByteArray ical = fmt.toICalString(inc).toUtf8();

    BackendRecord rec;
    rec.id           = inc->uid();
    rec.type         = QStringLiteral("event");
    rec.displayName  = inc->summary();
    rec.data         = ical;
    rec.contentHash  = QCryptographicHash::hash(ical, QCryptographicHash::Sha256).toHex();
    rec.lastModified = inc->lastModified().isValid()
                           ? inc->lastModified()
                           : QDateTime::currentDateTimeUtc();
    rec.isDeleted    = false;
    return rec;
}

} // anonymous namespace

// --- Identity ---

QString SubscriptionBackend::backendId() const
{
    return QStringLiteral("subscription:%1").arg(BackendTypeName);
}

QString SubscriptionBackend::displayName() const
{
    return QObject::tr("Subscription Calendar");
}

bool SubscriptionBackend::isAvailable() const
{
    return true;  // In-process generator; always available
}

// --- Collections ---

QList<CollectionInfo> SubscriptionBackend::availableCollections()
{
    QList<CollectionInfo> result;
    for (const SourceInfo &info : m_sources) {
        CollectionInfo ci;
        ci.id   = info.sourceId;
        ci.name = sourceDisplayName(info.sourceId);
        ci.type = QStringLiteral("calendar");
        result.append(ci);
    }
    return result;
}

CollectionInfo SubscriptionBackend::collectionInfo(const QString &collectionId)
{
    CollectionInfo ci;
    if (m_sources.contains(collectionId)) {
        ci.id   = collectionId;
        ci.name = sourceDisplayName(collectionId);
        ci.type = QStringLiteral("calendar");
    }
    return ci;
}

QString SubscriptionBackend::createCollection(const CollectionInfo &info)
{
    Q_UNUSED(info);
    qWarning() << "SubscriptionBackend::createCollection: read-only backend, creation rejected";
    return {};
}

// --- Records ---

QList<BackendRecord> SubscriptionBackend::loadRecords(const QString &collectionId)
{
    if (!m_sources.contains(collectionId)) {
        qWarning() << "SubscriptionBackend::loadRecords: unknown source" << collectionId;
        return {};
    }

    const QDate today     = QDate::currentDate();
    const QDate startDate = today.addYears(-1);
    const QDate endDate   = today.addYears(2);

    const QList<KCalendarCore::Incidence::Ptr> incidences =
        fetchEventsForSource(collectionId, startDate, endDate);

    QList<BackendRecord> records;
    records.reserve(incidences.size());
    for (const auto &inc : incidences) {
        if (inc) {
            records.append(subscriptionBlobRecord(inc));
        }
    }
    return records;
}

std::optional<BackendRecord> SubscriptionBackend::loadRecord(const QString &recordId)
{
    // Search all sources for a matching uid
    for (const SourceInfo &info : m_sources) {
        const QDate today     = QDate::currentDate();
        const QDate startDate = today.addYears(-1);
        const QDate endDate   = today.addYears(2);

        const QList<KCalendarCore::Incidence::Ptr> incidences =
            fetchEventsForSource(info.sourceId, startDate, endDate);

        for (const auto &inc : incidences) {
            if (inc && inc->uid() == recordId) {
                return subscriptionBlobRecord(inc);
            }
        }
    }
    return std::nullopt;
}

// --- Writes — rejected (read-only) ---

QString SubscriptionBackend::createRecord(const QString &collectionId,
                                          const BackendRecord &record)
{
    Q_UNUSED(collectionId);
    Q_UNUSED(record);
    qWarning() << "SubscriptionBackend::createRecord: read-only backend";
    return {};
}

bool SubscriptionBackend::updateRecord(const BackendRecord &record)
{
    Q_UNUSED(record);
    qWarning() << "SubscriptionBackend::updateRecord: read-only backend";
    return false;
}

bool SubscriptionBackend::deleteRecord(const QString &recordId)
{
    Q_UNUSED(recordId);
    qWarning() << "SubscriptionBackend::deleteRecord: read-only backend";
    return false;
}

// --- Change detection ---

QList<BackendRecord> SubscriptionBackend::modifiedSince(const QString &collectionId,
                                                         const QDateTime &since)
{
    // No server-side delta; return all records and let the engine diff by hash
    const QList<BackendRecord> all = loadRecords(collectionId);
    QList<BackendRecord> result;
    for (const BackendRecord &rec : all) {
        if (rec.lastModified > since) {
            result.append(rec);
        }
    }
    return result;
}

QStringList SubscriptionBackend::deletedSince(const QString &collectionId,
                                               const QDateTime &since)
{
    Q_UNUSED(collectionId);
    Q_UNUSED(since);
    // Read-only generated feeds have no deletion tracking
    return {};
}

} // namespace Kalburator::Sync
