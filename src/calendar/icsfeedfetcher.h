#ifndef KALBURATOR_CALENDAR_ICSFEEDFETCHER_H
#define KALBURATOR_CALENDAR_ICSFEEDFETCHER_H

#include <QDate>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include <KCalendarCore/Incidence>

class QNetworkAccessManager;

namespace Kalburator::Sync {

/**
 * @brief Synchronous fetcher for iCalendar feeds over HTTP(S) and file://.
 *
 * Wraps QNetworkAccessManager + KCalendarCore::ICalFormat into a single
 * blocking call returning the parsed Incidences (with optional date-range
 * filtering, RRULE expansion handled by KCalendarCore).
 *
 * The caller owns the QNetworkAccessManager (lets the caller centralise
 * proxy/redirect policy and keeps the fetcher trivially mockable).
 *
 * Designed for sibling-of-HolidaySubscriptionBackend reuse: a future
 * WebcalSubscriptionBackend can wrap this fetcher; the WildPalms webcal
 * plugin uses it directly as the source side of SyncEngine::runBlobMirror.
 */
class IcsFeedFetcher : public QObject
{
    Q_OBJECT
public:
    struct Result {
        bool success = false;
        QString errorMessage;
        QList<KCalendarCore::Incidence::Ptr> incidences;
    };

    explicit IcsFeedFetcher(QNetworkAccessManager *network,
                            QObject *parent = nullptr);
    ~IcsFeedFetcher() override;

    /**
     * @brief Fetch and parse a feed.
     *
     * If both startDate and endDate are valid, only incidences active in
     * [startDate, endDate] are returned (single-occurrence: dtStart in
     * range; recurring: any occurrence in range, RRULE/EXDATE expanded
     * by KCalendarCore).
     *
     * Synchronous: spins a local QEventLoop until the reply finishes or
     * the timeout fires.
     */
    Result fetch(const QUrl &url,
                 const QDate &startDate = {},
                 const QDate &endDate   = {},
                 int timeoutMs = 30000);

Q_SIGNALS:
    void progress(const QString &message);

private:
    QNetworkAccessManager *m_network; // borrowed
};

} // namespace Kalburator::Sync

#endif
