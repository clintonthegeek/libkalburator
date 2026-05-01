#ifndef HOLIDAYSUBSCRIPTIONBACKEND_H
#define HOLIDAYSUBSCRIPTIONBACKEND_H

#include "subscriptionbackend.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QDate>
#include <KHolidays/HolidayRegion>
#include <KCalendarCore/Event>

namespace Kalburator::Sync {

/**
 * @brief Subscription backend for holiday calendars via KHolidays.
 *
 * Generates read-only calendar events from KDE's KHolidays library, which
 * provides holiday data for 100+ countries and regions worldwide.
 *
 * Each enabled holiday region creates a separate calendar (e.g., "US Holidays",
 * "UK Holidays"). Multiple regions can be active simultaneously.
 *
 * ## Region Codes
 * Region codes follow ISO format: country_language (e.g., "us_en-us", "gb_en-gb").
 * Use availableRegionCodes() to get the full list of supported regions.
 *
 * ## Event Properties
 * Holiday events are created with:
 * - summary: Holiday name (localized based on app locale)
 * - description: Holiday description (if available)
 * - allDay: true
 * - readOnly: true
 * - categories: ["Holiday"]
 * - custom property: X-PLANSTAN-SOURCE = "holiday"
 * - custom property: X-PLANSTAN-REGION = region code
 *
 * ## Usage
 * ```cpp
 * auto *backend = new HolidaySubscriptionBackend(this);
 * backend->setEnabledRegions({"us_en-us", "gb_en-gb"});
 * backend->loadCalendars(collectionId);
 * ```
 */
class HolidaySubscriptionBackend : public SubscriptionBackend
{
    Q_OBJECT

public:
    explicit HolidaySubscriptionBackend(QObject *parent = nullptr);
    ~HolidaySubscriptionBackend() override;

    QList<Kalburator::Shape::Shape> nativeShapes() const override;

    // ========== Holiday Region Management ==========

    /**
     * @brief Enable specific holiday regions.
     *
     * Each region creates a separate calendar. Previous regions are replaced.
     *
     * @param regionCodes List of region codes (e.g., ["us_en-us", "gb_en-gb"])
     */
    void setEnabledRegions(const QStringList &regionCodes);

    /**
     * @brief Get currently enabled region codes.
     */
    QStringList enabledRegions() const;

    /**
     * @brief Add a single holiday region.
     *
     * @param regionCode Region code to add
     * @return true if region was added, false if invalid or already exists
     */
    bool addRegion(const QString &regionCode);

    /**
     * @brief Remove a single holiday region.
     *
     * @param regionCode Region code to remove
     */
    void removeRegion(const QString &regionCode);

    // ========== Static Utilities for UI ==========

    /**
     * @brief Get list of all available holiday region codes.
     *
     * @return List of region codes (e.g., ["us_en-us", "gb_en-gb", ...])
     */
    static QStringList availableRegionCodes();

    /**
     * @brief Get human-readable name for a region.
     *
     * @param regionCode Region code (e.g., "us_en-us")
     * @return Display name (e.g., "United States")
     */
    static QString regionName(const QString &regionCode);

    /**
     * @brief Get description for a region.
     *
     * @param regionCode Region code
     * @return Description string
     */
    static QString regionDescription(const QString &regionCode);

    /**
     * @brief Check if a region code is valid.
     */
    static bool isValidRegion(const QString &regionCode);

protected:
    /**
     * @brief Fetch holiday events for a specific region.
     *
     * Implements SubscriptionBackend::fetchEventsForSource().
     *
     * @param sourceId Region code (e.g., "us_en-us")
     * @param startDate Start of date range
     * @param endDate End of date range
     * @return List of holiday events
     */
    QList<KCalendarCore::Incidence::Ptr> fetchEventsForSource(
        const QString &sourceId,
        const QDate &startDate,
        const QDate &endDate) override;

    /**
     * @brief Get display name for a region calendar.
     *
     * @param sourceId Region code
     * @return Display name (e.g., "US Holidays")
     */
    QString sourceDisplayName(const QString &sourceId) const override;

private:
    /**
     * @brief Convert a KHolidays::Holiday to a KCalendarCore::Event.
     *
     * @param holiday The holiday data
     * @param regionCode The region this holiday belongs to
     * @return Event pointer
     */
    KCalendarCore::Event::Ptr convertHolidayToEvent(
        const KHolidays::Holiday &holiday,
        const QString &regionCode) const;

    /**
     * @brief Get or create a HolidayRegion instance for a region code.
     *
     * HolidayRegion instances are cached for performance.
     *
     * @param regionCode Region code
     * @return HolidayRegion pointer, or nullptr if invalid
     */
    KHolidays::HolidayRegion* getOrCreateRegion(const QString &regionCode);

    // Cache of HolidayRegion instances (owned by this backend)
    QHash<QString, KHolidays::HolidayRegion*> m_regionCache;
};

} // namespace Kalburator::Sync

#endif // HOLIDAYSUBSCRIPTIONBACKEND_H
