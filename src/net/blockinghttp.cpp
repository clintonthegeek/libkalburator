#include "blockinghttp.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace Kalburator::Net {

HttpResponse httpRequest(
    const QUrl &url,
    const QByteArray &method,
    const QList<QPair<QByteArray, QByteArray>> &headers,
    const QByteArray &body)
{
    HttpResponse response;

    QNetworkAccessManager nam;
    QNetworkRequest request(url);
    request.setTransferTimeout(60000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    for (const auto &h : headers)
        request.setRawHeader(h.first, h.second);

    QNetworkReply *reply = nam.sendCustomRequest(request, method, body);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(120000, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        const int httpStatus = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (httpStatus == 0)
            response.error = reply->errorString();
        response.status = httpStatus;
    } else {
        response.status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
    }
    response.body = reply->readAll();
    reply->deleteLater();
    return response;
}

QString urlEncodePathSegment(const QString &segment)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(segment, "-._~"));
}

} // namespace Kalburator::Net
