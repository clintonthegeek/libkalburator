#include "holidaysubscriptionbackend.h"
#include <KHolidays/Holiday>
#include <KCalendarCore/ICalFormat>
#include <QDateTime>
#include <QDebug>
#include "ilocalesource.h"

namespace Kalburator::Sync {

HolidaySubscriptionBackend::HolidaySubscriptionBackend(QObject *parent)
    : SubscriptionBackend(parent)
{
}

HolidaySubscriptionBackend::~HolidaySubscriptionBackend()
{
    // Clean up cached HolidayRegion instances
    qDeleteAll(m_regionCache);
    m_regionCache.clear();
}

void HolidaySubscriptionBackend::setEnabledRegions(const QStringList &regionCodes)
{
    // Remove all existing sources
    for (const QString &sourceId : sources()) {
        removeSource(sourceId);
    }

    // Add new regions
    for (const QString &regionCode : regionCodes) {
        if (!regionCode.isEmpty() && isValidRegion(regionCode)) {
            QVariantMap config;
            config["name"] = regionName(regionCode);
            config["regionCode"] = regionCode;
            addSource(regionCode, QStringLiteral("holiday"), config);
        } else {
            qWarning() << "Invalid region code:" << regionCode;
        }
    }
}

QStringList HolidaySubscriptionBackend::enabledRegions() const
{
    QStringList regions;
    for (const QString &sourceId : sources()) {
        if (sourceType(sourceId) == QStringLiteral("holiday")) {
            regions << sourceId;
        }
    }
    return regions;
}

bool HolidaySubscriptionBackend::addRegion(const QString &regionCode)
{
    if (regionCode.isEmpty() || !isValidRegion(regionCode)) {
        return false;
    }

    if (sources().contains(regionCode)) {
        return false;  // Already exists
    }

    QVariantMap config;
    config["name"] = regionName(regionCode);
    config["regionCode"] = regionCode;
    addSource(regionCode, QStringLiteral("holiday"), config);
    return true;
}

void HolidaySubscriptionBackend::removeRegion(const QString &regionCode)
{
    removeSource(regionCode);

    // Also clean up cached region instance
    if (m_regionCache.contains(regionCode)) {
        delete m_regionCache.take(regionCode);
    }
}

QStringList HolidaySubscriptionBackend::availableRegionCodes()
{
    return KHolidays::HolidayRegion::regionCodes();
}

QString HolidaySubscriptionBackend::regionName(const QString &regionCode)
{
    if (regionCode.isEmpty()) {
        return QString();
    }

    // Create a temporary region to get its name
    KHolidays::HolidayRegion region(regionCode);
    if (region.isValid()) {
        return region.name();
    }

    return regionCode;  // Fallback to code if region is invalid
}

QString HolidaySubscriptionBackend::regionDescription(const QString &regionCode)
{
    if (regionCode.isEmpty()) {
        return QString();
    }

    // Create a temporary region to get its description
    KHolidays::HolidayRegion region(regionCode);
    if (region.isValid()) {
        return region.description();
    }

    return QString();
}

bool HolidaySubscriptionBackend::isValidRegion(const QString &regionCode)
{
    if (regionCode.isEmpty()) {
        return false;
    }

    KHolidays::HolidayRegion region(regionCode);
    return region.isValid();
}

QList<KCalendarCore::Incidence::Ptr> HolidaySubscriptionBackend::fetchEventsForSource(
    const QString &sourceId,
    const QDate &startDate,
    const QDate &endDate)
{
    qDebug() << "=== HolidaySubscriptionBackend::fetchEventsForSource ===";
    qDebug() << "  Source ID:" << sourceId;
    qDebug() << "  Date range:" << startDate << "to" << endDate;

    QList<KCalendarCore::Incidence::Ptr> events;

    KHolidays::HolidayRegion *region = getOrCreateRegion(sourceId);
    if (!region || !region->isValid()) {
        qWarning() << "Invalid or missing holiday region:" << sourceId;
        return events;
    }

    qDebug() << "  Region is valid:" << region->isValid();
    qDebug() << "  Region name:" << region->name();
    qDebug() << "  Region description:" << region->description();

    // Fetch holidays for the date range
    KHolidays::Holiday::List holidays = region->rawHolidays(startDate, endDate);
    qDebug() << "  Raw holidays fetched:" << holidays.size();

    for (const KHolidays::Holiday &holiday : holidays) {
        qDebug() << "    Holiday:" << holiday.name() << "on" << holiday.observedStartDate();
        KCalendarCore::Event::Ptr event = convertHolidayToEvent(holiday, sourceId);
        if (event) {
            events << event;
        }
    }

    qDebug() << "  Total events created:" << events.size();
    return events;
}

QString HolidaySubscriptionBackend::sourceDisplayName(const QString &sourceId) const
{
    // Try to get name from source config first
    QString name = SubscriptionBackend::sourceDisplayName(sourceId);
    if (!name.isEmpty() && name != sourceId) {
        return name;
    }

    // Fallback to region name
    return regionName(sourceId);
}

KCalendarCore::Event::Ptr HolidaySubscriptionBackend::convertHolidayToEvent(
    const KHolidays::Holiday &holiday,
    const QString &regionCode) const
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());

    // Set basic properties
    event->setSummary(holiday.name());

    // Set description if available
    QString description = holiday.description();
    if (!description.isEmpty()) {
        event->setDescription(description);
    }

    // Set date/time - holidays are all-day events
    QDateTime startDateTime(holiday.observedStartDate(), QTime(0, 0));
    event->setDtStart(startDateTime);
    event->setAllDay(true);

    // Handle multi-day holidays
    if (holiday.observedEndDate() != holiday.observedStartDate()) {
        // For all-day events, the end date is exclusive in iCalendar
        // So we need to add 1 day to the observed end date
        QDateTime endDateTime(holiday.observedEndDate().addDays(1), QTime(0, 0));
        event->setDtEnd(endDateTime);
    }

    // Generate a unique UID
    // Format: holiday-{regionCode}-{date}-{name-hash}
    QString uidBase = QString("holiday-%1-%2-%3")
                          .arg(regionCode)
                          .arg(holiday.observedStartDate().toString(Qt::ISODate))
                          .arg(qHash(holiday.name()));
    event->setUid(uidBase);

    // Mark as read-only
    event->setReadOnly(true);

    // Set category for filtering
    event->setCategories(QStringList() << QStringLiteral("Holiday"));

    // Add custom properties to identify the source
    event->setCustomProperty("X-PLANSTAN", "SOURCE", "holiday");
    event->setCustomProperty("X-PLANSTAN", "REGION", regionCode);

    // Set holiday type as custom property if available
    QString holidayType;
    switch (holiday.dayType()) {
    case KHolidays::Holiday::Workday:
        holidayType = QStringLiteral("workday");
        break;
    case KHolidays::Holiday::NonWorkday:
        holidayType = QStringLiteral("non-workday");
        break;
    default:
        holidayType = QStringLiteral("holiday");
        break;
    }
    event->setCustomProperty("X-PLANSTAN", "HOLIDAY-TYPE", holidayType);

    // Set created/last-modified timestamps
    QDateTime now = QDateTime::currentDateTimeUtc();
    event->setCreated(now);
    event->setLastModified(now);

    return event;
}

KHolidays::HolidayRegion* HolidaySubscriptionBackend::getOrCreateRegion(const QString &regionCode)
{
    // Check cache first
    if (m_regionCache.contains(regionCode)) {
        return m_regionCache[regionCode];
    }

    // Create new region instance
    auto *region = new KHolidays::HolidayRegion(regionCode);
    if (region->isValid()) {
        m_regionCache[regionCode] = region;
        return region;
    }

    // Invalid region
    delete region;
    return nullptr;
}


} // namespace Kalburator::Sync
