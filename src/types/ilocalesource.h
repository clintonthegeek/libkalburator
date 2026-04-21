#ifndef ILOCALESOURCE_H
#define ILOCALESOURCE_H

#include <QLocale>
#include <QString>
#include <QTimeZone>

namespace Kalburator::Sync {

/**
 * @brief Interface for locale, timezone, and time format settings.
 *
 * Libraries use ILocaleSource::global() instead of depending on AppSettings
 * directly. The app shell sets the global instance at startup.
 */
class ILocaleSource
{
public:
    virtual ~ILocaleSource() = default;

    struct GeoLocation {
        double latitude = 0.0;
        double longitude = 0.0;
        bool valid = false;
    };

    virtual QLocale effectiveLocale() const = 0;
    virtual Qt::DayOfWeek effectiveFirstDayOfWeek() const = 0;
    virtual QString effectiveTimeFormatString() const = 0;
    virtual QTimeZone effectiveTimezone() const = 0;
    virtual int defaultEventDurationSecs() const = 0;
    virtual GeoLocation effectiveLocation() const = 0;

    // Global accessor (set by app shell at startup)
    static ILocaleSource* global();
    static void setGlobal(ILocaleSource* source);

private:
    static ILocaleSource* s_global;
};

} // namespace Kalburator::Sync

#endif // ILOCALESOURCE_H
