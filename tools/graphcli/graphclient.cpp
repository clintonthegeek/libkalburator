#include "graphclient.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

HttpResponse httpRequest(const QUrl &url,
                         const QByteArray &method,
                         const QList<QPair<QByteArray, QByteArray>> &headers,
                         const QByteArray &body)
{
    HttpResponse response;

    QNetworkAccessManager nam;
    QNetworkRequest request(url);
    request.setTransferTimeout(60'000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    for (const auto &[name, value] : headers)
        request.setRawHeader(name, value);

    QNetworkReply *reply = nam.sendCustomRequest(request, method, body);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(120'000, &loop, &QEventLoop::quit);
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
