#include "icsfeedfetcher.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimeZone>
#include <QTimer>

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

namespace Kalburator::Sync {

IcsFeedFetcher::IcsFeedFetcher(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), m_network(network)
{
}

IcsFeedFetcher::~IcsFeedFetcher() = default;

IcsFeedFetcher::Result IcsFeedFetcher::fetch(const QUrl &url,
                                              const QDate &startDate,
                                              const QDate &endDate,
                                              int timeoutMs)
{
    Result r;
    if (!url.isValid() || url.scheme().isEmpty()) {
        r.errorMessage = QStringLiteral("Invalid URL");
        return r;
    }
    if (!m_network) {
        r.errorMessage = QStringLiteral("No QNetworkAccessManager");
        return r;
    }

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("libkalburator/1.0"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    Q_EMIT progress(QStringLiteral("Fetching %1").arg(url.toString()));

    QNetworkReply *reply = m_network->get(req);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    if (!timer.isActive()) {
        reply->abort();
        reply->deleteLater();
        r.errorMessage = QStringLiteral("Timeout");
        return r;
    }
    timer.stop();

    if (reply->error() != QNetworkReply::NoError) {
        r.errorMessage = reply->errorString();
        reply->deleteLater();
        return r;
    }

    const QByteArray data = reply->readAll();
    reply->deleteLater();

    if (data.isEmpty()) {
        r.errorMessage = QStringLiteral("Empty response");
        return r;
    }

    KCalendarCore::ICalFormat fmt;
    auto cal = KCalendarCore::MemoryCalendar::Ptr(
        new KCalendarCore::MemoryCalendar(QTimeZone::utc()));
    if (!fmt.fromRawString(cal, data)) {
        r.errorMessage = QStringLiteral("Failed to parse iCalendar");
        return r;
    }

    const auto raw = cal->rawIncidences();

    if (!startDate.isValid() && !endDate.isValid()) {
        r.incidences = raw;
    } else {
        const QDate start = startDate.isValid() ? startDate : QDate(1970, 1, 1);
        const QDate end   = endDate.isValid()   ? endDate   : QDate(9999, 12, 31);
        for (const auto &inc : raw) {
            if (inc->recurs()) {
                bool active = false;
                for (QDate d = start; d <= end; d = d.addDays(1)) {
                    if (inc->recursOn(d, QTimeZone::utc())) {
                        active = true;
                        break;
                    }
                }
                if (active) {
                    r.incidences.append(inc);
                }
            } else {
                const QDate d = inc->dtStart().date();
                if (d.isValid() && d >= start && d <= end) {
                    r.incidences.append(inc);
                }
            }
        }
    }

    r.success = true;
    return r;
}

} // namespace Kalburator::Sync
